#pragma once

#include "Grpc/BackendClient.hpp"
#include "State/Actions.hpp"
#include "State/AppState.hpp"

#include <ftxui/component/component.hpp>
#include <functional>

namespace Aquarium::UI::Views {

ftxui::Component
CreateTaskBoardView(const State::AppState& state,
                    std::function<void(const State::Action&)> dispatch,
                    Grpc::BackendClient& client);

} // namespace Aquarium::UI::Views
