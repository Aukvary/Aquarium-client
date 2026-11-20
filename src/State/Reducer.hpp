#pragma once

#include "Actions.hpp"
#include "AppState.hpp"

namespace Aquarium::State {

class Reducer {
public:
    [[nodiscard]] static AppState Reduce(AppState state, const Action& action);
};

} // namespace Aquarium::State
