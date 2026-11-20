#include "MainScreen.hpp"

#include "Components/StatusBar.hpp"
#include "NavigationSidebar.hpp"
#include "State/Reducer.hpp"
#include "UI/UIHelpers.hpp"
#include "Views/AuthModal.hpp"
#include "Views/CreateTaskView.hpp"
#include "Views/ReviewView.hpp"
#include "Views/TaskBoardView.hpp"

#include <chrono>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Aquarium::UI {

using namespace ftxui;
using State::Action;
using State::Reducer;
using State::ViewType;

namespace {

class GlobalEventFilter : public ComponentBase {
public:
    GlobalEventFilter(Component child,
                      std::function<void()> onQuit,
                      std::function<void(ViewType)> onNavigate,
                      std::function<void()> onEscape)
        : _onQuit(std::move(onQuit)), _onNavigate(std::move(onNavigate)),
          _onEscape(std::move(onEscape)) {
        Add(std::move(child));
    }

    bool OnEvent(Event event) override {
        if (event == Event::Character('\x03')) {
            _onQuit();
            return true;
        }

        // Let active child (e.g. Input, Button) process event first
        if (ComponentBase::OnEvent(event)) {
            return true;
        }

        // Child did not consume event: check global hotkeys
        if (event == Event::Character('q')) {
            _onQuit();
            return true;
        }
        if (event == Event::Character('1')) {
            _onNavigate(ViewType::Tasks);
            return true;
        }
        if (event == Event::Character('2')) {
            _onNavigate(ViewType::Review);
            return true;
        }
        if (event == Event::Character('3')) {
            _onNavigate(ViewType::CreateTask);
            return true;
        }
        if (event == Event::Escape) {
            _onEscape();
            return true;
        }
        return false;
    }

private:
    std::function<void()> _onQuit;
    std::function<void(ViewType)> _onNavigate;
    std::function<void()> _onEscape;
};

} // namespace

MainScreen::MainScreen(std::shared_ptr<Config::ConfigManager> configManager,
                       std::shared_ptr<Grpc::BackendClient> backendClient)
    : _configManager(std::move(configManager)),
      _backendClient(std::move(backendClient)),
      _screen(ScreenInteractive::Fullscreen()) {
    InitState();
}

MainScreen::~MainScreen() {
    if (_healthChecker) {
        _healthChecker->Stop();
    }
    if (_eventSubscriber) {
        _eventSubscriber->Stop();
    }
}

void MainScreen::InitState() {
    if (_configManager) {
        _state.session.serverHost = _configManager->GetActiveServer();
        _state.session.authToken = _configManager->GetActiveToken();
        auto session = _configManager->GetActiveSession();
        if (session.has_value()) {
            _state.session.userId = session->userId;
            _state.session.userName = session->userName;
        }
    }
    _state.connectionStatus = State::ConnectionStatus::Connecting;
    if (!_state.session.IsAuthenticated()) {
        _state.activeModal = State::ModalType::AuthModal;
        _showAuthModal = true;
    }
}

void MainScreen::StartHealthMonitoring() {
    if (!_backendClient) {
        return;
    }

    _healthChecker = std::make_unique<Grpc::HealthChecker>(
        _backendClient, std::chrono::seconds(5),
        [this](const Common::Result<aquarium::api::HealthResponse>& result) {
            if (result.HasValue() &&
                result.Value().status() ==
                    aquarium::api::HealthResponse::SERVING) {
                Dispatch(State::SetConnectionStatusAction{
                    State::ConnectionStatus::Connected});
            } else {
                Dispatch(State::SetConnectionStatusAction{
                    State::ConnectionStatus::Offline});
            }
        });
    _healthChecker->Start();
}

void MainScreen::StartEventSubscription() {
    if (!_backendClient) {
        return;
    }
    if (_eventSubscriber) {
        _eventSubscriber->Stop();
    }
    _eventSubscriber = std::make_unique<Grpc::EventSubscriber>(
        _backendClient,
        [this](const aquarium::api::TaskEvent& event) {
            HandleTaskEvent(event);
        },
        [this] { FetchInitialData(); });
    _eventSubscriber->Start();
}

