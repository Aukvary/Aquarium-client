#pragma once

#include "State/AppState.hpp"

#include <cstdint>
#include <ftxui/component/component.hpp>
#include <functional>
#include <string>

namespace Aquarium::UI::Components {

struct RemarksCallbacks {
    std::function<void(std::int64_t taskId, const std::string& content)>
        onAddComment;
};

ftxui::Component CreateRemarksPane(const State::AppState& state,
                                   RemarksCallbacks callbacks = {});

} // namespace Aquarium::UI::Components
