#pragma once

#include "State/AppState.hpp"

#include <cstdint>
#include <ftxui/component/component.hpp>
#include <functional>

namespace Aquarium::UI::Components {

struct TaskDetailsCallbacks {
    std::function<void(std::int64_t taskId)> onPassTask;
    std::function<void(std::int64_t taskId)> onToggleArchive;
    std::function<void(std::int64_t taskId)> onAssignWorker;
    std::function<void(std::int64_t taskId)> onDeleteTask;
};

ftxui::Component CreateTaskDetailsPane(const State::AppState& state,
                                       TaskDetailsCallbacks callbacks = {});

} // namespace Aquarium::UI::Components
