#include "BackendClient.hpp"

#include <utility>

namespace Aquarium::Grpc {

AppError MapGrpcStatus(const grpc::Status& status) {
    if (status.ok()) {
        return AppError{
            .code = AppErrorCode::OK,
            .message = "",
            .details = "",
        };
    }

    AppErrorCode code = AppErrorCode::INTERNAL_ERROR;
    std::string message;

    switch (status.error_code()) {
    case grpc::StatusCode::UNAUTHENTICATED:
        code = AppErrorCode::UNAUTHENTICATED;
        message = "Unauthenticated. Invalid or expired token.";
        break;
    case grpc::StatusCode::PERMISSION_DENIED:
        code = AppErrorCode::PERMISSION_DENIED;
        message = "Permission Denied: Operation not permitted.";
        break;
    case grpc::StatusCode::NOT_FOUND:
        code = AppErrorCode::NOT_FOUND;
        message = "Resource not found.";
        break;
    case grpc::StatusCode::UNAVAILABLE:
        code = AppErrorCode::UNAVAILABLE;
        message = "Server unavailable. Check connection.";
        break;
    case grpc::StatusCode::INVALID_ARGUMENT:
        code = AppErrorCode::INVALID_ARGUMENT;
        message = status.error_message().empty() ? "Invalid argument provided."
                                                 : status.error_message();
        break;
    default:
        code = AppErrorCode::INTERNAL_ERROR;
        message = status.error_message().empty() ? "Internal gRPC error."
                                                 : status.error_message();
        break;
    }

    return AppError{
        .code = code,
        .message = std::move(message),
        .details = status.error_details().empty() ? status.error_message()
                                                  : status.error_details(),
    };
}

BackendClient::BackendClient(const std::string& targetServer,
                             std::string authToken)
    : _targetServer(targetServer), _authToken(std::move(authToken)) {
    auto channel =
        grpc::CreateChannel(targetServer, grpc::InsecureChannelCredentials());
    InitStubs(channel);
}

BackendClient::BackendClient(std::shared_ptr<grpc::ChannelInterface> channel,
                             std::string authToken)
    : _targetServer("custom_channel"), _authToken(std::move(authToken)) {
    InitStubs(std::move(channel));
}

void BackendClient::InitStubs(std::shared_ptr<grpc::ChannelInterface> channel) {
    _channel = std::move(channel);
    _utilsStub = aquarium::api::UtilsService::NewStub(_channel);
    _userStub = aquarium::api::UserStoreService::NewStub(_channel);
    _taskStub = aquarium::api::TaskStoreService::NewStub(_channel);
}

void BackendClient::SetAuthToken(std::string token) {
    _authToken = std::move(token);
}

const std::string& BackendClient::GetAuthToken() const noexcept {
    return _authToken;
}

void BackendClient::SetTargetServer(const std::string& targetServer) {
    _targetServer = targetServer;
    auto channel =
        grpc::CreateChannel(_targetServer, grpc::InsecureChannelCredentials());
    InitStubs(channel);
}

const std::string& BackendClient::GetTargetServer() const noexcept {
    return _targetServer;
}

std::unique_ptr<grpc::ClientContext>
BackendClient::CreateContext(std::chrono::milliseconds timeout,
                             const std::string& customInviteKey) const {
    auto context = std::make_unique<grpc::ClientContext>();
    auto deadline = std::chrono::system_clock::now() + timeout;
    context->set_deadline(deadline);

    if (!_authToken.empty()) {
        context->AddMetadata("authorization", "Bearer " + _authToken);
    }
    if (!customInviteKey.empty()) {
        context->AddMetadata("invite-key", "Invite " + customInviteKey);
    }

    return context;
}

Result<aquarium::api::HealthResponse>
BackendClient::Health(std::chrono::milliseconds timeout) {
    auto context = CreateContext(timeout);
    aquarium::api::HealthRequest request;
    aquarium::api::HealthResponse response;

    grpc::Status status = _utilsStub->Health(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response;
}

Result<std::vector<aquarium::api::User>> BackendClient::GetAllUsers() {
    auto context = CreateContext();
    aquarium::api::GetAllUsersRequest request;
    aquarium::api::GetAllUsersResponse response;

    grpc::Status status = _userStub->GetAll(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }

    std::vector<aquarium::api::User> users;
    users.reserve(response.users_size());
    for (int i = 0; i < response.users_size(); ++i) {
        users.push_back(response.users(i));
    }
    return users;
}

Result<aquarium::api::User> BackendClient::GetUser(std::int64_t userId) {
    auto context = CreateContext();
    aquarium::api::GetUserRequest request;
    request.set_id(userId);
    aquarium::api::GetUserResponse response;

    grpc::Status status = _userStub->GetUser(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.user();
}

Result<aquarium::api::User>
BackendClient::AddUser(const std::string& name, const std::string& inviteKey) {
    auto context = CreateContext(std::chrono::milliseconds(5000), inviteKey);
    aquarium::api::AddUserRequest request;
    request.set_name(name);
    request.set_invite_key(inviteKey);
    aquarium::api::AddUserResponse response;

    grpc::Status status = _userStub->AddUser(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.user();
}

Result<aquarium::api::User>
BackendClient::UpdateUser(std::int64_t userId, const std::string& newName) {
    auto context = CreateContext();
    aquarium::api::UpdateUserRequest request;
    request.set_id(userId);
    request.set_newname(newName);
    aquarium::api::UpdateUserResponse response;

    grpc::Status status =
        _userStub->UpdateUser(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.user();
}

Result<bool> BackendClient::DeleteUser(std::int64_t userId) {
    auto context = CreateContext();
    aquarium::api::DeleteUserRequest request;
    request.set_id(userId);
    aquarium::api::DeleteUserResponse response;

    grpc::Status status =
        _userStub->DeleteUser(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.success();
}

Result<aquarium::api::GetTasksResponse>
BackendClient::GetTasks(const GetTasksFilter& filter) {
    auto context = CreateContext();
    aquarium::api::GetTasksRequest request;
    if (filter.status.has_value()) {
        request.set_status(*filter.status);
    }
    if (filter.authorId.has_value()) {
        request.set_authorid(*filter.authorId);
    }
    if (filter.workerId.has_value()) {
        request.set_workerid(*filter.workerId);
    }
    request.set_limit(filter.limit);
    request.set_offset(filter.offset);

    aquarium::api::GetTasksResponse response;
    grpc::Status status =
        _taskStub->GetTasks(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response;
}

Result<aquarium::api::Task> BackendClient::GetTask(std::int64_t taskId) {
    auto context = CreateContext();
    aquarium::api::GetTaskRequest request;
    request.set_id(taskId);
    aquarium::api::GetTaskResponse response;

    grpc::Status status = _taskStub->GetTask(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.task();
}

Result<aquarium::api::Task>
BackendClient::CreateTask(const aquarium::api::CreateTaskRequest& request) {
    auto context = CreateContext();
    aquarium::api::CreateTaskResponse response;

    grpc::Status status =
        _taskStub->CreateTask(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.task();
}

Result<aquarium::api::Task>
BackendClient::UpdateTask(const aquarium::api::UpdateTaskRequest& request) {
    auto context = CreateContext();
    aquarium::api::UpdateTaskResponse response;

    grpc::Status status =
        _taskStub->UpdateTask(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.task();
}

Result<aquarium::api::Task>
BackendClient::UpdateTaskStatus(std::int64_t taskId,
                                aquarium::api::TaskStatus newStatus) {
    auto context = CreateContext();
    aquarium::api::UpdateTaskStatusRequest request;
    request.set_id(taskId);
    request.set_status(newStatus);
    aquarium::api::UpdateTaskStatusResponse response;

    grpc::Status status =
        _taskStub->UpdateTaskStatus(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.task();
}

Result<aquarium::api::Task> BackendClient::AssignWorker(std::int64_t taskId,
                                                        std::int64_t workerId) {
    auto context = CreateContext();
    aquarium::api::AssignWorkerRequest request;
    request.set_id(taskId);
    request.set_workerid(workerId);
    aquarium::api::AssignWorkerResponse response;

    grpc::Status status =
        _taskStub->AssignWorker(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.task();
}

Result<aquarium::api::Task> BackendClient::RemoveWorker(std::int64_t taskId,
                                                        std::int64_t workerId) {
    auto context = CreateContext();
    aquarium::api::RemoveWorkerRequest request;
    request.set_id(taskId);
    request.set_workerid(workerId);
    aquarium::api::RemoveWorkerResponse response;

    grpc::Status status =
        _taskStub->RemoveWorker(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.task();
}

Result<bool> BackendClient::DeleteTask(std::int64_t taskId) {
    auto context = CreateContext();
    aquarium::api::DeleteTaskRequest request;
    request.set_id(taskId);
    aquarium::api::DeleteTaskResponse response;

    grpc::Status status =
        _taskStub->DeleteTask(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.success();
}

Result<std::vector<aquarium::api::Comment>>
BackendClient::GetComments(std::int64_t taskId) {
    auto context = CreateContext();
    aquarium::api::GetCommentsRequest request;
    request.set_taskid(taskId);
    aquarium::api::GetCommentsResponse response;

    grpc::Status status =
        _taskStub->GetComments(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }

    std::vector<aquarium::api::Comment> comments;
    comments.reserve(response.comments_size());
    for (int i = 0; i < response.comments_size(); ++i) {
        comments.push_back(response.comments(i));
    }
    return comments;
}

Result<aquarium::api::Comment>
BackendClient::AddComment(std::int64_t taskId,
                          const std::string& content,
                          aquarium::api::CommentType type,
                          std::optional<std::int64_t> authorId) {
    auto context = CreateContext();
    aquarium::api::AddCommentRequest request;
    request.set_taskid(taskId);
    request.set_content(content);
    request.set_type(type);
    if (authorId.has_value()) {
        request.set_authorid(*authorId);
    }
    aquarium::api::AddCommentResponse response;

    grpc::Status status =
        _taskStub->AddComment(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response.comment();
}

Result<aquarium::api::RejectTaskResponse>
BackendClient::RejectTask(std::int64_t taskId, const std::string& reason) {
    auto context = CreateContext();
    aquarium::api::RejectTaskRequest request;
    request.set_taskid(taskId);
    request.set_reason(reason);
    aquarium::api::RejectTaskResponse response;

    grpc::Status status =
        _taskStub->RejectTask(context.get(), request, &response);
    if (!status.ok()) {
        return MapGrpcStatus(status);
    }
    return response;
}

std::unique_ptr<grpc::ClientReader<aquarium::api::TaskEvent>>
BackendClient::SubscribeEvents(grpc::ClientContext* context) {
    aquarium::api::SubscribeEventsRequest request;
    return _taskStub->SubscribeEvents(context, request);
}

} // namespace Aquarium::Grpc
