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

} // namespace Aquarium::UI