void MainScreen::HandleTaskEvent(const aquarium::api::TaskEvent& event) {
    using aquarium::api::TaskEvent;
    switch (event.type()) {
    case TaskEvent::TASK_CREATED:
        if (event.has_task()) {
            Dispatch(State::TaskCreatedAction{event.task()});
        }
        Dispatch(State::ShowToastAction{
            .message = "Task #" + std::to_string(event.taskid()) + " created",
            .severity = State::ToastSeverity::Info,
        });
        break;

    case TaskEvent::TASK_UPDATED:
    case TaskEvent::WORKER_ASSIGNED:
    case TaskEvent::WORKER_REMOVED:
        if (event.has_task()) {
            Dispatch(State::TaskUpdatedAction{event.task()});
        }
        Dispatch(State::ShowToastAction{
            .message = "Task #" + std::to_string(event.taskid()) + " updated",
            .severity = State::ToastSeverity::Info,
        });
        break;

    case TaskEvent::STATUS_CHANGED:
        if (event.has_task()) {
            Dispatch(State::TaskUpdatedAction{event.task()});
            Dispatch(State::ShowToastAction{
                .message = "Task #" + std::to_string(event.taskid()) +
                           " moved to " +
                           GetStatusBadge(event.task().status()).text,
                .severity = State::ToastSeverity::Info,
            });
        }
        break;

    case TaskEvent::TASK_REJECTED:
        if (event.has_task()) {
            aquarium::api::Comment c = event.has_comment()
                                           ? event.comment()
                                           : aquarium::api::Comment{};
            Dispatch(State::TaskRejectedAction{
                .task = event.task(),
                .comment = c,
            });
        }
        Dispatch(State::ShowToastAction{
            .message = "Task #" + std::to_string(event.taskid()) +
                       " rejected for revision",
            .severity = State::ToastSeverity::Warning,
        });
        break;

    case TaskEvent::COMMENT_ADDED:
        if (event.has_comment()) {
            Dispatch(State::CommentAddedAction{event.comment()});
        }
        Dispatch(State::ShowToastAction{
            .message = "New remark on task #" + std::to_string(event.taskid()),
            .severity = State::ToastSeverity::Info,
        });
        break;

    case TaskEvent::TASK_DELETED:
        Dispatch(State::TaskDeletedAction{event.taskid()});
        Dispatch(State::ShowToastAction{
            .message = "Task #" + std::to_string(event.taskid()) + " deleted",
            .severity = State::ToastSeverity::Info,
        });
        break;

    default:
        break;
    }
}

void MainScreen::Dispatch(const Action& action) {
    _screen.Post([this, action] {
        _state = Reducer::Reduce(_state, action);
        _showAuthModal = (_state.activeModal == State::ModalType::AuthModal);
        _activeTabIndex = static_cast<int>(_state.currentView);
    });
    _screen.PostEvent(Event::Custom);
}

const State::AppState& MainScreen::GetState() const noexcept {
    return _state;
}

ScreenInteractive& MainScreen::GetScreen() noexcept {
    return _screen;
}

Element MainScreen::RenderHeader() const {
    std::string userDisplay =
        _state.session.IsAuthenticated()
            ? _state.session.userName +
                  " [ID: " + std::to_string(_state.session.userId) + "]"
            : "Unauthenticated";

    return hbox({
               text(" Aquarium TUI ") | bold | color(Color::CyanLight),
               text(" - Connected: ") | dim,
               text(_state.session.serverHost) | bold,
               text(" (User: ") | dim,
               text(userDisplay) | color(Color::YellowLight) | bold,
               text(")") | dim,
               filler(),
           }) |
           border;
}

