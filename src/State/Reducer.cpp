#include "Reducer.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace Aquarium::State {

namespace {

bool ContainsCaseInsensitive(const std::string& haystack,
                             const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto it =
        std::search(haystack.begin(), haystack.end(), needle.begin(),
                    needle.end(), [](char ch1, char ch2) {
                        return std::tolower(static_cast<unsigned char>(ch1)) ==
                               std::tolower(static_cast<unsigned char>(ch2));
                    });
    return it != haystack.end();
}

} // namespace

const aquarium::api::Task* AppState::GetSelectedTask() const {
    if (!selectedTaskId.has_value()) {
        return nullptr;
    }
    for (const auto& task : tasks) {
        if (task.id() == *selectedTaskId) {
            return &task;
        }
    }
    return nullptr;
}

const aquarium::api::User* AppState::FindUser(std::int64_t id) const {
    for (const auto& user : users) {
        if (user.id() == id) {
            return &user;
        }
    }
    return nullptr;
}

std::vector<const aquarium::api::Task*> AppState::GetFilteredTasks() const {
    std::vector<const aquarium::api::Task*> result;
    result.reserve(tasks.size());

    for (const auto& task : tasks) {
        if (currentView == ViewType::Review) {
            if (task.status() != aquarium::api::TaskStatus::IN_REVIEW) {
                continue;
            }
        } else if (filter.statusFilter.has_value()) {
            if (task.status() != *filter.statusFilter) {
                continue;
            }
        }

        if (!filter.searchQuery.empty()) {
            std::string idStr = std::to_string(task.id());
            bool matchTitle =
                ContainsCaseInsensitive(task.title(), filter.searchQuery);
            bool matchId = ContainsCaseInsensitive(idStr, filter.searchQuery);
            if (!matchTitle && !matchId) {
                continue;
            }
        }

        result.push_back(&task);
    }

    return result;
}

AppState Reducer::Reduce(AppState state, const Action& action) {
    std::visit(
        [&state](const auto& a) {
            using T = std::decay_t<decltype(a)>;

            if constexpr (std::is_same_v<T, NavigateToViewAction>) {
                state.currentView = a.view;
            } else if constexpr (std::is_same_v<T, OpenModalAction>) {
                state.activeModal = a.modal;
            } else if constexpr (std::is_same_v<T, CloseModalAction>) {
                state.activeModal = ModalType::None;
            } else if constexpr (std::is_same_v<T, SelectTaskAction>) {
                if (state.selectedTaskId != a.taskId) {
                    state.selectedTaskId = a.taskId;
                    state.activeComments.clear();
                }
            } else if constexpr (std::is_same_v<T, SetStatusFilterAction>) {
                state.filter.statusFilter = a.status;
            } else if constexpr (std::is_same_v<T, SetSearchQueryAction>) {
                state.filter.searchQuery = a.query;
            } else if constexpr (std::is_same_v<T, ShowToastAction>) {
                state.activeToast = ToastMessage{
                    .message = a.message,
                    .severity = a.severity,
                    .expiryTime = std::chrono::steady_clock::now() + a.duration,
                };
            } else if constexpr (std::is_same_v<T, DismissToastAction>) {
                state.activeToast = std::nullopt;
            } else if constexpr (std::is_same_v<T, SetConnectionStatusAction>) {
                state.connectionStatus = a.status;
            } else if constexpr (std::is_same_v<T, SetSessionAction>) {
                state.session.serverHost = a.server;
                state.session.userId = a.userId;
                state.session.userName = a.userName;
                state.session.authToken = a.token;
            } else if constexpr (std::is_same_v<T, SetLoadingTasksAction>) {
                state.isLoadingTasks = a.loading;
            } else if constexpr (std::is_same_v<T, TasksLoadedAction>) {
                state.tasks = a.tasks;
                state.isLoadingTasks = false;
                if (state.selectedTaskId.has_value()) {
                    bool found = false;
                    for (const auto& t : state.tasks) {
                        if (t.id() == *state.selectedTaskId) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        state.selectedTaskId =
                            state.tasks.empty()
                                ? std::nullopt
                                : std::make_optional(state.tasks.front().id());
                        state.activeComments.clear();
                    }
                } else if (!state.tasks.empty()) {
                    state.selectedTaskId = state.tasks.front().id();
                }
            } else if constexpr (std::is_same_v<T, SetLoadingCommentsAction>) {
                state.isLoadingComments = a.loading;
            } else if constexpr (std::is_same_v<T, CommentsLoadedAction>) {
                state.isLoadingComments = false;
                if (state.selectedTaskId.has_value() &&
                    *state.selectedTaskId == a.taskId) {
                    state.activeComments = a.comments;
                }
            } else if constexpr (std::is_same_v<T, SetLoadingUsersAction>) {
                state.isLoadingUsers = a.loading;
            } else if constexpr (std::is_same_v<T, UsersLoadedAction>) {
                state.users = a.users;
                state.isLoadingUsers = false;
            } else if constexpr (std::is_same_v<T, TaskCreatedAction>) {
                state.tasks.insert(state.tasks.begin(), a.task);
                if (!state.selectedTaskId.has_value()) {
                    state.selectedTaskId = a.task.id();
                }
            } else if constexpr (std::is_same_v<T, TaskUpdatedAction>) {
                for (auto& t : state.tasks) {
                    if (t.id() == a.task.id()) {
                        t = a.task;
                        break;
                    }
                }
            } else if constexpr (std::is_same_v<T, TaskStatusChangedAction>) {
                for (auto& t : state.tasks) {
                    if (t.id() == a.taskId) {
                        t.set_status(a.newStatus);
                        break;
                    }
                }
            } else if constexpr (std::is_same_v<T, TaskDeletedAction>) {
                auto it = std::remove_if(
                    state.tasks.begin(), state.tasks.end(),
                    [&a](const auto& t) { return t.id() == a.taskId; });
                state.tasks.erase(it, state.tasks.end());

                if (state.selectedTaskId.has_value() &&
                    *state.selectedTaskId == a.taskId) {
                    state.selectedTaskId =
                        state.tasks.empty()
                            ? std::nullopt
                            : std::make_optional(state.tasks.front().id());
                    state.activeComments.clear();
                }
            } else if constexpr (std::is_same_v<T, CommentAddedAction>) {
                if (state.selectedTaskId.has_value() &&
                    *state.selectedTaskId == a.comment.taskid()) {
                    state.activeComments.push_back(a.comment);
                }
            } else if constexpr (std::is_same_v<T, TaskRejectedAction>) {
                for (auto& t : state.tasks) {
                    if (t.id() == a.task.id()) {
                        t = a.task;
                        break;
                    }
                }
                if (state.selectedTaskId.has_value() &&
                    *state.selectedTaskId == a.task.id()) {
                    state.activeComments.push_back(a.comment);
                }
            }
        },
        action);

    return state;
}

} // namespace Aquarium::State
