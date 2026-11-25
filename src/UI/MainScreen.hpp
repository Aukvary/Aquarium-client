#pragma once

#include "Config/ConfigManager.hpp"
#include "Grpc/BackendClient.hpp"
#include "Grpc/EventSubscriber.hpp"
#include "Grpc/HealthChecker.hpp"
#include "State/Actions.hpp"
#include "State/AppState.hpp"

#include <atomic>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <thread>

namespace Aquarium::UI {

class MainScreen {
public:
    MainScreen(std::shared_ptr<Config::ConfigManager> configManager,
               std::shared_ptr<Grpc::BackendClient> backendClient);
    ~MainScreen();

    MainScreen(const MainScreen&) = delete;
    MainScreen& operator=(const MainScreen&) = delete;

    void Run();
    void Dispatch(const State::Action& action);

    [[nodiscard]] const State::AppState& GetState() const noexcept;
    [[nodiscard]] ftxui::ScreenInteractive& GetScreen() noexcept;

private:
    void InitState();
    void StartHealthMonitoring();
    void StartEventSubscription();
    void HandleTaskEvent(const aquarium::api::TaskEvent& event);
    void FetchInitialData();
    void ExitToProjectSelector();
    void HandleSelectProject(const std::string& server);
    void HandleDeleteProject(const std::string& server, bool forceLocal);
    void HandleAuthSubmit(const std::string& server,
                          const std::string& name,
                          const std::string& inviteKey);

    [[nodiscard]] ftxui::Component BuildLayout();
    [[nodiscard]] ftxui::Element RenderHeader() const;

    std::shared_ptr<Config::ConfigManager> _configManager;
    std::shared_ptr<Grpc::BackendClient> _backendClient;
    std::unique_ptr<Grpc::HealthChecker> _healthChecker;
    std::unique_ptr<Grpc::EventSubscriber> _eventSubscriber;
    std::jthread _actionThread;

    State::AppState _state;
    ftxui::ScreenInteractive _screen;
    bool _showAuthModal{false};
    bool _showProjectModal{false};
    int _activeTabIndex{0};
    std::atomic<std::uint64_t> _sessionGeneration{0};
    std::atomic<bool> _isDeletingProject{false};
    std::string _projectModalError;
};

} // namespace Aquarium::UI
