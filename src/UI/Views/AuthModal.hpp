#pragma once

#include "State/AppState.hpp"

#include <ftxui/component/component.hpp>
#include <functional>
#include <string>

namespace Aquarium::UI::Views {

struct AuthModalCallbacks {
    std::function<void(const std::string& server,
                       const std::string& name,
                       const std::string& inviteKey)>
        onSubmit;
    std::function<void()> onCancel;
};

ftxui::Component CreateAuthModal(const State::AppState& state,
                                 AuthModalCallbacks callbacks);

} // namespace Aquarium::UI::Views
