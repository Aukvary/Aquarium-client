#pragma once

#include "State/AppState.hpp"

#include <ftxui/component/component.hpp>

namespace Aquarium::UI::Components {

ftxui::Component CreateStatusBar(const State::AppState& state);

} // namespace Aquarium::UI::Components
