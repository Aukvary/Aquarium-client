#include "./BackendClient.hpp"

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
    std::scoped_lock lock(_mutex);
    _channel = std::move(channel);
    _utilsStub = aquarium::api::UtilsService::NewStub(_channel);
    _userStub = aquarium::api::UserStoreService::NewStub(_channel);
    _taskStub = aquarium::api::TaskStoreService::NewStub(_channel);
}

std::shared_ptr<aquarium::api::UtilsService::Stub>
BackendClient::GetUtilsStub() const {
    std::scoped_lock lock(_mutex);
    return _utilsStub;
}

std::shared_ptr<aquarium::api::UserStoreService::Stub>
BackendClient::GetUserStub() const {
    std::scoped_lock lock(_mutex);
    return _userStub;
}

std::shared_ptr<aquarium::api::TaskStoreService::Stub>
BackendClient::GetTaskStub() const {
    std::scoped_lock lock(_mutex);
    return _taskStub;
}

void BackendClient::SetAuthToken(std::string token) {
    std::scoped_lock lock(_mutex);
    _authToken = std::move(token);
}

std::string BackendClient::GetAuthToken() const {
    std::scoped_lock lock(_mutex);
    return _authToken;
}

void BackendClient::SetTargetServer(const std::string& targetServer) {
    auto channel =
        grpc::CreateChannel(targetServer, grpc::InsecureChannelCredentials());
    std::scoped_lock lock(_mutex);
    _targetServer = targetServer;
    _channel = std::move(channel);
    _utilsStub = aquarium::api::UtilsService::NewStub(_channel);
    _userStub = aquarium::api::UserStoreService::NewStub(_channel);
    _taskStub = aquarium::api::TaskStoreService::NewStub(_channel);
}

std::string BackendClient::GetTargetServer() const {
    std::scoped_lock lock(_mutex);
    return _targetServer;
}

std::unique_ptr<grpc::ClientContext>
BackendClient::CreateContext(std::chrono::milliseconds timeout,
                             const std::string& customInviteKey) const {
    std::string token;
    {
        std::scoped_lock lock(_mutex);
        token = _authToken;
    }

    auto context = std::make_unique<grpc::ClientContext>();
    if (timeout > std::chrono::milliseconds::zero()) {
        auto deadline = std::chrono::system_clock::now() + timeout;
        context->set_deadline(deadline);
    }

    if (!token.empty()) {
        context->AddMetadata("authorization", "Bearer " + token);
    }
    if (!customInviteKey.empty()) {
        context->AddMetadata("invite-key", "Invite " + customInviteKey);
    }

    return context;
}

std::unique_ptr<grpc::ClientContext>
BackendClient::CreateStreamContext() const {
    return CreateContext(std::chrono::milliseconds::zero());
}

