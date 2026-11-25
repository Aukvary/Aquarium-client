#pragma once

#include "State/AppState.hpp"

#include <cstdint>
#include <ftxui/component/component.hpp>
#include <functional>
#include <string>
#include <vector>

namespace Aquarium::UI::Views {

struct ProjectEntryView {
    std::string server;
    std::int64_t userId{0};
    std::string userName;
};

struct ProjectModalCallbacks {
    std::function<std::vector<ProjectEntryView>()> getProjects;
    std::function<void(const std::string& server)> onSelectProject;
    std::function<void()> onAddProject;
    std::function<void(const std::string& server, bool forceLocal)>
        onDeleteProject;
    std::function<std::string()> getErrorMessage;
    std::function<bool()> isBusy;
    std::function<void()> onClearError;
};

ftxui::Component CreateProjectModal(const State::AppState& state,
                                    ProjectModalCallbacks callbacks);

} // namespace Aquarium::UI::Views
