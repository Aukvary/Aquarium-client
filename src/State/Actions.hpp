#pragma once

#include "./AppState.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <TaskStoreService.pb.h>
#include <UserStoreService.pb.h>
#include <variant>
#include <vector>

namespace Aquarium::State {

struct NavigateToViewAction {
    ViewType view{ViewType::Tasks};
};

struct OpenModalAction {
    ModalType modal{ModalType::None};
};

struct CloseModalAction {};

struct SelectTaskAction {
    std::optional<std::int64_t> taskId{std::nullopt};
};

struct SetStatusFilterAction {
    std::optional<aquarium::api::TaskStatus> status{std::nullopt};
};

struct SetSearchQueryAction {
    std::string query;
};

struct ShowToastAction {
    std::string message;
    ToastSeverity severity{ToastSeverity::Info};
    std::chrono::milliseconds duration{std::chrono::milliseconds(5000)};
};

struct DismissToastAction {};

struct SetConnectionStatusAction {
    ConnectionStatus status{ConnectionStatus::Connecting};
};

struct SetSessionAction {
    std::string server;
    std::int64_t userId{0};
    std::string userName;
    std::string token;
};

struct ExitProjectAction {};

struct SetLoadingTasksAction {
    bool loading{false};
};

struct TasksLoadedAction {
    std::vector<aquarium::api::Task> tasks;
};

struct SetLoadingCommentsAction {
    bool loading{false};
};

struct CommentsLoadedAction {
    std::int64_t taskId{0};
    std::vector<aquarium::api::Comment> comments;
};

struct SetLoadingUsersAction {
    bool loading{false};
};

struct UsersLoadedAction {
    std::vector<aquarium::api::User> users;
};

struct TaskCreatedAction {
    aquarium::api::Task task;
};

struct TaskUpdatedAction {
    aquarium::api::Task task;
};

struct TaskStatusChangedAction {
    std::int64_t taskId{0};
    aquarium::api::TaskStatus newStatus{aquarium::api::TaskStatus::UNSPECIFIED};
};

struct TaskDeletedAction {
    std::int64_t taskId{0};
};

struct CommentAddedAction {
    aquarium::api::Comment comment;
};

struct TaskRejectedAction {
    aquarium::api::Task task;
    aquarium::api::Comment comment;
};

using Action = std::variant<NavigateToViewAction,
                            OpenModalAction,
                            CloseModalAction,
                            SelectTaskAction,
                            SetStatusFilterAction,
                            SetSearchQueryAction,
                            ShowToastAction,
                            DismissToastAction,
                            SetConnectionStatusAction,
                            SetSessionAction,
                            ExitProjectAction,
                            SetLoadingTasksAction,
                            TasksLoadedAction,
                            SetLoadingCommentsAction,
                            CommentsLoadedAction,
                            SetLoadingUsersAction,
                            UsersLoadedAction,
                            TaskCreatedAction,
                            TaskUpdatedAction,
                            TaskStatusChangedAction,
                            TaskDeletedAction,
                            CommentAddedAction,
                            TaskRejectedAction>;

} // namespace Aquarium::State
