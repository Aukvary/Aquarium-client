#pragma once

#include "Config/ConfigManager.hpp"
#include "Grpc/BackendClient.hpp"
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
    void FetchInitialData();

    [[nodiscard]] ftxui::Component BuildLayout();
    [[nodiscard]] ftxui::Element RenderHeader() const;
    [[nodiscard]] ftxui::Element RenderWorkspace() const;

    std::shared_ptr<Config::ConfigManager> _configManager;
    std::shared_ptr<Grpc::BackendClient> _backendClient;
    std::unique_ptr<Grpc::HealthChecker> _healthChecker;

    State::AppState _state;
    ftxui::ScreenInteractive _screen;
};

} // namespace Aquarium::UI
