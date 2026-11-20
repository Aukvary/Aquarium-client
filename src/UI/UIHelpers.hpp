#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <TaskStoreService.pb.h>

namespace Aquarium::UI {

struct StatusBadge {
    std::string text;
    ftxui::Color color;
};

inline StatusBadge GetStatusBadge(aquarium::api::TaskStatus status) {
    switch (status) {
    case aquarium::api::TaskStatus::PLAN:
        return {"PLAN", ftxui::Color::GrayDark};
    case aquarium::api::TaskStatus::ACTIVE:
        return {"ACTIVE", ftxui::Color::Cyan};
    case aquarium::api::TaskStatus::IN_REVIEW:
        return {"IN_REVIEW", ftxui::Color::YellowLight};
    case aquarium::api::TaskStatus::COMPLETE:
        return {"COMPLETE", ftxui::Color::GreenLight};
    case aquarium::api::TaskStatus::VERIFIED:
        return {"VERIFIED", ftxui::Color::MagentaLight};
    case aquarium::api::TaskStatus::ARCHIVE:
        return {"ARCHIVE", ftxui::Color::GrayDark};
    default:
        return {"UNSPECIFIED", ftxui::Color::Default};
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
               ftxui::text("WARNING: TERMINAL WINDOW TOO SMALL") |
                   ftxui::bold | ftxui::color(ftxui::Color::RedLight) |
                   ftxui::center,
               ftxui::separator(),
               ftxui::text("Current size: " + std::to_string(currentWidth) +
                           "x" + std::to_string(currentHeight)) |
                   ftxui::center,
               ftxui::text("Minimum required size: " +
                           std::to_string(minWidth) + "x" +
                           std::to_string(minHeight)) |
                   ftxui::bold | ftxui::color(ftxui::Color::YellowLight) |
                   ftxui::center,
               ftxui::text("Please enlarge or maximize your terminal window.") |
                   ftxui::dim | ftxui::center,
               ftxui::filler(),
           }) |
           ftxui::border | ftxui::center;
}

} // namespace Aquarium::UI
