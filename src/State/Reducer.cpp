#include "./Reducer.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Aquarium::State {

namespace {

std::string Trim(std::string_view str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return std::string(str.substr(first, (last - first + 1)));
}

std::string ToLowerUtf8(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        auto c = static_cast<unsigned char>(str[i]);
        if (c == 0xD0 && i + 1 < str.size()) {
            auto next = static_cast<unsigned char>(str[i + 1]);
            if (next >= 0x90 && next <= 0x9F) {
                result.push_back(static_cast<char>(0xD0));
                result.push_back(static_cast<char>(next + 0x20));
                ++i;
                continue;
            }
            if (next >= 0xA0 && next <= 0xAF) {
                result.push_back(static_cast<char>(0xD1));
                result.push_back(static_cast<char>(next - 0x20));
                ++i;
                continue;
            }
            if (next == 0x81) {
                result.push_back(static_cast<char>(0xD1));
                result.push_back(static_cast<char>(0x91));
                ++i;
                continue;
            }
        }
        result.push_back(static_cast<char>(std::tolower(c)));
    }
    return result;
}

bool ContainsCaseInsensitive(std::string_view haystack,
                             std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    std::string lowerHaystack = ToLowerUtf8(haystack);
    std::string lowerNeedle = ToLowerUtf8(needle);
    return lowerHaystack.find(lowerNeedle) != std::string::npos;
}

void EnsureValidSelectionForFilter(AppState& state) {
    auto filtered = state.GetFilteredTasks();
    if (state.selectedTaskId.has_value()) {
        for (const auto* t : filtered) {
            if (t->id() == *state.selectedTaskId) {
                return;
            }
        }
    }
    state.selectedTaskId = filtered.empty()
                               ? std::nullopt
                               : std::make_optional(filtered.front()->id());
    state.activeComments.clear();
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

bool AppState::IsTaskAssignedAsWorker(
    const aquarium::api::Task& task) const {
    if (session.userId <= 0) {
        return false;
    }
    for (int i = 0; i < task.workerids_size(); ++i) {
        if (task.workerids(i) == session.userId) {
            return true;
        }
    }
    return false;
}

bool AppState::IsTaskAssignedAsChecker(
    const aquarium::api::Task& task) const {
    return session.userId > 0 && task.has_checkerid() &&
           task.checkerid() == session.userId;
}

std::vector<const aquarium::api::Task*> AppState::GetFilteredTasks() const {
    std::vector<const aquarium::api::Task*> result;
    result.reserve(tasks.size());
    std::unordered_set<std::int64_t> seenIds;

    std::string trimmedQuery = Trim(filter.searchQuery);

    for (const auto& task : tasks) {
        if (!seenIds.insert(task.id()).second) {
            continue;
        }

        if (filter.onlyMyTasks) {
            const bool isMyTask = currentView == ViewType::Review
                                      ? IsTaskAssignedAsChecker(task)
                                      : IsTaskAssignedAsWorker(task);
            if (!isMyTask) {
                continue;
            }
        }

        if (currentView == ViewType::Review) {
            if (task.status() != aquarium::api::TaskStatus::IN_REVIEW) {
                continue;
            }
        } else if (filter.statusFilter.has_value()) {
            if (task.status() != *filter.statusFilter) {
                continue;
            }
        }

        if (!trimmedQuery.empty()) {
            std::string idStr = std::to_string(task.id());
            bool matchTitle =
                ContainsCaseInsensitive(task.title(), trimmedQuery);
            bool matchId = ContainsCaseInsensitive(idStr, trimmedQuery);
            bool matchDesc =
                ContainsCaseInsensitive(task.description(), trimmedQuery);
            if (!matchTitle && !matchId && !matchDesc) {
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
                if (a.view == ViewType::Tasks || a.view == ViewType::Review) {
                    EnsureValidSelectionForFilter(state);
                }
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
            } else if constexpr (std::is_same_v<T, SetOnlyMyTasksAction>) {
                state.filter.onlyMyTasks = a.onlyMyTasks;
                EnsureValidSelectionForFilter(state);
            } else if constexpr (std::is_same_v<T, ToggleOnlyMyTasksAction>) {
                state.filter.onlyMyTasks = !state.filter.onlyMyTasks;
                EnsureValidSelectionForFilter(state);
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
            } else if constexpr (std::is_same_v<T, ExitProjectAction>) {
                state.session = SessionState{};
                state.session.serverHost.clear();
                state.tasks.clear();
                state.selectedTaskId = std::nullopt;
                state.activeComments.clear();
                state.users.clear();
                state.filter = FilterState{};
                state.isLoadingTasks = false;
                state.isLoadingComments = false;
                state.isLoadingUsers = false;
                state.connectionStatus = ConnectionStatus::Offline;
                state.currentView = ViewType::Tasks;
                state.activeModal = ModalType::ProjectModal;
            } else if constexpr (std::is_same_v<T, SetLoadingTasksAction>) {
                state.isLoadingTasks = a.loading;
            } else if constexpr (std::is_same_v<T, TasksLoadedAction>) {
                state.tasks.clear();
                std::unordered_set<std::int64_t> seenIds;
                for (const auto& t : a.tasks) {
                    if (seenIds.insert(t.id()).second) {
                        state.tasks.push_back(t);
                    }
                }
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
                    state.activeComments.clear();
                    std::unordered_set<std::int64_t> seenIds;
                    for (const auto& c : a.comments) {
                        if (seenIds.insert(c.id()).second) {
                            state.activeComments.push_back(c);
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, SetLoadingUsersAction>) {
                state.isLoadingUsers = a.loading;
            } else if constexpr (std::is_same_v<T, UsersLoadedAction>) {
                state.users = a.users;
                state.isLoadingUsers = false;
            } else if constexpr (std::is_same_v<T, TaskCreatedAction>) {
                auto it =
                    std::ranges::find_if(state.tasks, [&a](const auto& t) {
                        return t.id() == a.task.id();
                    });
                if (it != state.tasks.end()) {
                    *it = a.task;
                } else {
                    state.tasks.insert(state.tasks.begin(), a.task);
                }
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
                std::erase_if(state.tasks, [&a](const auto& t) {
                    return t.id() == a.taskId;
                });

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
                    auto it = std::ranges::find_if(
                        state.activeComments, [&a](const auto& c) {
                            return c.id() == a.comment.id();
                        });
                    if (it == state.activeComments.end()) {
                        state.activeComments.push_back(a.comment);
                    } else {
                        *it = a.comment;
                    }
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
                    auto it = std::ranges::find_if(
                        state.activeComments, [&a](const auto& c) {
                            return c.id() == a.comment.id();
                        });
                    if (it == state.activeComments.end()) {
                        state.activeComments.push_back(a.comment);
                    } else {
                        *it = a.comment;
                    }
                }
            }
        },
        action);

    return state;
}

} // namespace Aquarium::State
