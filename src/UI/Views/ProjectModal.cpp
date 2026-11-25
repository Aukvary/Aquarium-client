#include "./ProjectModal.hpp"

#include <algorithm>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Aquarium::UI::Views {

using namespace ftxui;

namespace {

struct ProjectModalState {
    std::vector<ProjectEntryView> projects;
    std::vector<std::string> labels;
    int selectedIndex{0};
    bool confirmDelete{false};
    std::string deleteTargetServer;
    std::int64_t deleteTargetUserId{0};
    std::string deleteTargetUserName;
    int tabIndex{0};
};

std::string FormatProjectLabel(const ProjectEntryView& project) {
    if (!project.userName.empty()) {
        return project.server + "  (User: " + project.userName +
               " [ID: " + std::to_string(project.userId) + "])";
    }
    return project.server + "  (Unauthenticated)";
}

} // namespace

Component CreateProjectModal(const State::AppState& /*state*/,
                             ProjectModalCallbacks callbacks) {
    auto modalState = std::make_shared<ProjectModalState>();

    auto syncState = [modalState, callbacks] {
        if (callbacks.getProjects) {
            modalState->projects = callbacks.getProjects();
        } else {
            modalState->projects.clear();
        }

        modalState->labels.clear();
        modalState->labels.reserve(modalState->projects.size());
        for (const auto& project : modalState->projects) {
            modalState->labels.push_back(FormatProjectLabel(project));
        }

        if (modalState->projects.empty()) {
            modalState->selectedIndex = 0;
        } else {
            modalState->selectedIndex =
                std::clamp(modalState->selectedIndex, 0,
                           static_cast<int>(modalState->projects.size()) - 1);
        }

        if (modalState->confirmDelete) {
            bool targetStillExists = std::ranges::any_of(
                modalState->projects, [modalState](const ProjectEntryView& p) {
                    return p.server == modalState->deleteTargetServer;
                });
            if (!targetStillExists) {
                modalState->confirmDelete = false;
                modalState->deleteTargetServer.clear();
                modalState->deleteTargetUserId = 0;
                modalState->deleteTargetUserName.clear();
            }
        }

        modalState->tabIndex = modalState->confirmDelete ? 1 : 0;
    };

    auto connectSelected = [modalState, callbacks, syncState] {
        if (callbacks.isBusy && callbacks.isBusy()) {
            return;
        }
        syncState();
        if (modalState->projects.empty()) {
            return;
        }
        const auto idx = static_cast<std::size_t>(modalState->selectedIndex);
        if (idx < modalState->projects.size() && callbacks.onSelectProject) {
            callbacks.onSelectProject(modalState->projects[idx].server);
        }
    };

    auto startDeleteSelected = [modalState, callbacks, syncState] {
        if (callbacks.isBusy && callbacks.isBusy()) {
            return;
        }
        syncState();
        if (modalState->projects.empty()) {
            return;
        }
        const auto idx = static_cast<std::size_t>(modalState->selectedIndex);
        if (idx >= modalState->projects.size()) {
            return;
        }
        const auto& target = modalState->projects[idx];
        modalState->confirmDelete = true;
        modalState->deleteTargetServer = target.server;
        modalState->deleteTargetUserId = target.userId;
        modalState->deleteTargetUserName = target.userName;
        modalState->tabIndex = 1;
        if (callbacks.onClearError) {
            callbacks.onClearError();
        }
    };

    MenuOption menuOpt = MenuOption::Vertical();
    menuOpt.entries = &modalState->labels;
    menuOpt.selected = &modalState->selectedIndex;
    menuOpt.on_enter = connectSelected;
    auto projectMenu = Menu(menuOpt);

    auto btnConnect = Button("Connect", connectSelected);
    auto btnAdd = Button("Add Project", [callbacks] {
        if (callbacks.isBusy && callbacks.isBusy()) {
            return;
        }
        if (callbacks.onClearError) {
            callbacks.onClearError();
        }
        if (callbacks.onAddProject) {
            callbacks.onAddProject();
        }
    });
    auto btnDelete = Button("Delete Project", startDeleteSelected);

    auto listButtons = Container::Horizontal({
        btnConnect,
        btnAdd,
        btnDelete,
    });

    auto listTab = Container::Vertical({
        Maybe(projectMenu,
              [modalState] { return !modalState->projects.empty(); }),
        listButtons,
    });

    auto btnConfirmDelete = Button("Confirm Delete", [modalState, callbacks] {
        if (callbacks.isBusy && callbacks.isBusy()) {
            return;
        }
        if (callbacks.onDeleteProject &&
            !modalState->deleteTargetServer.empty()) {
            callbacks.onDeleteProject(modalState->deleteTargetServer, false);
        }
    });

    auto btnForceLocal = Button("Remove Locally", [modalState, callbacks] {
        if (callbacks.isBusy && callbacks.isBusy()) {
            return;
        }
        if (callbacks.onDeleteProject &&
            !modalState->deleteTargetServer.empty()) {
            callbacks.onDeleteProject(modalState->deleteTargetServer, true);
        }
    });

    auto btnCancelDelete = Button("Cancel", [modalState, callbacks] {
        if (callbacks.isBusy && callbacks.isBusy()) {
            return;
        }
        modalState->confirmDelete = false;
        modalState->tabIndex = 0;
        if (callbacks.onClearError) {
            callbacks.onClearError();
        }
    });

    auto hasError = [callbacks] {
        return callbacks.getErrorMessage &&
               !callbacks.getErrorMessage().empty();
    };

    auto confirmTab = Container::Horizontal({
        btnConfirmDelete,
        Maybe(btnForceLocal, hasError),
        btnCancelDelete,
    });

    auto rootTabs =
        Container::Tab({listTab, confirmTab}, &modalState->tabIndex);

    auto renderer = Renderer(rootTabs, [modalState, callbacks, syncState,
                                        projectMenu, btnConnect, btnAdd,
                                        btnDelete, btnConfirmDelete,
                                        btnForceLocal, btnCancelDelete,
                                        hasError] {
        syncState();

        const bool busy = callbacks.isBusy && callbacks.isBusy();
        const std::string errMsg =
            callbacks.getErrorMessage ? callbacks.getErrorMessage() : "";

        if (modalState->confirmDelete) {
            Elements rows = {
                text("DELETE PROJECT") | bold | color(Color::RedLight) | center,
                separator(),
                text("Project: " + modalState->deleteTargetServer) | bold,
            };

            if (modalState->deleteTargetUserId > 0) {
                std::string userInfo =
                    "User '" + modalState->deleteTargetUserName +
                    "' [ID: " + std::to_string(modalState->deleteTargetUserId) +
                    "] will be deleted from the server.";
                rows.push_back(paragraph(userInfo) | color(Color::YellowLight));
            } else {
                rows.push_back(
                    text("No user ID saved; will remove local entry.") | dim);
            }

            if (busy) {
                rows.push_back(separator());
                rows.push_back(text("Deleting user from server...") |
                               color(Color::CyanLight) | bold | center);
            } else if (!errMsg.empty()) {
                rows.push_back(separator());
                rows.push_back(paragraph(errMsg) | color(Color::RedLight) |
                               bold);
            }

            rows.push_back(separator());
            Elements actionButtons = {
                filler(),
                btnConfirmDelete->Render(),
            };
            if (hasError()) {
                actionButtons.push_back(text(" "));
                actionButtons.push_back(btnForceLocal->Render());
            }
            actionButtons.push_back(text(" "));
            actionButtons.push_back(btnCancelDelete->Render());
            actionButtons.push_back(filler());
            rows.push_back(hbox(std::move(actionButtons)));

            return vbox(std::move(rows)) | border | size(WIDTH, EQUAL, 62) |
                   clear_under;
        }

        Elements listRows = {
            text("SELECT PROJECT") | bold | color(Color::CyanLight) | center,
            separator(),
            text("Choose a project from config or add a new one:") | dim,
            separator(),
        };

        if (modalState->projects.empty()) {
            listRows.push_back(text("No saved projects in config.") | dim |
                               center);
            listRows.push_back(
                text("Click 'Add Project' to register and connect.") | dim |
                center);
        } else {
            listRows.push_back(projectMenu->Render() | yframe |
                               size(HEIGHT, LESS_THAN, 10));
        }

        if (!errMsg.empty()) {
            listRows.push_back(separator());
            listRows.push_back(paragraph(errMsg) | color(Color::RedLight) |
                               bold);
        }

        listRows.push_back(separator());
        listRows.push_back(hbox({
            filler(),
            btnConnect->Render(),
            text(" "),
            btnAdd->Render(),
            text(" "),
            btnDelete->Render(),
            filler(),
        }));
        listRows.push_back(
            text("Enter: Connect | a: Add | d: Delete | j/k: Navigate") | dim |
            center);

        return vbox(std::move(listRows)) | border | size(WIDTH, EQUAL, 62) |
               clear_under;
    });

    return CatchEvent(renderer, [modalState, callbacks, syncState, projectMenu,
                                 connectSelected,
                                 startDeleteSelected](Event event) {
        syncState();

        if (callbacks.isBusy && callbacks.isBusy()) {
            return true;
        }

        if (event == Event::Escape) {
            if (modalState->confirmDelete) {
                modalState->confirmDelete = false;
                modalState->tabIndex = 0;
                if (callbacks.onClearError) {
                    callbacks.onClearError();
                }
            }
            return true;
        }

        if (!modalState->confirmDelete) {
            if (event == Event::Character('j') &&
                !modalState->projects.empty()) {
                modalState->selectedIndex =
                    std::min(modalState->selectedIndex + 1,
                             static_cast<int>(modalState->projects.size()) - 1);
                projectMenu->TakeFocus();
                return true;
            }
            if (event == Event::Character('k') &&
                !modalState->projects.empty()) {
                modalState->selectedIndex =
                    std::max(modalState->selectedIndex - 1, 0);
                projectMenu->TakeFocus();
                return true;
            }
            if (event == Event::Character('a')) {
                if (callbacks.onClearError) {
                    callbacks.onClearError();
                }
                if (callbacks.onAddProject) {
                    callbacks.onAddProject();
                }
                return true;
            }
            if (event == Event::Character('d') || event == Event::Delete) {
                startDeleteSelected();
                return true;
            }
        }

        return false;
    });
}

} // namespace Aquarium::UI::Views
