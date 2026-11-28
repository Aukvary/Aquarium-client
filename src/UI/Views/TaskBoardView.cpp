#include "./TaskBoardView.hpp"

#include "UI/Components/RemarksPane.hpp"
#include "UI/Components/TaskDetailsPane.hpp"
#include "UI/UIHelpers.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <Common/TimeUtils.hpp>
#include <cstdint>
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
using aquarium::api::TaskStatus;

namespace {

constexpr std::array<std::pair<std::string_view, std::optional<TaskStatus>>, 7>
    kFilters = {{
        {"ALL", std::nullopt},
        {"PLAN", TaskStatus::PLAN},
        {"ACTIVE", TaskStatus::ACTIVE},
        {"IN_REVIEW", TaskStatus::IN_REVIEW},
        {"COMPLETE", TaskStatus::COMPLETE},
        {"VERIFIED", TaskStatus::VERIFIED},
        {"ARCHIVE", TaskStatus::ARCHIVE},
    }};

class SearchInputWrapper : public ComponentBase {
public:
    SearchInputWrapper(Component child,
                       std::shared_ptr<std::string> query,
                       const State::AppState& state,
                       std::function<void(const State::Action&)> dispatch)
        : _query(std::move(query)), _state(state),
          _dispatch(std::move(dispatch)), _lastQuery(*_query) {
        Add(std::move(child));
    }

    bool OnEvent(Event event) override {
        bool handled = ComponentBase::OnEvent(event);
        SyncQuery();
        return handled;
    }

    Element Render() override {
        if (_lastQuery == *_query && *_query != _state.filter.searchQuery) {
            *_query = _state.filter.searchQuery;
            _lastQuery = *_query;
        } else {
            SyncQuery();
        }
        return ComponentBase::Render();
    }

private:
    void SyncQuery() {
        if (*_query != _lastQuery) {
            _lastQuery = *_query;
            _dispatch(State::SetSearchQueryAction{_lastQuery});
        }
    }

    std::shared_ptr<std::string> _query;
    const State::AppState& _state;
    std::function<void(const State::Action&)> _dispatch;
    std::string _lastQuery;
};

} // namespace

