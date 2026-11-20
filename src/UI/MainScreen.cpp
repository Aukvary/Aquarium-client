#include "MainScreen.hpp"

#include "Components/StatusBar.hpp"
#include "NavigationSidebar.hpp"
#include "State/Reducer.hpp"
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

void MainScreen::Dispatch(const Action& action) {
    _screen.Post([this, action] { _state = Reducer::Reduce(_state, action); });
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

Element MainScreen::RenderWorkspace() const {
    switch (_state.currentView) {
    case ViewType::Tasks:
        return text("");

    case ViewType::Review:
        return vbox({
                   text("Review Workspace") | bold | center,
                   separator(),
                   text("Phase 8 will provide task approval and revision "
                        "workflow.") |
                       dim | center,
               }) |
               border | flex;

    case ViewType::CreateTask:
        return vbox({
                   text("Create Task Workspace") | bold | center,
                   separator(),
                   text("Phase 8 will provide task authoring form.") | dim |
                       center,
               }) |
               border | flex;
    }
    return text("Unknown view") | border | flex;
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

Component MainScreen::BuildLayout() {
    auto sidebar = CreateNavigationSidebar(_state, [this](ViewType view) {
        Dispatch(State::NavigateToViewAction{view});
    });

    auto statusBar = Components::CreateStatusBar(_state);

    auto taskBoard = Views::CreateTaskBoardView(
        _state, [this](const State::Action& a) { Dispatch(a); },
        _backendClient);

    auto workspaceRenderer = Renderer([this, taskBoard] {
        switch (_state.currentView) {
        case ViewType::Tasks:
            return taskBoard->Render();
        case ViewType::Review:
        case ViewType::CreateTask:
            return RenderWorkspace();
        }
        return text("Unknown view") | border | flex;
    });

    auto middleContainer = Container::Horizontal({
        sidebar,
        taskBoard,
    });

    auto rootContainer = Container::Vertical({
        middleContainer,
        statusBar,
    });

    auto layoutRenderer =
        Renderer(rootContainer, [this, sidebar, workspaceRenderer, statusBar] {
            return vbox({
                RenderHeader(),
                hbox({
                    sidebar->Render(),
                    workspaceRenderer->Render() | flex,
                }) | flex,
                separator(),
                statusBar->Render(),
            });
        });

    return CatchEvent(layoutRenderer, [this](Event event) {
        if (event == Event::Character('q') ||
            event == Event::Character('\x03')) {
            _screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Character('1')) {
            Dispatch(State::NavigateToViewAction{ViewType::Tasks});
            return true;
        }
        if (event == Event::Character('2')) {
            Dispatch(State::NavigateToViewAction{ViewType::Review});
            return true;
        }
        if (event == Event::Character('3')) {
            Dispatch(State::NavigateToViewAction{ViewType::CreateTask});
            return true;
        }
        if (event == Event::Escape) {
            Dispatch(State::CloseModalAction{});
            Dispatch(State::DismissToastAction{});
            return true;
        }
        return false;
    });
}

void MainScreen::Run() {
    StartHealthMonitoring();
    FetchInitialData();
    auto rootComponent = BuildLayout();
    _screen.Loop(rootComponent);
}

} // namespace Aquarium::UI
