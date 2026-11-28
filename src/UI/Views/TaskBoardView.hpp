#pragma once

#include "Common/CoroutineExecutor.hpp"
#include "Grpc/BackendClient.hpp"
#include "State/Actions.hpp"
#include "State/AppState.hpp"

#include <ftxui/component/component.hpp>
#include <functional>

namespace Aquarium::UI::Views {

ftxui::Component
CreateTaskBoardView(const State::AppState& state,
                    const std::function<void(const State::Action&)>& dispatch,
                    Grpc::BackendClient& client,
                    Common::CoroutineExecutor* executor = nullptr);

} // namespace Aquarium::UI::Views
