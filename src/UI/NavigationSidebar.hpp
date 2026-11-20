#pragma once

#include "State/AppState.hpp"

#include <ftxui/component/component.hpp>
#include <functional>

namespace Aquarium::UI {

ftxui::Component
CreateNavigationSidebar(const State::AppState& state,
                        std::function<void(State::ViewType)> onNavigate);

} // namespace Aquarium::UI
