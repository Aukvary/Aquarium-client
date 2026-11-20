#include "StatusBar.hpp"

#include <ftxui/dom/elements.hpp>
#include <string>

namespace Aquarium::UI::Components {

using namespace ftxui;
using State::ConnectionStatus;
using State::ToastSeverity;

Element RenderConnectionBadge(ConnectionStatus status) {
    switch (status) {
    case ConnectionStatus::Connected:
        return text("[HEALTH: OK]") | color(Color::GreenLight) | bold;
    case ConnectionStatus::Connecting:
        return text("[CONNECTING...]") | color(Color::YellowLight) | bold;
    case ConnectionStatus::Reconnecting:
        return text("[RECONNECTING...]") | color(Color::YellowLight) | bold;
    case ConnectionStatus::Offline:
        return text("[OFFLINE]") | color(Color::RedLight) | bold;
    }
    return text("[UNKNOWN]") | dim;
}

Element RenderToast(const std::optional<State::ToastMessage>& toast) {
    if (!toast.has_value() || toast->IsExpired()) {
        return text("");
    }

    Color badgeColor = Color::Cyan;
    std::string badgeText = "[INFO] ";

    switch (toast->severity) {
    case ToastSeverity::Info:
        badgeColor = Color::Cyan;
        badgeText = "[INFO] ";
        break;
    case ToastSeverity::Success:
        badgeColor = Color::GreenLight;
        badgeText = "[OK] ";
        break;
    case ToastSeverity::Warning:
        badgeColor = Color::YellowLight;
        badgeText = "[WARN] ";
        break;
    case ToastSeverity::Error:
        badgeColor = Color::RedLight;
        badgeText = "[ERROR] ";
        break;
    }

    return hbox({
        text("  "),
        text(badgeText) | color(badgeColor) | bold,
        text(toast->message) | bold,
    });
}

Component CreateStatusBar(const State::AppState& state) {
    return Renderer([&state] {
        auto connBadge = RenderConnectionBadge(state.connectionStatus);
        auto toastElement = RenderToast(state.activeToast);
        auto cheatsheet =
            text("[1:Tasks 2:Review 3:Create] [/:Search] [q:Quit]") | dim;

        return hbox({
            text(" "),
            connBadge,
            toastElement,
            filler(),
            cheatsheet,
            text(" "),
        });
    });
}

} // namespace Aquarium::UI::Components
