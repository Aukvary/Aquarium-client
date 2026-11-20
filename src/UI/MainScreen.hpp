#pragma once

#include "Config/ConfigManager.hpp"
#include "Grpc/BackendClient.hpp"
#include "Grpc/EventSubscriber.hpp"
#include "Grpc/HealthChecker.hpp"
#include "State/Actions.hpp"
#include "State/AppState.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>

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
    void HandleAuthSubmit(const std::string& server,
                          const std::string& name,
                          const std::string& inviteKey);

    [[nodiscard]] ftxui::Component BuildLayout();
    [[nodiscard]] ftxui::Element RenderHeader() const;

    std::shared_ptr<Config::ConfigManager> _configManager;
    std::shared_ptr<Grpc::BackendClient> _backendClient;
    std::unique_ptr<Grpc::HealthChecker> _healthChecker;
    std::unique_ptr<Grpc::EventSubscriber> _eventSubscriber;

    State::AppState _state;
    ftxui::ScreenInteractive _screen;
    bool _showAuthModal{false};
    int _activeTabIndex{0};
};

} // namespace Aquarium::UI
