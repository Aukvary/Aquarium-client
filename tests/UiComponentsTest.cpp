#include "State/AppState.hpp"
#include "UI/Components/StatusBar.hpp"
#include "UI/NavigationSidebar.hpp"

#include <chrono>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>
#include <string>

namespace Aquarium::UI::Tests {

using namespace ftxui;
using State::AppState;
using State::ConnectionStatus;
using State::ToastMessage;
using State::ToastSeverity;
using State::ViewType;

TEST(UiComponentsTest, StatusBarRendersConnectionAndToast) {
    AppState state;
    state.connectionStatus = ConnectionStatus::Connected;
    state.activeToast = ToastMessage{
        .message = "Task #7 created successfully",
        .severity = ToastSeverity::Success,
        .expiryTime =
            std::chrono::steady_clock::now() + std::chrono::seconds(5),
    };

    auto comp = Components::CreateStatusBar(state);
    auto elem = comp->Render();

    auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(1));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("[HEALTH: OK]"), std::string::npos);
    EXPECT_NE(rendered.find("[OK]"), std::string::npos);
    EXPECT_NE(rendered.find("Task #7 created successfully"), std::string::npos);
    EXPECT_NE(rendered.find("[q:Quit]"), std::string::npos);
}

TEST(UiComponentsTest, StatusBarRendersOfflineState) {
    AppState state;
    state.connectionStatus = ConnectionStatus::Offline;

    auto comp = Components::CreateStatusBar(state);
    auto elem = comp->Render();

    auto screen = Screen::Create(Dimension::Fixed(100), Dimension::Fixed(1));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("[OFFLINE]"), std::string::npos);
}

TEST(UiComponentsTest, NavigationSidebarRendersAndHandlesHotkeys) {
    AppState state;
    state.currentView = ViewType::Tasks;

    ViewType lastNavigated = ViewType::Tasks;
    auto comp = CreateNavigationSidebar(
        state, [&](ViewType view) { lastNavigated = view; });

    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(30), Dimension::Fixed(15));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("NAVIGATION"), std::string::npos);
    EXPECT_NE(rendered.find("[1] Tasks"), std::string::npos);
    EXPECT_NE(rendered.find("[2] Review"), std::string::npos);
    EXPECT_NE(rendered.find("[3] Create"), std::string::npos);

    // Test hotkeys 1, 2, 3
    EXPECT_TRUE(comp->OnEvent(Event::Character('2')));
    EXPECT_EQ(lastNavigated, ViewType::Review);

    EXPECT_TRUE(comp->OnEvent(Event::Character('3')));
    EXPECT_EQ(lastNavigated, ViewType::CreateTask);

    EXPECT_TRUE(comp->OnEvent(Event::Character('1')));
    EXPECT_EQ(lastNavigated, ViewType::Tasks);

    // Unhandled event
    EXPECT_FALSE(comp->OnEvent(Event::Character('x')));
}

} // namespace Aquarium::UI::Tests