void MainScreen::FetchInitialData() {
    if (!_backendClient) {
        return;
    }
    std::thread([this] {
        auto usersRes = _backendClient->GetAllUsers();
        if (usersRes.HasValue()) {
            Dispatch(State::UsersLoadedAction{usersRes.Value()});
        }

        auto tasksRes = _backendClient->GetTasks();
        if (tasksRes.HasValue()) {
            std::vector<aquarium::api::Task> tasksList;
            tasksList.reserve(tasksRes.Value().tasks_size());
            for (int i = 0; i < tasksRes.Value().tasks_size(); ++i) {
                tasksList.push_back(tasksRes.Value().tasks(i));
            }
            Dispatch(State::TasksLoadedAction{tasksList});

            if (!tasksList.empty()) {
                auto firstId = tasksList.front().id();
                auto commentsRes = _backendClient->GetComments(firstId);
                if (commentsRes.HasValue()) {
                    Dispatch(State::CommentsLoadedAction{
                        .taskId = firstId,
                        .comments = commentsRes.Value(),
                    });
                }
            }
        }
    }).detach();
}

void MainScreen::HandleAuthSubmit(const std::string& server,
                                  const std::string& name,
                                  const std::string& inviteKey) {
    if (!_backendClient) {
        return;
    }
    _backendClient->SetTargetServer(server);
    auto res = _backendClient->AddUser(name, inviteKey);
    if (res.HasValue()) {
        auto user = res.Value();
        if (_configManager) {
            _configManager->UpdateActiveSession(user.id(), user.name(),
                                                user.key());
        }
        _backendClient->SetAuthToken(user.key());

        Dispatch(State::SetSessionAction{
            .server = server,
            .userId = user.id(),
            .userName = user.name(),
            .token = user.key(),
        });
        Dispatch(State::CloseModalAction{});
        Dispatch(State::ShowToastAction{
            .message = "Successfully registered as " + user.name() + "!",
            .severity = State::ToastSeverity::Success,
        });
        FetchInitialData();
        StartEventSubscription();
    } else {
        Dispatch(State::ShowToastAction{
            .message = "Registration failed: " + res.Error().message,
            .severity = State::ToastSeverity::Error,
        });
    }
}

Component MainScreen::BuildLayout() {
    auto sidebar = CreateNavigationSidebar(_state, [this](ViewType view) {
        Dispatch(State::NavigateToViewAction{view});
    });

    auto statusBar = Components::CreateStatusBar(_state);

    auto taskBoard = Views::CreateTaskBoardView(
        _state, [this](const State::Action& a) { Dispatch(a); },
        _backendClient);

    auto reviewView = Views::CreateReviewView(
        _state, [this](const State::Action& a) { Dispatch(a); },
        _backendClient);

    auto createTaskView = Views::CreateCreateTaskView(
        _state, [this](const State::Action& a) { Dispatch(a); },
        _backendClient);

    auto tabWorkspace = Container::Tab({taskBoard, reviewView, createTaskView},
                                       &_activeTabIndex);

    auto middleContainer = Container::Horizontal({
        sidebar,
        tabWorkspace,
    });

    auto rootContainer = Container::Vertical({
        middleContainer,
        statusBar,
    });

    auto layoutRenderer =
        Renderer(rootContainer, [this, sidebar, tabWorkspace, statusBar] {
            return vbox({
                RenderHeader(),
                hbox({
                    sidebar->Render(),
                    tabWorkspace->Render() | flex,
                }) | flex,
                separator(),
                statusBar->Render(),
            });
        });

    auto authModal = Views::CreateAuthModal(
        _state,
        Views::AuthModalCallbacks{
            .onSubmit =
                [this](const std::string& s, const std::string& n,
                       const std::string& k) { HandleAuthSubmit(s, n, k); },
            .onCancel = [this] { Dispatch(State::CloseModalAction{}); },
        });

    auto modalContainer = Modal(layoutRenderer, authModal, &_showAuthModal);

    return Make<GlobalEventFilter>(
        modalContainer, [this] { _screen.ExitLoopClosure()(); },
        [this](ViewType v) { Dispatch(State::NavigateToViewAction{v}); },
        [this] {
            Dispatch(State::CloseModalAction{});
            Dispatch(State::DismissToastAction{});
        });
}

void MainScreen::Run() {
    StartHealthMonitoring();
    StartEventSubscription();
    FetchInitialData();
    auto rootComponent = BuildLayout();
    _screen.Loop(rootComponent);
}

} // namespace Aquarium::UI
