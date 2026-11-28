#include "./ReviewView.hpp"

#include "UI/Components/RemarksPane.hpp"
#include "UI/Components/TaskDetailsPane.hpp"
#include "UI/UIHelpers.hpp"

#include <algorithm>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Aquarium::UI::Views {

using namespace ftxui;
using aquarium::api::Task;
using aquarium::api::TaskStatus;

namespace {

std::vector<const Task*> GetReviewTasks(const State::AppState& state) {
    std::vector<const Task*> result;
    result.reserve(state.tasks.size());
    constexpr std::size_t kLinearDedupThreshold = 64;
    std::unordered_set<std::int64_t> seenIds;
    if (state.tasks.size() > kLinearDedupThreshold) {
        seenIds.reserve(state.tasks.size());
    }
    for (const auto& task : state.tasks) {
        if (task.status() != TaskStatus::IN_REVIEW) {
            continue;
        }
        if (state.filter.onlyMyTasks && !state.IsTaskAssignedAsChecker(task)) {
            continue;
        }
        const auto id = task.id();
        if (state.tasks.size() <= kLinearDedupThreshold) {
            if (std::ranges::any_of(result, [id](const auto* existing) {
                    return existing->id() == id;
                })) {
                continue;
            }
        } else if (!seenIds.insert(id).second) {
            continue;
        }
        result.push_back(&task);
    }
    return result;
}

struct ReviewFormState {
    std::string revisionReason;
    std::string reasonError;
};

} // namespace

