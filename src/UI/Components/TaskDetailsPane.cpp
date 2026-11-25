#include "TaskDetailsPane.hpp"

#include "Common/TimeUtils.hpp"
#include "UI/UIHelpers.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Aquarium::UI::Components {

using namespace ftxui;

Component CreateTaskDetailsPane(const State::AppState& state,
                                TaskDetailsCallbacks callbacks) {
    auto btnPass = Button("Pass", [&state, callbacks] {
        auto task = state.GetSelectedTask();
        if (task != nullptr && callbacks.onPassTask) {
            callbacks.onPassTask(task->id());
        }
    });

    auto btnToggleArchive = Button("Toggle Archive", [&state, callbacks] {
        auto task = state.GetSelectedTask();
        if (task != nullptr && callbacks.onToggleArchive) {
            callbacks.onToggleArchive(task->id());
        }
    });

    auto btnAssign = Button("Assign Worker", [&state, callbacks] {
        auto task = state.GetSelectedTask();
        if (task != nullptr && callbacks.onAssignWorker) {
            callbacks.onAssignWorker(task->id());
        }
    });

    auto btnDelete = Button("Delete", [&state, callbacks] {
        auto task = state.GetSelectedTask();
        if (task != nullptr && callbacks.onDeleteTask) {
            callbacks.onDeleteTask(task->id());
        }
    });

    auto buttonContainer = Container::Horizontal({
        btnPass,
        btnToggleArchive,
        btnAssign,
        btnDelete,
    });

    return Renderer(buttonContainer, [&state, buttonContainer] {
        auto task = state.GetSelectedTask();
        if (task == nullptr) {
            return vbox({
                       text("TASK DETAILS") | bold | center,
                       separator(),
                       filler(),
                       text("Select a task from the list to view details.") |
                           dim | center,
                       filler(),
                   }) |
                   border;
        }

        // Author name
        auto author = state.FindUser(task->authorid());
        std::string authorName =
            author != nullptr ? author->name()
                              : "User #" + std::to_string(task->authorid());

        // Workers names
        std::string workersStr;
        if (task->workerids_size() == 0) {
            workersStr = "None";
        } else {
            for (int i = 0; i < task->workerids_size(); ++i) {
                if (i > 0) {
                    workersStr += ", ";
                }
                auto worker = state.FindUser(task->workerids(i));
                if (worker != nullptr) {
                    workersStr += worker->name();
                } else {
                    workersStr += "User #" + std::to_string(task->workerids(i));
                }
            }
        }

        // Checker name
        std::string checkerStr = "None";
        if (task->has_checkerid()) {
            auto checker = state.FindUser(task->checkerid());
            checkerStr = checker != nullptr
                             ? checker->name()
                             : "User #" + std::to_string(task->checkerid());
        }

        // Dates
        std::string createdStr = Common::FormatTimestamp(task->createdat());
        std::string dueStr = task->has_duedate()
                                 ? Common::FormatTimestamp(task->duedate())
                                 : "None";

        auto metaGrid = vbox({
            hbox({
                text("Author:    ") | bold | color(Color::CyanLight),
                text(authorName),
            }),
            hbox({
                text("Workers:   ") | bold | color(Color::CyanLight),
                text(workersStr),
            }),
            hbox({
                text("Checker:   ") | bold | color(Color::CyanLight),
                text(checkerStr),
            }),
            hbox({
                text("Created:   ") | bold | color(Color::CyanLight),
                text(createdStr),
            }),
            hbox({
                text("Due Date:  ") | bold | color(Color::CyanLight),
                text(dueStr) | (task->has_duedate() ? color(Color::YellowLight)
                                                    : color(Color::Default)),
            }),
        });

        std::string desc = task->description().empty()
                               ? "(No description provided.)"
                               : task->description();

        return vbox({
                   hbox({
                       text("#" + std::to_string(task->id()) + " ") | bold |
                           color(Color::White),
                       text(task->title()) | bold | flex,
                       RenderStatusBadge(task->status()),
                   }),
                   separator(),
                   metaGrid,
                   separator(),
                   text("DESCRIPTION:") | bold | color(Color::CyanLight),
                   paragraph(desc) | flex,
                   separator(),
                   buttonContainer->Render(),
               }) |
               border;
    });
}

} // namespace Aquarium::UI::Components