Result<aquarium::api::HealthResponse>
BackendClient::Health(std::chrono::milliseconds timeout) {
    auto context = CreateContext(timeout);
    aquarium::api::HealthRequest request;
    aquarium::api::HealthResponse response;

    grpc::Status status =
        GetUtilsStub()->Health(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::HealthResponse>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::HealthResponse>(std::move(response));
}

Result<std::vector<aquarium::api::User>> BackendClient::GetAllUsers() {
    auto context = CreateContext();
    aquarium::api::GetAllUsersRequest request;
    aquarium::api::GetAllUsersResponse response;

    grpc::Status status =
        GetUserStub()->GetAll(context.get(), request, &response);
    if (!status.ok()) {
        return Result<std::vector<aquarium::api::User>>(MapGrpcStatus(status));
    }

    std::vector<aquarium::api::User> users;
    users.reserve(response.users_size());
    for (int i = 0; i < response.users_size(); ++i) {
        users.push_back(response.users(i));
    }
    return Result<std::vector<aquarium::api::User>>(std::move(users));
}

Result<aquarium::api::User> BackendClient::GetUser(std::int64_t userId) {
    auto context = CreateContext();
    aquarium::api::GetUserRequest request;
    request.set_id(userId);
    aquarium::api::GetUserResponse response;

    grpc::Status status =
        GetUserStub()->GetUser(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::User>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::User>(response.user());
}

Result<aquarium::api::User>
BackendClient::AddUser(const std::string& name, const std::string& inviteKey) {
    auto context = CreateContext(std::chrono::milliseconds(5000), inviteKey);
    aquarium::api::AddUserRequest request;
    request.set_name(name);
    request.set_invite_key(inviteKey);
    aquarium::api::AddUserResponse response;

    grpc::Status status =
        GetUserStub()->AddUser(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::User>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::User>(response.user());
}

Result<aquarium::api::User>
BackendClient::UpdateUser(std::int64_t userId, const std::string& newName) {
    auto context = CreateContext();
    aquarium::api::UpdateUserRequest request;
    request.set_id(userId);
    request.set_newname(newName);
    aquarium::api::UpdateUserResponse response;

    grpc::Status status =
        GetUserStub()->UpdateUser(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::User>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::User>(response.user());
}

Result<bool> BackendClient::DeleteUser(std::int64_t userId) {
    auto context = CreateContext();
    aquarium::api::DeleteUserRequest request;
    request.set_id(userId);
    aquarium::api::DeleteUserResponse response;

    grpc::Status status =
        GetUserStub()->DeleteUser(context.get(), request, &response);
    if (!status.ok()) {
        return Result<bool>(MapGrpcStatus(status));
    }
    return Result<bool>(response.success());
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
        GetTaskStub()->GetTasks(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::GetTasksResponse>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::GetTasksResponse>(std::move(response));
}

Result<aquarium::api::Task> BackendClient::GetTask(std::int64_t taskId) {
    auto context = CreateContext();
    aquarium::api::GetTaskRequest request;
    request.set_id(taskId);
    aquarium::api::GetTaskResponse response;

    grpc::Status status =
        GetTaskStub()->GetTask(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::Task>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::Task>(response.task());
}

Result<aquarium::api::Task>
BackendClient::CreateTask(const aquarium::api::CreateTaskRequest& request) {
    auto context = CreateContext();
    aquarium::api::CreateTaskResponse response;

    grpc::Status status =
        GetTaskStub()->CreateTask(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::Task>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::Task>(response.task());
}

Result<aquarium::api::Task>
BackendClient::UpdateTask(const aquarium::api::UpdateTaskRequest& request) {
    auto context = CreateContext();
    aquarium::api::UpdateTaskResponse response;

    grpc::Status status =
        GetTaskStub()->UpdateTask(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::Task>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::Task>(response.task());
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
        GetTaskStub()->UpdateTaskStatus(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::Task>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::Task>(response.task());
}

Result<aquarium::api::Task> BackendClient::AssignWorker(std::int64_t taskId,
                                                        std::int64_t workerId) {
    auto context = CreateContext();
    aquarium::api::AssignWorkerRequest request;
    request.set_id(taskId);
    request.set_workerid(workerId);
    aquarium::api::AssignWorkerResponse response;

    grpc::Status status =
        GetTaskStub()->AssignWorker(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::Task>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::Task>(response.task());
}

Result<aquarium::api::Task> BackendClient::RemoveWorker(std::int64_t taskId,
                                                        std::int64_t workerId) {
    auto context = CreateContext();
    aquarium::api::RemoveWorkerRequest request;
    request.set_id(taskId);
    request.set_workerid(workerId);
    aquarium::api::RemoveWorkerResponse response;

    grpc::Status status =
        GetTaskStub()->RemoveWorker(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::Task>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::Task>(response.task());
}

Result<bool> BackendClient::DeleteTask(std::int64_t taskId) {
    auto context = CreateContext();
    aquarium::api::DeleteTaskRequest request;
    request.set_id(taskId);
    aquarium::api::DeleteTaskResponse response;

    grpc::Status status =
        GetTaskStub()->DeleteTask(context.get(), request, &response);
    if (!status.ok()) {
        return Result<bool>(MapGrpcStatus(status));
    }
    return Result<bool>(response.success());
}

Result<std::vector<aquarium::api::Comment>>
BackendClient::GetComments(std::int64_t taskId) {
    auto context = CreateContext();
    aquarium::api::GetCommentsRequest request;
    request.set_taskid(taskId);
    aquarium::api::GetCommentsResponse response;

    grpc::Status status =
        GetTaskStub()->GetComments(context.get(), request, &response);
    if (!status.ok()) {
        return Result<std::vector<aquarium::api::Comment>>(
            MapGrpcStatus(status));
    }

    std::vector<aquarium::api::Comment> comments;
    comments.reserve(response.comments_size());
    for (int i = 0; i < response.comments_size(); ++i) {
        comments.push_back(response.comments(i));
    }
    return Result<std::vector<aquarium::api::Comment>>(std::move(comments));
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
        GetTaskStub()->AddComment(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::Comment>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::Comment>(response.comment());
}

Result<aquarium::api::RejectTaskResponse>
BackendClient::RejectTask(std::int64_t taskId, const std::string& reason) {
    auto context = CreateContext();
    aquarium::api::RejectTaskRequest request;
    request.set_taskid(taskId);
    request.set_reason(reason);
    aquarium::api::RejectTaskResponse response;

    grpc::Status status =
        GetTaskStub()->RejectTask(context.get(), request, &response);
    if (!status.ok()) {
        return Result<aquarium::api::RejectTaskResponse>(MapGrpcStatus(status));
    }
    return Result<aquarium::api::RejectTaskResponse>(std::move(response));
}

std::unique_ptr<grpc::ClientReader<aquarium::api::TaskEvent>>
BackendClient::SubscribeEvents(grpc::ClientContext* context) {
    aquarium::api::SubscribeEventsRequest request;
    return GetTaskStub()->SubscribeEvents(context, request);
}

} // namespace Aquarium::Grpc