Component
CreateTaskBoardView(const State::AppState& state,
                    const std::function<void(const State::Action&)>& dispatch,
                    Grpc::BackendClient& client,
                    Common::CoroutineExecutor* executor) {
    auto searchQuery = std::make_shared<std::string>(state.filter.searchQuery);
    auto commentFetchSeq = std::make_shared<std::atomic<std::uint64_t>>(0);

    auto selectTaskAndFetchComments =
        [dispatch, &client, executor,
         commentFetchSeq](std::int64_t taskId) -> Common::DetachedTask {
        dispatch(State::SelectTaskAction{taskId});
        if (!client.HasChannel()) {
            co_return;
        }
        const auto seq =
            commentFetchSeq->fetch_add(1, std::memory_order_acq_rel) + 1;
        dispatch(State::SetLoadingCommentsAction{true});
        co_await Common::ResumeBackground(executor);
        if ((executor != nullptr && executor->IsStopped()) ||
            seq != commentFetchSeq->load(std::memory_order_acquire)) {
            co_return;
        }
        auto commentsRes = client.GetComments(taskId);
        if ((executor != nullptr && executor->IsStopped()) ||
            seq != commentFetchSeq->load(std::memory_order_acquire)) {
            co_return;
        }
        if (commentsRes.HasValue()) {
            dispatch(State::CommentsLoadedAction{
                .taskId = taskId,
                .comments = std::move(commentsRes.Value()),
            });
        } else {
            dispatch(State::SetLoadingCommentsAction{false});
        }
    };

    auto btnScopeToggle = CreateTaskScopeToggleButton(state, dispatch);
    ftxui::Components filterButtons;
    filterButtons.reserve(kFilters.size());
    for (const auto& [name, statusOpt] : kFilters) {
        auto btn = Button(
            std::string(name),
            [dispatch, statusOpt = statusOpt] {
                dispatch(State::SetStatusFilterAction{statusOpt});
            },
            ButtonOption::Simple());
        filterButtons.push_back(std::move(btn));
    }
    auto filterBar = Container::Horizontal(std::move(filterButtons));

    InputOption searchOpt;
    searchOpt.multiline = false;
    searchOpt.on_change = [dispatch, searchQuery] {
        dispatch(State::SetSearchQueryAction{*searchQuery});
    };
    searchOpt.on_enter = [filterBar] {
        if (filterBar) {
            filterBar->TakeFocus();
        }
    };
    auto rawSearchField = Input(searchQuery.get(), "Search (/)...", searchOpt);
    auto searchField =
        Make<SearchInputWrapper>(rawSearchField, searchQuery, state, dispatch);

    auto updateTaskStatus =
        [dispatch, &client,
         executor](std::int64_t taskId,
                   TaskStatus targetStatus) -> Common::DetachedTask {
        co_await Common::ResumeBackground(executor);
        if (executor != nullptr && executor->IsStopped()) {
            co_return;
        }
        auto res = client.UpdateTaskStatus(taskId, targetStatus);
        if (executor != nullptr && executor->IsStopped()) {
            co_return;
        }
        if (res.HasValue()) {
            const auto updatedStatus = res.Value().status();
            dispatch(State::TaskUpdatedAction{res.Value()});
            std::string toastMsg = "Task #" + std::to_string(taskId);
            toastMsg += " moved to ";
            toastMsg += GetStatusBadge(updatedStatus).text;
            dispatch(State::ShowToastAction{
                .message = std::move(toastMsg),
                .severity = State::ToastSeverity::Success,
            });
        } else {
            dispatch(State::ShowToastAction{
                .message = res.Error().message,
                .severity = State::ToastSeverity::Error,
            });
        }
    };

    Components::TaskDetailsCallbacks detailsCallbacks{
        .onPassTask =
            [updateTaskStatus](std::int64_t taskId) {
                updateTaskStatus(taskId, TaskStatus::COMPLETE);
            },
        .onToggleArchive =
            [&state, updateTaskStatus](std::int64_t taskId) {
                auto task = state.GetSelectedTask();
                if (task == nullptr) {
                    return;
                }
                const auto targetStatus = task->status() == TaskStatus::ARCHIVE
                                              ? TaskStatus::ACTIVE
                                              : TaskStatus::ARCHIVE;
                updateTaskStatus(taskId, targetStatus);
            },
        .onAssignWorker = [&state, dispatch, &client, executor](
                              std::int64_t taskId) -> Common::DetachedTask {
            const auto userId = state.session.userId;
            if (userId == 0) {
                dispatch(State::ShowToastAction{
                    .message = "Cannot assign: unauthenticated user.",
                    .severity = State::ToastSeverity::Warning,
                });
                co_return;
            }
            co_await Common::ResumeBackground(executor);
            if (executor != nullptr && executor->IsStopped()) {
                co_return;
            }
            auto res = client.AssignWorker(taskId, userId);
            if (executor != nullptr && executor->IsStopped()) {
                co_return;
            }
            if (res.HasValue()) {
                dispatch(State::TaskUpdatedAction{res.Value()});
                dispatch(State::ShowToastAction{
                    .message =
                        "Assigned you to task #" + std::to_string(taskId),
                    .severity = State::ToastSeverity::Success,
                });
            } else {
                dispatch(State::ShowToastAction{
                    .message = res.Error().message,
                    .severity = State::ToastSeverity::Error,
                });
            }
        },
        .onDeleteTask = [dispatch, &client, executor](
                            std::int64_t taskId) -> Common::DetachedTask {
            co_await Common::ResumeBackground(executor);
            if (executor != nullptr && executor->IsStopped()) {
                co_return;
            }
            auto res = client.DeleteTask(taskId);
            if (executor != nullptr && executor->IsStopped()) {
                co_return;
            }
            if (res.HasValue()) {
                dispatch(State::TaskDeletedAction{taskId});
                dispatch(State::ShowToastAction{
                    .message = "Task #" + std::to_string(taskId) + " deleted.",
                    .severity = State::ToastSeverity::Info,
                });
            } else {
                dispatch(State::ShowToastAction{
                    .message = res.Error().message,
                    .severity = State::ToastSeverity::Error,
                });
            }
        },
    };
    auto detailsPane =
        Components::CreateTaskDetailsPane(state, detailsCallbacks);

    Components::RemarksCallbacks remarksCallbacks{
        .onAddComment = [&state, dispatch, &client, executor](
                            std::int64_t taskId,
                            std::string content) -> Common::DetachedTask {
            const auto authorId = state.session.userId > 0
                                      ? std::make_optional(state.session.userId)
                                      : std::nullopt;
            co_await Common::ResumeBackground(executor);
            if (executor != nullptr && executor->IsStopped()) {
                co_return;
            }
            auto res = client.AddComment(
                taskId, content, aquarium::api::CommentType::COMMENT_GENERAL,
                authorId);
            if (executor != nullptr && executor->IsStopped()) {
                co_return;
            }
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

    auto leftContainer =
        Container::Vertical({btnScopeToggle, filterBar, searchField});

    auto leftRenderer =
        Renderer(leftContainer, [&state, btnScopeToggle, filterBar, searchField,
                                 selectTaskAndFetchComments] {
            Elements filterElements;
            filterElements.reserve(kFilters.size());
            for (size_t i = 0; i < kFilters.size(); ++i) {
                const auto& [name, statusOpt] = kFilters[i];
                bool isActive = state.filter.statusFilter == statusOpt;
                auto btnElem = filterBar->ChildAt(i)->Render();
                if (isActive) {
                    filterElements.push_back(btnElem | color(Color::CyanLight) |
                                             bold | inverted);
                } else {
                    filterElements.push_back(btnElem);
                }
            }
            auto filtersHeader = vbox({
                hbox({
                    text("FILTERS:") | bold | color(Color::CyanLight),
                    filler(),
                    btnScopeToggle->Render(),
                }),
                hflow(std::move(filterElements)),
            });

            auto filteredTasks = state.GetFilteredTasks();
            Elements taskRows;
            if (filteredTasks.empty()) {
                taskRows.push_back(text("No tasks found.") | dim | center);
            } else {
                taskRows.reserve(filteredTasks.size());
                for (const auto* task : filteredTasks) {
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
                        taskRows.push_back(ftxui::select(
                            row | color(Color::CyanLight) | bold | inverted));
                    } else {
                        taskRows.push_back(row);
                    }
                }
            }

            auto taskListArea = vbox(std::move(taskRows)) | yframe | flex;

            return vbox({
                       filtersHeader,
                       separator(),
                       searchField->Render(),
                       separator(),
                       taskListArea,
                   }) |
                   border;
        });

    auto mainColumns = Container::Horizontal({
        leftRenderer,
        detailsPane,
        remarksPane,
    });

    auto fullLayout =
        Renderer(mainColumns, [leftRenderer, detailsPane, remarksPane] {
            return hbox({
                leftRenderer->Render() | size(WIDTH, EQUAL, 36),
                separator(),
                detailsPane->Render() | flex,
                separator(),
                remarksPane->Render() | size(WIDTH, EQUAL, 36),
            });
        });

    return CatchEvent(fullLayout, [&state, dispatch, searchField, searchQuery,
                                   filterBar, remarksPane,
                                   selectTaskAndFetchComments](Event event) {
        if (event == Event::Character('/') && !searchField->Focused() &&
            !remarksPane->Focused()) {
            searchField->TakeFocus();
            return true;
        }

        if (searchField->Focused()) {
            if (event == Event::Escape) {
                if (!searchQuery->empty()) {
                    searchQuery->clear();
                    dispatch(State::SetSearchQueryAction{""});
                }
                filterBar->TakeFocus();
                return true;
            }
            if (event == Event::Return) {
                filterBar->TakeFocus();
                return true;
            }
            return false;
        }

        if (remarksPane->Focused()) {
            return false;
        }

        if (event == Event::Character('m')) {
            dispatch(State::ToggleOnlyMyTasksAction{});
            return true;
        }

        if (event == Event::Character('f')) {
            size_t currentIndex = 0;
            for (size_t i = 0; i < kFilters.size(); ++i) {
                if (state.filter.statusFilter == kFilters[i].second) {
                    currentIndex = i;
                    break;
                }
            }
            size_t nextIndex = (currentIndex + 1) % kFilters.size();
            dispatch(State::SetStatusFilterAction{kFilters[nextIndex].second});
            return true;
        }

        if (event == Event::Character('j') || event == Event::ArrowDown) {
            auto tasks = state.GetFilteredTasks();
            if (!tasks.empty()) {
                int currentIndex = -1;
                for (size_t i = 0; i < tasks.size(); ++i) {
                    if (state.selectedTaskId.has_value() &&
                        tasks[i]->id() == *state.selectedTaskId) {
                        currentIndex = static_cast<int>(i);
                        break;
                    }
                }
                int nextIndex = std::min(currentIndex + 1,
                                         static_cast<int>(tasks.size() - 1));
                selectTaskAndFetchComments(tasks[nextIndex]->id());
                return true;
            }
        }

        if (event == Event::Character('k') || event == Event::ArrowUp) {
            auto tasks = state.GetFilteredTasks();
            if (!tasks.empty()) {
                int currentIndex = 0;
                for (size_t i = 0; i < tasks.size(); ++i) {
                    if (state.selectedTaskId.has_value() &&
                        tasks[i]->id() == *state.selectedTaskId) {
                        currentIndex = static_cast<int>(i);
                        break;
                    }
                }
                int prevIndex = std::max(currentIndex - 1, 0);
                selectTaskAndFetchComments(tasks[prevIndex]->id());
                return true;
            }
        }

        return false;
    });
}

} // namespace Aquarium::UI::Views
