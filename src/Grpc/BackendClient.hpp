#pragma once

#include "Common/Types.hpp"

#include <chrono>
#include <cstdint>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <TaskStoreService.grpc.pb.h>
#include <UserStoreService.grpc.pb.h>
#include <UtilsService.grpc.pb.h>
#include <vector>

namespace Aquarium::Grpc {

using Common::AppError;
using Common::AppErrorCode;
using Common::Result;

struct GetTasksFilter {
    std::optional<aquarium::api::TaskStatus> status;
    std::optional<std::int64_t> authorId;
    std::optional<std::int64_t> workerId;
    std::int32_t limit{50};
    std::int32_t offset{0};
};

AppError MapGrpcStatus(const grpc::Status& status);

class BackendClient {
private:
    void InitStubs(std::shared_ptr<grpc::ChannelInterface> channel);
    [[nodiscard]] std::shared_ptr<aquarium::api::UtilsService::Stub>
    GetUtilsStub() const;
    [[nodiscard]] std::shared_ptr<aquarium::api::UserStoreService::Stub>
    GetUserStub() const;
    [[nodiscard]] std::shared_ptr<aquarium::api::TaskStoreService::Stub>
    GetTaskStub() const;

    mutable std::mutex _mutex;
    std::string _targetServer;
    std::string _authToken;
    std::shared_ptr<grpc::ChannelInterface> _channel;

    std::shared_ptr<aquarium::api::UtilsService::Stub> _utilsStub;
    std::shared_ptr<aquarium::api::UserStoreService::Stub> _userStub;
    std::shared_ptr<aquarium::api::TaskStoreService::Stub> _taskStub;

public:
    explicit BackendClient(const std::string& targetServer,
                           std::string authToken = "");

    explicit BackendClient(std::shared_ptr<grpc::ChannelInterface> channel,
                           std::string authToken = "");

    virtual ~BackendClient() = default;

    void SetAuthToken(std::string token);
    [[nodiscard]] std::string GetAuthToken() const;
    void SetTargetServer(const std::string& targetServer);
    [[nodiscard]] std::string GetTargetServer() const;

    [[nodiscard]] std::unique_ptr<grpc::ClientContext> CreateContext(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000),
        const std::string& customInviteKey = "") const;

    [[nodiscard]] std::unique_ptr<grpc::ClientContext>
    CreateStreamContext() const;

    [[nodiscard]] Result<aquarium::api::HealthResponse>
    Health(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

    [[nodiscard]] Result<std::vector<aquarium::api::User>> GetAllUsers();
    [[nodiscard]] Result<aquarium::api::User> GetUser(std::int64_t userId);
    [[nodiscard]] Result<aquarium::api::User>
    AddUser(const std::string& name, const std::string& inviteKey);
    [[nodiscard]] Result<aquarium::api::User>
    UpdateUser(std::int64_t userId, const std::string& newName);
    [[nodiscard]] Result<bool> DeleteUser(std::int64_t userId);

    [[nodiscard]] Result<aquarium::api::GetTasksResponse>
    GetTasks(const GetTasksFilter& filter = {});
    [[nodiscard]] Result<aquarium::api::Task> GetTask(std::int64_t taskId);
    [[nodiscard]] Result<aquarium::api::Task>
    CreateTask(const aquarium::api::CreateTaskRequest& request);
    [[nodiscard]] Result<aquarium::api::Task>
    UpdateTask(const aquarium::api::UpdateTaskRequest& request);
    [[nodiscard]] Result<aquarium::api::Task>
    UpdateTaskStatus(std::int64_t taskId, aquarium::api::TaskStatus status);
    [[nodiscard]] Result<aquarium::api::Task>
    AssignWorker(std::int64_t taskId, std::int64_t workerId);
    [[nodiscard]] Result<aquarium::api::Task>
    RemoveWorker(std::int64_t taskId, std::int64_t workerId);
    [[nodiscard]] Result<bool> DeleteTask(std::int64_t taskId);

    [[nodiscard]] Result<std::vector<aquarium::api::Comment>>
    GetComments(std::int64_t taskId);
    [[nodiscard]] Result<aquarium::api::Comment>
    AddComment(std::int64_t taskId,
               const std::string& content,
               aquarium::api::CommentType type =
                   aquarium::api::CommentType::COMMENT_GENERAL,
               std::optional<std::int64_t> authorId = std::nullopt);
    [[nodiscard]] Result<aquarium::api::RejectTaskResponse>
    RejectTask(std::int64_t taskId, const std::string& reason);

    [[nodiscard]] std::unique_ptr<grpc::ClientReader<aquarium::api::TaskEvent>>
    SubscribeEvents(grpc::ClientContext* context);
};

} // namespace Aquarium::Grpc
