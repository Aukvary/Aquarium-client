#pragma once

#include "State/Actions.hpp"
#include "State/AppState.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <functional>
#include <string>
#include <TaskStoreService.pb.h>
#include <utility>

namespace Aquarium::UI {

struct StatusBadge {
    std::string text;
    ftxui::Color color;
};

inline StatusBadge GetStatusBadge(aquarium::api::TaskStatus status) {
    switch (status) {
    case aquarium::api::TaskStatus::PLAN:
        return {.text = "PLAN", .color = ftxui::Color::GrayDark};
    case aquarium::api::TaskStatus::ACTIVE:
        return {.text = "ACTIVE", .color = ftxui::Color::Cyan};
    case aquarium::api::TaskStatus::IN_REVIEW:
        return {.text = "IN_REVIEW", .color = ftxui::Color::YellowLight};
    case aquarium::api::TaskStatus::COMPLETE:
        return {.text = "COMPLETE", .color = ftxui::Color::GreenLight};
    case aquarium::api::TaskStatus::VERIFIED:
        return {.text = "VERIFIED", .color = ftxui::Color::MagentaLight};
    case aquarium::api::TaskStatus::ARCHIVE:
        return {.text = "ARCHIVE", .color = ftxui::Color::GrayDark};
    default:
        return {.text = "UNSPECIFIED", .color = ftxui::Color::Default};
    }
}

inline ftxui::Element RenderStatusBadge(aquarium::api::TaskStatus status) {
    auto badge = GetStatusBadge(status);
    return ftxui::text("[" + badge.text + "]") | ftxui::color(badge.color) |
           ftxui::bold;
}

inline ftxui::Element RenderTerminalTooSmallWarning(int currentWidth,
                                                    int currentHeight,
                                                    int minWidth = 80,
                                                    int minHeight = 24) {
    return ftxui::vbox({
               ftxui::filler(),
               ftxui::text("WARNING: TERMINAL WINDOW TOO SMALL") | ftxui::bold |
                   ftxui::color(ftxui::Color::RedLight) | ftxui::center,
               ftxui::separator(),
               ftxui::text("Current size: " + std::to_string(currentWidth) +
                           "x" + std::to_string(currentHeight)) |
                   ftxui::center,
               ftxui::text(
                   "Minimum required size: " + std::to_string(minWidth) + "x" +
                   std::to_string(minHeight)) |
                   ftxui::bold | ftxui::color(ftxui::Color::YellowLight) |
                   ftxui::center,
               ftxui::text("Please enlarge or maximize your terminal window.") |
                   ftxui::dim | ftxui::center,
               ftxui::filler(),
           }) |
           ftxui::border | ftxui::center;
}

inline ftxui::Component CreateTaskScopeToggleButton(
    const State::AppState& state,
    std::function<void(const State::Action&)> dispatch) {
    ftxui::ButtonOption opt;
    opt.label = "Scope";
    opt.on_click = [dispatch = std::move(dispatch)] {
        dispatch(State::ToggleOnlyMyTasksAction{});
    };
    opt.transform = [&state](const ftxui::EntryState& s) {
        const std::string label = state.filter.onlyMyTasks
                                      ? "[Show: My Tasks]"
                                      : "[Show: All Tasks]";
        auto elem = ftxui::text(label);
        if (state.filter.onlyMyTasks) {
            elem =
                elem | ftxui::color(ftxui::Color::YellowLight) | ftxui::bold;
        } else {
            elem = elem | ftxui::color(ftxui::Color::CyanLight);
        }
        if (s.focused) {
            elem = elem | ftxui::inverted;
        }
        return elem;
    };
    return ftxui::Button(std::move(opt));
}

} // namespace Aquarium::UI
