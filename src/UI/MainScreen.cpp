#include "MainScreen.hpp"

#include "Components/StatusBar.hpp"
#include "NavigationSidebar.hpp"
#include "State/Reducer.hpp"
#include "UI/UIHelpers.hpp"
#include "Views/AuthModal.hpp"
#include "Views/CreateTaskView.hpp"
#include "Views/ProjectModal.hpp"
#include "Views/ReviewView.hpp"
#include "Views/TaskBoardView.hpp"

#include <chrono>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
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
                      std::function<void()> onProjectSwitch,
                      std::function<void(ViewType)> onNavigate,
                      std::function<void()> onEscape)
        : _onQuit(std::move(onQuit)),
          _onProjectSwitch(std::move(onProjectSwitch)),
          _onNavigate(std::move(onNavigate)), _onEscape(std::move(onEscape)) {
        Add(std::move(child));
    }

    bool OnEvent(Event event) override {
        if (event == Event::Character('\x03')) {
            _onQuit();
            return true;
        }
        if (event == Event::Character('\x10')) {
            _onProjectSwitch();
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
    std::function<void()> _onProjectSwitch;
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
    if (_actionThread.joinable()) {
        _actionThread.request_stop();
        _actionThread.join();
    }
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
    _state.connectionStatus = _state.session.IsAuthenticated()
                                  ? State::ConnectionStatus::Connecting
                                  : State::ConnectionStatus::Offline;
    if (!_state.session.IsAuthenticated()) {
        if (_configManager && !_configManager->GetSavedProjects().empty()) {
            _state.session.serverHost.clear();
            _state.activeModal = State::ModalType::ProjectModal;
            _showProjectModal = true;
        } else {
            _state.activeModal = State::ModalType::AuthModal;
            _showAuthModal = true;
        }
    }
}

void MainScreen::StartHealthMonitoring() {
    if (!_backendClient) {
        return;
    }
    if (_healthChecker) {
        _healthChecker->Stop();
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
        _showProjectModal =
            (_state.activeModal == State::ModalType::ProjectModal);
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
    if (!_state.session.IsAuthenticated() &&
        _state.session.serverHost.empty()) {
        return hbox({
                   text(" Aquarium TUI ") | bold | color(Color::CyanLight),
                   text(" - No project selected (Ctrl+P to select)") | dim,
                   filler(),
               }) |
               border;
    }

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
    const auto generation = _sessionGeneration.load();
    std::thread([this, generation] {
        auto usersRes = _backendClient->GetAllUsers();
        if (generation != _sessionGeneration.load()) {
            return;
        }
        if (usersRes.HasValue()) {
            Dispatch(State::UsersLoadedAction{usersRes.Value()});
        }

        auto tasksRes = _backendClient->GetTasks();
        if (generation != _sessionGeneration.load()) {
            return;
        }
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
                if (generation != _sessionGeneration.load()) {
                    return;
                }
                if (commentsRes.HasValue()) {
                    Dispatch(State::CommentsLoadedAction{
                        .taskId = firstId,
                        .comments = commentsRes.Value(),
                    });
                }
            }
        } else {
            Dispatch(State::ShowToastAction{
                .message = "Failed to load tasks: " + tasksRes.Error().message,
                .severity = State::ToastSeverity::Error,
            });
        }
    }).detach();
}

void MainScreen::ExitToProjectSelector() {
    if (_state.activeModal == State::ModalType::ProjectModal &&
        !_state.session.IsAuthenticated()) {
        return;
    }

    ++_sessionGeneration;
    _projectModalError.clear();

    if (_healthChecker) {
        _healthChecker->Stop();
    }
    if (_eventSubscriber) {
        _eventSubscriber->Stop();
    }
    if (_backendClient) {
        _backendClient->SetAuthToken("");
    }

    Dispatch(State::ExitProjectAction{});
}

