#include "TaskBoardView.hpp"

#include "Common/TimeUtils.hpp"
#include "UI/Components/RemarksPane.hpp"
#include "UI/Components/TaskDetailsPane.hpp"
#include "UI/UIHelpers.hpp"

#include <algorithm>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Aquarium::UI::Views {

using namespace ftxui;
using aquarium::api::TaskStatus;

namespace {

const std::vector<std::pair<std::string, std::optional<TaskStatus>>> kFilters =
    {
        {"ALL", std::nullopt},
        {"PLAN", TaskStatus::PLAN},
        {"ACTIVE", TaskStatus::ACTIVE},
        {"IN_REVIEW", TaskStatus::IN_REVIEW},
        {"COMPLETE", TaskStatus::COMPLETE},
        {"VERIFIED", TaskStatus::VERIFIED},
        {"ARCHIVE", TaskStatus::ARCHIVE},
};

TaskStatus GetNextStatus(TaskStatus current) {
    switch (current) {
    case TaskStatus::PLAN:
        return TaskStatus::ACTIVE;
    case TaskStatus::ACTIVE:
        return TaskStatus::IN_REVIEW;
    case TaskStatus::IN_REVIEW:
        return TaskStatus::COMPLETE;
    case TaskStatus::COMPLETE:
        return TaskStatus::VERIFIED;
    case TaskStatus::VERIFIED:
        return TaskStatus::ARCHIVE;
    case TaskStatus::ARCHIVE:
    default:
        return TaskStatus::PLAN;
    }
}

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
        // If state modified query externally, keep local query in sync
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
                    std::function<void(const State::Action&)> dispatch,
                    std::shared_ptr<Grpc::BackendClient> client) {
    auto searchQuery = std::make_shared<std::string>(state.filter.searchQuery);

    auto selectTaskAndFetchComments = [dispatch, client](std::int64_t taskId) {
        dispatch(State::SelectTaskAction{taskId});
        if (client) {
            auto commentsRes = client->GetComments(taskId);
            if (commentsRes.HasValue()) {
                dispatch(State::CommentsLoadedAction{
                    .taskId = taskId,
                    .comments = commentsRes.Value(),
                });
            }
        }
    };

    // Filter Buttons
    ftxui::Components filterButtons;
    for (const auto& [name, statusOpt] : kFilters) {
        auto btn = Button(
            name,
            [dispatch, statusOpt = statusOpt] {
                dispatch(State::SetStatusFilterAction{statusOpt});
            },
            ButtonOption::Simple());
        filterButtons.push_back(btn);
    }
    auto filterBar = Container::Horizontal(filterButtons);

    // Search Field
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

    // Center Details Pane Callbacks
    Components::TaskDetailsCallbacks detailsCallbacks{
        .onChangeStatus =
            [&state, dispatch, client](std::int64_t taskId) {
                auto task = state.GetSelectedTask();
                if (task == nullptr || !client) {
                    return;
                }
                auto next = GetNextStatus(task->status());
                auto res = client->UpdateTaskStatus(taskId, next);
                if (res.HasValue()) {
                    dispatch(State::TaskStatusChangedAction{
                        .taskId = taskId,
                        .newStatus = next,
                    });
                    dispatch(State::ShowToastAction{
                        .message = "Task #" + std::to_string(taskId) +
                                   " moved to " + GetStatusBadge(next).text,
                        .severity = State::ToastSeverity::Success,
                    });
                } else {
                    dispatch(State::ShowToastAction{
                        .message = res.Error().message,
                        .severity = State::ToastSeverity::Error,
                    });
                }
            },
        .onAssignWorker =
            [&state, dispatch, client](std::int64_t taskId) {
                if (!client || state.session.userId == 0) {
                    dispatch(State::ShowToastAction{
                        .message = "Cannot assign: unauthenticated user.",
                        .severity = State::ToastSeverity::Warning,
                    });
                    return;
                }
                auto res = client->AssignWorker(taskId, state.session.userId);
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
        .onDeleteTask =
            [dispatch, client](std::int64_t taskId) {
                if (!client) {
                    return;
                }
                auto res = client->DeleteTask(taskId);
                if (res.HasValue()) {
                    dispatch(State::TaskDeletedAction{taskId});
                    dispatch(State::ShowToastAction{
                        .message =
                            "Task #" + std::to_string(taskId) + " deleted.",
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

    // Right Remarks Pane Callbacks
    Components::RemarksCallbacks remarksCallbacks{
        .onAddComment =
            [&state, dispatch, client](std::int64_t taskId,
                                       const std::string& content) {
                if (!client) {
                    return;
                }
                auto res = client->AddComment(
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

    // Left Column Renderer
    ftxui::Components leftChildren;
    leftChildren.push_back(filterBar);
    leftChildren.push_back(searchField);
    auto leftContainer = Container::Vertical(leftChildren);

    auto leftRenderer = Renderer(leftContainer, [&state, filterBar, searchField,
                                                 selectTaskAndFetchComments] {
        // Render Filter Bar
        Elements filterElements;
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
            text("FILTERS:") | bold | color(Color::CyanLight),
            hflow(std::move(filterElements)),
        });

        // Render Task List
        auto filteredTasks = state.GetFilteredTasks();
        Elements taskRows;
        if (filteredTasks.empty()) {
            taskRows.push_back(text("No tasks found.") | dim | center);
        } else {
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
        // Hotkey '/' focuses search field if not currently typing in an input
        if (event == Event::Character('/') && !searchField->Focused() &&
            !remarksPane->Focused()) {
            searchField->TakeFocus();
            return true;
        }

        // When searchField is focused:
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
            // Do not intercept characters (e.g. 'f', 'j', 'k') or backspace
            // while in search
            return false;
        }

        // When remarksPane is focused (typing a comment), do not intercept
        // hotkeys
        if (remarksPane->Focused()) {
            return false;
        }

        // Hotkey 'f': cycle filters
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

        // Navigation: 'j' or Down key: select next task
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

        // Navigation: 'k' or Up key: select prev task
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
