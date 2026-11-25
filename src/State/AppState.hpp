#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <TaskStoreService.pb.h>
#include <UserStoreService.pb.h>
#include <vector>

namespace Aquarium::State {

enum class ViewType : std::uint8_t {
    Tasks = 0,
    Review,
    CreateTask,
};

enum class ModalType : std::uint8_t {
    None = 0,
    AuthModal,
    ProjectModal,
    RejectTaskModal,
    ConfirmDialog,
};

enum class ConnectionStatus : std::uint8_t {
    Connecting = 0,
    Connected,
    Reconnecting,
    Offline,
};

enum class ToastSeverity : std::uint8_t {
    Info = 0,
    Success,
    Warning,
    Error,
};

struct ToastMessage {
    std::string message;
    ToastSeverity severity{ToastSeverity::Info};
    std::chrono::steady_clock::time_point expiryTime;

    [[nodiscard]] bool IsExpired() const {
        return std::chrono::steady_clock::now() >= expiryTime;
    }
};

struct SessionState {
    std::string serverHost{"localhost:50051"};
    std::int64_t userId{0};
    std::string userName;
    std::string authToken;

    [[nodiscard]] bool IsAuthenticated() const noexcept {
        return !authToken.empty();
    }
};

struct FilterState {
    std::optional<aquarium::api::TaskStatus> statusFilter{std::nullopt};
    std::string searchQuery;
};

struct AppState {
    SessionState session;
    ViewType currentView{ViewType::Tasks};
    ModalType activeModal{ModalType::None};
    ConnectionStatus connectionStatus{ConnectionStatus::Connecting};
    std::optional<ToastMessage> activeToast{std::nullopt};
    FilterState filter;

    std::vector<aquarium::api::Task> tasks;
    std::optional<std::int64_t> selectedTaskId{std::nullopt};
    std::vector<aquarium::api::Comment> activeComments;
    std::vector<aquarium::api::User> users;

    bool isLoadingTasks{false};
    bool isLoadingComments{false};
    bool isLoadingUsers{false};

    [[nodiscard]] const aquarium::api::Task* GetSelectedTask() const;
    [[nodiscard]] const aquarium::api::User* FindUser(std::int64_t id) const;
    [[nodiscard]] std::vector<const aquarium::api::Task*>
    GetFilteredTasks() const;
};

} // namespace Aquarium::State