void MainScreen::HandleSelectProject(const std::string& server) {
    if (!_configManager || !_backendClient) {
        return;
    }

    _projectModalError.clear();
    if (!_configManager->SwitchActiveProject(server)) {
        _projectModalError = "Project '" + server + "' not found in config.";
        Dispatch(State::ShowToastAction{
            .message = _projectModalError,
            .severity = State::ToastSeverity::Error,
        });
        return;
    }

    auto session = _configManager->GetActiveSession();
    if (!session.has_value() || session->authToken.empty()) {
        _backendClient->SetTargetServer(server);
        _backendClient->SetAuthToken("");
        Dispatch(State::SetSessionAction{
            .server = server,
            .userId = 0,
            .userName = "",
            .token = "",
        });
        Dispatch(State::OpenModalAction{State::ModalType::AuthModal});
        return;
    }

    ++_sessionGeneration;
    if (_healthChecker) {
        _healthChecker->Stop();
    }
    if (_eventSubscriber) {
        _eventSubscriber->Stop();
    }

    _backendClient->SetTargetServer(server);
    _backendClient->SetAuthToken(session->authToken);

    Dispatch(State::SetSessionAction{
        .server = server,
        .userId = session->userId,
        .userName = session->userName,
        .token = session->authToken,
    });
    Dispatch(
        State::SetConnectionStatusAction{State::ConnectionStatus::Connecting});
    Dispatch(State::CloseModalAction{});
    Dispatch(State::ShowToastAction{
        .message = "Switched to project " + server,
        .severity = State::ToastSeverity::Success,
    });

    StartHealthMonitoring();
    StartEventSubscription();
    FetchInitialData();
}

void MainScreen::HandleDeleteProject(const std::string& server,
                                     bool forceLocal) {
    if (!_configManager) {
        return;
    }

    const auto& servers = _configManager->GetConfig().servers;
    auto it = servers.find(server);
    if (it == servers.end()) {
        return;
    }
    const Config::ServerEntry entry = it->second;

    if (forceLocal || entry.userId <= 0 || entry.authToken.empty()) {
        _configManager->RemoveProject(server);
        _projectModalError.clear();
        Dispatch(State::ShowToastAction{
            .message = "Project '" + server + "' removed from config.",
            .severity = State::ToastSeverity::Info,
        });
        return;
    }

    if (_isDeletingProject.exchange(true)) {
        return;
    }
    _projectModalError.clear();

    if (_actionThread.joinable()) {
        _actionThread.request_stop();
        _actionThread.join();
    }

    _actionThread = std::jthread([this, server, entry](std::stop_token) {
        Common::Result<bool> res(Common::AppError{
            .code = Common::AppErrorCode::INTERNAL_ERROR,
            .message = "Client unavailable",
            .details = "",
        });

        if (_backendClient &&
            _backendClient->GetTargetServer() == "custom_channel") {
            _backendClient->SetAuthToken(entry.authToken);
            res = _backendClient->DeleteUser(entry.userId);
        } else {
            Grpc::BackendClient deleteClient(server, entry.authToken);
            res = deleteClient.DeleteUser(entry.userId);
        }

        _screen.Post([this, server, res] {
            _isDeletingProject = false;
            if (res.HasValue()) {
                _configManager->RemoveProject(server);
                _projectModalError.clear();
                _state = Reducer::Reduce(
                    _state,
                    State::ShowToastAction{
                        .message = "Project '" + server +
                                   "' deleted and user removed from server.",
                        .severity = State::ToastSeverity::Success,
                    });
            } else if (res.Error().code ==
                           Common::AppErrorCode::UNAUTHENTICATED ||
                       res.Error().code == Common::AppErrorCode::NOT_FOUND) {
                _configManager->RemoveProject(server);
                _projectModalError.clear();
                _state = Reducer::Reduce(
                    _state,
                    State::ShowToastAction{
                        .message = "Project '" + server +
                                   "' removed (user inactive on server).",
                        .severity = State::ToastSeverity::Info,
                    });
            } else {
                _projectModalError =
                    "Failed to delete user from server: " + res.Error().message;
                _state = Reducer::Reduce(
                    _state, State::ShowToastAction{
                                .message = _projectModalError,
                                .severity = State::ToastSeverity::Error,
                            });
            }
        });
        _screen.PostEvent(Event::Custom);
    });
}

