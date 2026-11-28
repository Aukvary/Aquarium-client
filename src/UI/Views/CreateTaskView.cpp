#include "./CreateTaskView.hpp"

#include "Common/TimeUtils.hpp"
#include "UI/UIHelpers.hpp"

#include <charconv>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Aquarium::UI::Views {

using namespace ftxui;
using aquarium::api::CreateTaskRequest;
using aquarium::api::TaskStatus;

namespace {

struct CreateTaskFormState {
    std::string title;
    std::string description;
    std::string workers;
    std::string checker;
    std::string dueDate;
    TaskStatus status{TaskStatus::PLAN};
    std::string error;
};

std::string_view TrimAscii(std::string_view sv) noexcept {
    const auto first = sv.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = sv.find_last_not_of(" \t\n\r\f\v");
    return sv.substr(first, last - first + 1);
}

bool ParseInt64Exact(std::string_view sv, std::int64_t& out) noexcept {
    sv = TrimAscii(sv);
    if (sv.empty()) {
        return false;
    }
    const char* begin = sv.data();
    const char* end = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

} // namespace

Component
CreateCreateTaskView(const State::AppState& state,
                     std::function<void(const State::Action&)> dispatch,
                     Grpc::BackendClient& client,
                     Common::CoroutineExecutor* executor) {
    auto form = std::make_shared<CreateTaskFormState>();

    InputOption singleOpt;
    singleOpt.multiline = false;

    auto titleInput = Input(&form->title, "Enter task title...", singleOpt);
    auto descInput =
        Input(&form->description, "Enter detailed description...", singleOpt);
    auto workersInput =
        Input(&form->workers, "e.g. 1, 2 (optional)", singleOpt);
    auto checkerInput = Input(&form->checker, "e.g. 2 (optional)", singleOpt);
    auto dueDateInput =
        Input(&form->dueDate, "YYYY-MM-DD (optional)", singleOpt);

    auto btnToggleStatus = Button("Toggle Status", [form] {
        if (form->status == TaskStatus::PLAN) {
            form->status = TaskStatus::ACTIVE;
        } else {
            form->status = TaskStatus::PLAN;
        }
    });

    auto submitForm = [&state, dispatch, &client, executor,
                       form]() -> Common::DetachedTask {
        if (form->title.empty()) {
            form->error = "Task title cannot be empty.";
            co_return;
        }

        if (state.session.userId <= 0) {
            form->error =
                "Cannot create task: unauthenticated user. Please log in.";
            co_return;
        }

        CreateTaskRequest req;
        req.set_title(form->title);
        req.set_description(form->description);
        req.set_authorid(state.session.userId);
        req.set_status(form->status);

        // Parse workers (comma-separated list of IDs)
        if (!form->workers.empty()) {
            std::string_view remaining(form->workers);
            while (!remaining.empty()) {
                const auto commaPos = remaining.find(',');
                const std::string_view rawToken =
                    commaPos == std::string_view::npos
                        ? remaining
                        : remaining.substr(0, commaPos);
                const std::string_view token = TrimAscii(rawToken);
                if (!token.empty()) {
                    std::int64_t wid = 0;
                    if (!ParseInt64Exact(token, wid)) {
                        form->error = "Invalid worker ID: '";
                        form->error += token;
                        form->error += "'";
                        co_return;
                    }
                    req.add_workerids(wid);
                }
                if (commaPos == std::string_view::npos) {
                    break;
                }
                remaining.remove_prefix(commaPos + 1);
            }
        }

        // Parse checker
        if (!form->checker.empty()) {
            std::int64_t cid = 0;
            if (!ParseInt64Exact(form->checker, cid)) {
                form->error = "Invalid checker ID: '" + form->checker + "'";
                co_return;
            }
            req.set_checkerid(cid);
        }

        // Parse due date
        if (!form->dueDate.empty()) {
            auto tsOpt = Common::ParseDate(form->dueDate);
            if (!tsOpt.has_value()) {
                form->error = "Invalid date format. Expected YYYY-MM-DD.";
                co_return;
            }
            *req.mutable_duedate() = *tsOpt;
        }

        form->error.clear();

        if (!client.HasChannel()) {
            form->error = "Cannot create task: offline or client missing.";
            co_return;
        }

        co_await Common::ResumeBackground(executor);
        if (executor != nullptr && executor->IsStopped()) {
            co_return;
        }
        auto res = client.CreateTask(req);
        co_await Common::ResumeUi(executor);
        if (executor != nullptr && executor->IsStopped()) {
            co_return;
        }
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
            form->error = res.Error().message;
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
                                             btnSubmit, btnCancel, form] {
        // Build available users summary
        std::string usersSummary = "Available Team Members: ";
        if (state.users.empty()) {
            usersSummary += "None";
        } else {
            usersSummary.reserve(usersSummary.size() + state.users.size() * 16);
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
                RenderStatusBadge(form->status),
                text("  "),
                btnToggleStatus->Render(),
            }),
            separator(),
            hbox({
                text("Due Date:    ") | bold,
                dueDateInput->Render() | flex,
            }),
        };

        if (!form->error.empty()) {
            rows.push_back(separator());
            rows.push_back(text(form->error) | color(Color::RedLight) | bold |
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
