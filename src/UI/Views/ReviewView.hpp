#pragma once

#include "Grpc/BackendClient.hpp"
#include "State/Actions.hpp"
#include "State/AppState.hpp"

#include <ftxui/component/component.hpp>
#include <functional>
#include <memory>

namespace Aquarium::UI::Views {

ftxui::Component
CreateReviewView(const State::AppState& state,
                 std::function<void(const State::Action&)> dispatch,
                 std::shared_ptr<Grpc::BackendClient> client);

} // namespace Aquarium::UI::Views