Component CreateReviewView(const State::AppState& state,
                           std::function<void(const State::Action&)> dispatch,
                           Grpc::BackendClient& client) {
    auto selectTaskAndFetchComments = [dispatch, &client](std::int64_t taskId) {
        dispatch(State::SelectTaskAction{taskId});
        auto commentsRes = client.GetComments(taskId);
        if (commentsRes.HasValue()) {
            dispatch(State::CommentsLoadedAction{
                .taskId = taskId,
                .comments = commentsRes.Value(),
            });
        }
    };

    auto detailsPane = Components::CreateTaskDetailsPane(state);

    auto formState = std::make_shared<ReviewFormState>();

    InputOption reasonOpt;
    reasonOpt.multiline = false;
    auto reasonInput =
        Input(&formState->revisionReason, "Revision reason...", reasonOpt);

    auto btnApprove = Button(
        "✓ Approve & Verify",
        [&state, dispatch, &client] {
            auto task = state.GetSelectedTask();
            if (task == nullptr) {
                return;
            }
            auto taskId = task->id();
            auto res = client.UpdateTaskStatus(taskId, TaskStatus::VERIFIED);
            if (res.HasValue()) {
                dispatch(State::TaskStatusChangedAction{
                    .taskId = taskId,
                    .newStatus = TaskStatus::VERIFIED,
                });
                dispatch(State::ShowToastAction{
                    .message = "Task #" + std::to_string(taskId) +
                               " approved and verified!",
                    .severity = State::ToastSeverity::Success,
                });
            } else {
                dispatch(State::ShowToastAction{
                    .message = "Failed to approve: " + res.Error().message,
                    .severity = State::ToastSeverity::Error,
                });
            }
        },
        ButtonOption::Simple());

    auto btnReject = Button(
        "✗ Reject with Reason",
        [&state, dispatch, &client, formState] {
            auto task = state.GetSelectedTask();
            if (task == nullptr) {
                return;
            }
            if (formState->revisionReason.empty()) {
                formState->reasonError = "Revision reason is mandatory.";
                dispatch(State::ShowToastAction{
                    .message = "Cannot reject: revision reason is required.",
                    .severity = State::ToastSeverity::Warning,
                });
                return;
            }
            formState->reasonError.clear();
            auto taskId = task->id();
            auto res = client.RejectTask(taskId, formState->revisionReason);
            if (res.HasValue()) {
                dispatch(State::TaskRejectedAction{
                    .task = res.Value().task(),
                    .comment = res.Value().comment(),
                });
                dispatch(State::ShowToastAction{
                    .message = "Task #" + std::to_string(taskId) +
                               " returned to ACTIVE with revision remarks.",
                    .severity = State::ToastSeverity::Warning,
                });
                formState->revisionReason.clear();
            } else {
                dispatch(State::ShowToastAction{
                    .message = "Failed to reject: " + res.Error().message,
                    .severity = State::ToastSeverity::Error,
                });
            }
        },
        ButtonOption::Simple());

    // Remarks pane for comment history & discussion
    Components::RemarksCallbacks remarksCallbacks{
        .onAddComment =
            [&state, dispatch, &client](std::int64_t taskId,
                                        const std::string& content) {
                auto res = client.AddComment(
                    taskId, content,
                    aquarium::api::CommentType::COMMENT_GENERAL,
                    state.session.userId > 0
                        ? std::make_optional(state.session.userId)
                        : std::nullopt);
                if (res.HasValue()) {
                    dispatch(State::CommentAddedAction{res.Value()});
                    dispatch(State::ShowToastAction{
                        .message = "Remark posted.",
                        .severity = State::ToastSeverity::Success,
                    });
                } else {
                    dispatch(State::ShowToastAction{
                        .message = res.Error().message,
                        .severity = State::ToastSeverity::Error,
                    });
                }
            },
    };
    auto remarksPane = Components::CreateRemarksPane(state, remarksCallbacks);

    // Actions container
    auto reviewActionsContainer = Container::Vertical({
        btnApprove,
        reasonInput,
        btnReject,
    });

    auto reviewActionsRenderer =
        Renderer(reviewActionsContainer, [btnApprove, reasonInput, btnReject,
                                          formState] {
            Elements rows = {
                text("REVIEW ACTIONS") | bold | color(Color::CyanLight) |
                    center,
                separator(),
                hbox({
                    filler(),
                    btnApprove->Render() | bold | color(Color::GreenLight),
                    filler(),
                }),
                separator(),
                text("Return for Revision:") | bold | color(Color::YellowLight),
                reasonInput->Render() | borderLight,
            };

            if (!formState->reasonError.empty()) {
                rows.push_back(text(formState->reasonError) |
                               color(Color::RedLight) | bold);
            }

            rows.push_back(hbox({
                filler(),
                btnReject->Render() | bold | color(Color::RedLight),
                filler(),
            }));

            return vbox(std::move(rows)) | border;
        });

    // Right column combining Review Actions and RemarksPane
    auto rightContainer = Container::Vertical({
        reviewActionsRenderer,
        remarksPane,
    });

    auto rightRenderer =
        Renderer(rightContainer, [reviewActionsRenderer, remarksPane] {
            return vbox({
                reviewActionsRenderer->Render(),
                separator(),
                remarksPane->Render() | flex,
            });
        });

    // Left Column: Review Task List
    auto btnScopeToggle = CreateTaskScopeToggleButton(state, dispatch);
    auto leftContainer = Container::Vertical({btnScopeToggle});
    auto leftRenderer = Renderer(leftContainer, [&state, btnScopeToggle] {
        auto currentReviewTasks = GetReviewTasks(state);
        Elements taskRows;
        if (currentReviewTasks.empty()) {
            taskRows.push_back(text("No tasks awaiting review.") | dim |
                               center);
        } else {
            taskRows.reserve(currentReviewTasks.size());
            for (const auto* task : currentReviewTasks) {
                bool isSelected = state.selectedTaskId.has_value() &&
                                  *state.selectedTaskId == task->id();

                std::string idStr = "#" + std::to_string(task->id()) + " ";
                auto idElem = text(idStr) | bold;
                auto badgeElem = RenderStatusBadge(task->status());
                auto titleElem = text(" " + task->title()) | flex;

                auto row = hbox({
                    idElem,
                    badgeElem,
                    titleElem,
                });

                if (isSelected) {
                    taskRows.push_back(row | color(Color::YellowLight) | bold |
                                       inverted);
                } else {
                    taskRows.push_back(row);
                }
            }
        }

        auto taskListArea = vbox(std::move(taskRows)) | yframe | flex;

        return vbox({
                   text("IN REVIEW (" +
                        std::to_string(currentReviewTasks.size()) + ")") |
                       bold | color(Color::YellowLight) | center,
                   btnScopeToggle->Render() | center,
                   separator(),
                   taskListArea,
               }) |
               border;
    });

    auto mainColumns = Container::Horizontal({
        leftRenderer,
        detailsPane,
        rightRenderer,
    });

    auto fullLayout =
        Renderer(mainColumns, [leftRenderer, detailsPane, rightRenderer] {
            return hbox({
                leftRenderer->Render() | size(WIDTH, EQUAL, 34),
                separator(),
                detailsPane->Render() | flex,
                separator(),
                rightRenderer->Render() | size(WIDTH, EQUAL, 38),
            });
        });

    return CatchEvent(fullLayout, [&state, dispatch, rightRenderer,
                                   selectTaskAndFetchComments](Event event) {
        // If typing in revision reason or remarks pane, do not intercept
        // hotkeys
        if (rightRenderer->Focused()) {
            return false;
        }

        // Hotkey 'm': toggle between my review tasks (checker) and all tasks
        if (event == Event::Character('m')) {
            dispatch(State::ToggleOnlyMyTasksAction{});
            return true;
        }

        auto currentReviewTasks = GetReviewTasks(state);
        // Navigation: 'j' or Down key: select next review task
        if (event == Event::Character('j') || event == Event::ArrowDown) {
            if (!currentReviewTasks.empty()) {
                int currentIndex = -1;
                for (size_t i = 0; i < currentReviewTasks.size(); ++i) {
                    if (state.selectedTaskId.has_value() &&
                        currentReviewTasks[i]->id() == *state.selectedTaskId) {
                        currentIndex = static_cast<int>(i);
                        break;
                    }
                }
                int nextIndex =
                    std::min(currentIndex + 1,
                             static_cast<int>(currentReviewTasks.size() - 1));
                selectTaskAndFetchComments(currentReviewTasks[nextIndex]->id());
                return true;
            }
        }

        // Navigation: 'k' or Up key: select prev review task
        if (event == Event::Character('k') || event == Event::ArrowUp) {
            if (!currentReviewTasks.empty()) {
                int currentIndex = 0;
                for (size_t i = 0; i < currentReviewTasks.size(); ++i) {
                    if (state.selectedTaskId.has_value() &&
                        currentReviewTasks[i]->id() == *state.selectedTaskId) {
                        currentIndex = static_cast<int>(i);
                        break;
                    }
                }
                int prevIndex = std::max(currentIndex - 1, 0);
                selectTaskAndFetchComments(currentReviewTasks[prevIndex]->id());
                return true;
            }
        }

        return false;
    });
}

} // namespace Aquarium::UI::Views
