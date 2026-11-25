#include "./CreateTaskView.hpp"

#include "Common/TimeUtils.hpp"
#include "UI/UIHelpers.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Aquarium::UI::Views {

using namespace ftxui;
using aquarium::api::CreateTaskRequest;
using aquarium::api::TaskStatus;

Component
CreateCreateTaskView(const State::AppState& state,
                     std::function<void(const State::Action&)> dispatch,
                     std::shared_ptr<Grpc::BackendClient> client) {
    auto titleModel = std::make_shared<std::string>();
    auto descModel = std::make_shared<std::string>();
    auto workersModel = std::make_shared<std::string>();
    auto checkerModel = std::make_shared<std::string>();
    auto dueDateModel = std::make_shared<std::string>();
    auto statusModel = std::make_shared<TaskStatus>(TaskStatus::PLAN);
    auto errorModel = std::make_shared<std::string>();

    InputOption singleOpt;
    singleOpt.multiline = false;

    auto titleInput = Input(titleModel.get(), "Enter task title...", singleOpt);
    auto descInput =
        Input(descModel.get(), "Enter detailed description...", singleOpt);
    auto workersInput =
        Input(workersModel.get(), "e.g. 1, 2 (optional)", singleOpt);
    auto checkerInput =
        Input(checkerModel.get(), "e.g. 2 (optional)", singleOpt);
    auto dueDateInput =
        Input(dueDateModel.get(), "YYYY-MM-DD (optional)", singleOpt);

    auto btnToggleStatus = Button("Toggle Status", [statusModel] {
        if (*statusModel == TaskStatus::PLAN) {
            *statusModel = TaskStatus::ACTIVE;
        } else {
            *statusModel = TaskStatus::PLAN;
        }
    });

    auto submitForm = [&state, dispatch, client, titleModel, descModel,
                       workersModel, checkerModel, dueDateModel, statusModel,
                       errorModel] {
        if (titleModel->empty()) {
            *errorModel = "Task title cannot be empty.";
            return;
        }

        if (state.session.userId <= 0) {
            *errorModel =
                "Cannot create task: unauthenticated user. Please log in.";
            return;
        }

        CreateTaskRequest req;
        req.set_title(*titleModel);
        req.set_description(*descModel);
        req.set_authorid(state.session.userId);
        req.set_status(*statusModel);

        // Parse workers (comma-separated list of IDs)
        if (!workersModel->empty()) {
            std::istringstream ss(*workersModel);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token.erase(
                    0, token.find_first_not_of(" \t\n\r\f\v")); // trim left
                auto last = token.find_last_not_of(" \t\n\r\f\v");
                if (last != std::string::npos) {
                    token.erase(last + 1); // trim right
                }
                if (!token.empty()) {
                    try {
                        auto wid = std::stoll(token);
                        req.add_workerids(wid);
                    } catch (...) {
                        *errorModel = "Invalid worker ID: '" + token + "'";
                        return;
                    }
                }
            }
        }

        // Parse checker
        if (!checkerModel->empty()) {
            try {
                auto cid = std::stoll(*checkerModel);
                req.set_checkerid(cid);
            } catch (...) {
                *errorModel = "Invalid checker ID: '" + *checkerModel + "'";
                return;
            }
        }

        // Parse due date
        if (!dueDateModel->empty()) {
            auto tsOpt = Common::ParseDate(*dueDateModel);
            if (!tsOpt.has_value()) {
                *errorModel = "Invalid date format. Expected YYYY-MM-DD.";
                return;
            }
            *req.mutable_duedate() = *tsOpt;
        }

        errorModel->clear();

        if (!client) {
            *errorModel = "Cannot create task: offline or client missing.";
            return;
        }

        auto res = client->CreateTask(req);
        if (res.HasValue()) {
            auto created = res.Value();
            dispatch(State::TaskCreatedAction{created});
            dispatch(State::SelectTaskAction{created.id()});
            dispatch(State::ShowToastAction{
                .message = "Task #" + std::to_string(created.id()) +
                           " created successfully!",
                .severity = State::ToastSeverity::Success,
            });
            dispatch(State::NavigateToViewAction{State::ViewType::Tasks});
        } else {
            *errorModel = res.Error().message;
            dispatch(State::ShowToastAction{
                .message = "Task creation failed: " + res.Error().message,
                .severity = State::ToastSeverity::Error,
            });
        }
    };

    auto btnSubmit = Button("Create Task", submitForm);
    auto btnCancel = Button("Cancel", [dispatch] {
        dispatch(State::NavigateToViewAction{State::ViewType::Tasks});
    });

    auto formContainer = Container::Vertical({
        titleInput,
        descInput,
        workersInput,
        checkerInput,
        btnToggleStatus,
        dueDateInput,
        Container::Horizontal({btnSubmit, btnCancel}),
    });

    auto renderer = Renderer(formContainer, [&state, titleInput, descInput,
                                             workersInput, checkerInput,
                                             btnToggleStatus, dueDateInput,
                                             btnSubmit, btnCancel, statusModel,
                                             errorModel] {
        // Build available users summary
        std::string usersSummary = "Available Team Members: ";
        if (state.users.empty()) {
            usersSummary += "None";
        } else {
            for (size_t i = 0; i < state.users.size(); ++i) {
                if (i > 0) {
                    usersSummary += ", ";
                }
                usersSummary += "[" + std::to_string(state.users[i].id()) +
                                "] " + state.users[i].name();
            }
        }

        Elements rows = {
            text("CREATE NEW TASK") | bold | color(Color::CyanLight) | center,
            separator(),
            hbox({
                text("Title:       ") | bold,
                titleInput->Render() | flex,
            }),
            separator(),
            hbox({
                text("Description: ") | bold,
                descInput->Render() | flex,
            }),
            separator(),
            hbox({
                text("Worker IDs:  ") | bold,
                workersInput->Render() | flex,
            }),
            text(usersSummary) | dim,
            separator(),
            hbox({
                text("Checker ID:  ") | bold,
                checkerInput->Render() | flex,
            }),
            separator(),
            hbox({
                text("Status:      ") | bold,
                RenderStatusBadge(*statusModel),
                text("  "),
                btnToggleStatus->Render(),
            }),
            separator(),
            hbox({
                text("Due Date:    ") | bold,
                dueDateInput->Render() | flex,
            }),
        };

        if (!errorModel->empty()) {
            rows.push_back(separator());
            rows.push_back(text(*errorModel) | color(Color::RedLight) | bold |
                           center);
        }

        rows.push_back(separator());
        rows.push_back(hbox({
            filler(),
            btnSubmit->Render() | bold | color(Color::GreenLight),
            text("   "),
            btnCancel->Render(),
            filler(),
        }));

        return vbox(std::move(rows)) | border | size(WIDTH, EQUAL, 72) | center;
    });

    return CatchEvent(renderer, [dispatch, submitForm](Event event) {
        if (event == Event::Escape) {
            dispatch(State::NavigateToViewAction{State::ViewType::Tasks});
            return true;
        }
        return false;
    });
}

} // namespace Aquarium::UI::Views