void MainScreen::HandleAuthSubmit(const std::string& server,
                                  const std::string& name,
                                  const std::string& inviteKey) {
    if (!_backendClient) {
        return;
    }

    ++_sessionGeneration;
    if (_healthChecker) {
        _healthChecker->Stop();
    }
    if (_eventSubscriber) {
        _eventSubscriber->Stop();
    }

    _backendClient->SetTargetServer(server);
    _backendClient->SetAuthToken("");
    auto res = _backendClient->AddUser(name, inviteKey);
    if (res.HasValue()) {
        auto user = res.Value();
        if (_configManager) {
            _configManager->SaveProjectSession(server, user.id(), user.name(),
                                               user.key());
        }
        _backendClient->SetAuthToken(user.key());

        Dispatch(State::SetSessionAction{
            .server = server,
            .userId = user.id(),
            .userName = user.name(),
            .token = user.key(),
        });
        Dispatch(State::SetConnectionStatusAction{
            State::ConnectionStatus::Connecting});
        Dispatch(State::CloseModalAction{});
        Dispatch(State::ShowToastAction{
            .message = "Successfully registered as " + user.name() + "!",
            .severity = State::ToastSeverity::Success,
        });
        StartHealthMonitoring();
        StartEventSubscription();
        FetchInitialData();
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
            auto termSize = Terminal::Size();
            if (termSize.dimx > 0 && termSize.dimy > 0 &&
                (termSize.dimx < 80 || termSize.dimy < 24)) {
                return RenderTerminalTooSmallWarning(termSize.dimx,
                                                     termSize.dimy, 80, 24);
            }
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
            .onCancel =
                [this] {
                    if (!_state.session.IsAuthenticated()) {
                        Dispatch(State::OpenModalAction{
                            State::ModalType::ProjectModal});
                    } else {
                        Dispatch(State::CloseModalAction{});
                    }
                },
        });

    auto projectModal = Views::CreateProjectModal(
        _state, Views::ProjectModalCallbacks{
                    .getProjects =
                        [this] {
                            std::vector<Views::ProjectEntryView> result;
                            if (_configManager) {
                                auto saved = _configManager->GetSavedProjects();
                                result.reserve(saved.size());
                                for (const auto& p : saved) {
                                    result.push_back(Views::ProjectEntryView{
                                        .server = p.server,
                                        .userId = p.entry.userId,
                                        .userName = p.entry.userName,
                                    });
                                }
                            }
                            return result;
                        },
                    .onSelectProject =
                        [this](const std::string& server) {
                            HandleSelectProject(server);
                        },
                    .onAddProject =
                        [this] {
                            _projectModalError.clear();
                            Dispatch(State::OpenModalAction{
                                State::ModalType::AuthModal});
                        },
                    .onDeleteProject =
                        [this](const std::string& server, bool forceLocal) {
                            HandleDeleteProject(server, forceLocal);
                        },
                    .getErrorMessage = [this] { return _projectModalError; },
                    .isBusy = [this] { return _isDeletingProject.load(); },
                    .onClearError = [this] { _projectModalError.clear(); },
                });

    auto withAuthModal = Modal(layoutRenderer, authModal, &_showAuthModal);
    auto modalContainer =
        Modal(withAuthModal, projectModal, &_showProjectModal);

    return Make<GlobalEventFilter>(
        modalContainer, [this] { _screen.ExitLoopClosure()(); },
        [this] { ExitToProjectSelector(); },
        [this](ViewType v) {
            if (_state.activeModal == State::ModalType::None) {
                Dispatch(State::NavigateToViewAction{v});
            }
        },
        [this] {
            if (_state.activeModal != State::ModalType::ProjectModal &&
                _state.activeModal != State::ModalType::AuthModal) {
                Dispatch(State::CloseModalAction{});
            }
            Dispatch(State::DismissToastAction{});
        });
}

void MainScreen::Run() {
    if (_state.session.IsAuthenticated()) {
        StartHealthMonitoring();
        StartEventSubscription();
        FetchInitialData();
    }
    auto rootComponent = BuildLayout();
    _screen.Loop(rootComponent);
}

} // namespace Aquarium::UI
