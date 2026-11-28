#pragma once

#include "Common/CoroutineExecutor.hpp"
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
#include <optional>
#include <string>

namespace Aquarium::UI {

class MainScreen {
private:
    [[nodiscard]] ftxui::Component BuildLayout();
    [[nodiscard]] ftxui::Element RenderHeader() const;

    Config::ConfigManager& _configManager;
    Grpc::BackendClient _backendClient;
    std::optional<Grpc::HealthChecker> _healthChecker;
    std::optional<Grpc::EventSubscriber> _eventSubscriber;

    State::AppState _state;
    ftxui::ScreenInteractive _screen;
    bool _showAuthModal{false};
    bool _showProjectModal{false};
    int _activeTabIndex{0};
    std::atomic<std::uint64_t> _sessionGeneration{0};
    std::atomic<bool> _isDeletingProject{false};
    std::string _projectModalError;
    Common::CoroutineExecutor _executor;

    void InitState();
    void StartHealthMonitoring();
    void StartEventSubscription();
    void HandleTaskEvent(const aquarium::api::TaskEvent& event);
    Common::DetachedTask FetchInitialData();
    void ExitToProjectSelector();
    void HandleSelectProject(const std::string& server);
    Common::DetachedTask HandleDeleteProject(std::string server,
                                             bool forceLocal);
    Common::DetachedTask HandleAuthSubmit(std::string server,
                                          std::string name,
                                          std::string inviteKey);

public:
    explicit MainScreen(Config::ConfigManager& configManager);
    ~MainScreen();

    MainScreen(const MainScreen&) = delete;
    MainScreen& operator=(const MainScreen&) = delete;

    void Run();
    void Dispatch(const State::Action& action);

    [[nodiscard]] const State::AppState& GetState() const noexcept;
    [[nodiscard]] ftxui::ScreenInteractive& GetScreen() noexcept;
};

} // namespace Aquarium::UI
