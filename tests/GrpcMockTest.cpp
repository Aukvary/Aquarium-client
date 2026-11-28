#include "Common/Types.hpp"
#include "Grpc/BackendClient.hpp"
#include "Grpc/EventSubscriber.hpp"
#include "Grpc/HealthChecker.hpp"

#include <chrono>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <TaskStoreService.grpc.pb.h>
#include <thread>
#include <UserStoreService.grpc.pb.h>
#include <UtilsService.grpc.pb.h>
#include <vector>

namespace Aquarium::Grpc::Tests {

using namespace aquarium::api;
using Common::AppErrorCode;

class MockUtilsServiceImpl final : public UtilsService::Service {
public:
    grpc::Status Health(grpc::ServerContext* /*context*/,
                        const HealthRequest* /*request*/,
                        HealthResponse* response) override {
        response->set_status(HealthResponse::SERVING);
        response->set_db_initialized("true");
        response->set_utilities_initialized(true);
        return grpc::Status::OK;
    }
};

class MockUserStoreServiceImpl final : public UserStoreService::Service {
public:
    grpc::Status GetAll(grpc::ServerContext* context,
                        const GetAllUsersRequest* /*request*/,
                        GetAllUsersResponse* response) override {
        auto clientMetadata = context->client_metadata();
        auto authIt = clientMetadata.find("authorization");
        if (authIt == clientMetadata.end() ||
            authIt->second != "Bearer valid_token") {
            return {grpc::StatusCode::UNAUTHENTICATED,
                    "Missing or invalid Bearer token"};
        }

        auto* u1 = response->add_users();
        u1->set_id(1);
        u1->set_name("alice");

        auto* u2 = response->add_users();
        u2->set_id(2);
        u2->set_name("bob");

        return grpc::Status::OK;
    }

    grpc::Status GetUser(grpc::ServerContext* /*context*/,
                         const GetUserRequest* request,
                         GetUserResponse* response) override {
        if (request->id() == 999) {
            return {grpc::StatusCode::NOT_FOUND, "User not found"};
        }
        auto* user = response->mutable_user();
        user->set_id(request->id());
        user->set_name("user_" + std::to_string(request->id()));
        return grpc::Status::OK;
    }

    grpc::Status AddUser(grpc::ServerContext* context,
                         const AddUserRequest* request,
                         AddUserResponse* response) override {
        auto clientMetadata = context->client_metadata();
        auto inviteIt = clientMetadata.find("invite-key");
        if (inviteIt == clientMetadata.end() ||
            inviteIt->second != "Invite secret_invite_code") {
            return grpc::Status{grpc::StatusCode::PERMISSION_DENIED,
                                "Invalid invite key"};
        }

        auto* user = response->mutable_user();
        user->set_id(42);
        user->set_name(request->name());
        user->set_key("generated_token_hex_42");
        return grpc::Status::OK;
    }

    grpc::Status UpdateUser(grpc::ServerContext* /*context*/,
                            const UpdateUserRequest* request,
                            UpdateUserResponse* response) override {
        auto* user = response->mutable_user();
        user->set_id(request->id());
        user->set_name(request->newname());
        return grpc::Status::OK;
    }

    grpc::Status DeleteUser(grpc::ServerContext* /*context*/,
                            const DeleteUserRequest* request,
                            DeleteUserResponse* response) override {
        if (request->id() == 1) {
            return {grpc::StatusCode::PERMISSION_DENIED, "Cannot delete admin"};
        }
        response->set_success(true);
        return grpc::Status::OK;
    }
};

class MockTaskStoreServiceImpl final : public TaskStoreService::Service {
public:
    grpc::Status CreateTask(grpc::ServerContext* /*context*/,
                            const CreateTaskRequest* request,
                            CreateTaskResponse* response) override {
        auto* task = response->mutable_task();
        task->set_id(100);
        task->set_title(request->title());
        task->set_description(request->description());
        task->set_authorid(request->authorid());
        task->set_status(request->status());
        for (int i = 0; i < request->workerids_size(); ++i) {
            task->add_workerids(request->workerids(i));
        }
        return grpc::Status::OK;
    }

    grpc::Status GetTask(grpc::ServerContext* /*context*/,
                         const GetTaskRequest* request,
                         GetTaskResponse* response) override {
        if (request->id() == 404) {
            return {grpc::StatusCode::NOT_FOUND, "Task not found"};
        }
        auto* task = response->mutable_task();
        task->set_id(request->id());
        task->set_title("Test Task " + std::to_string(request->id()));
        task->set_status(TaskStatus::ACTIVE);
        return grpc::Status::OK;
    }

    grpc::Status GetTasks(grpc::ServerContext* /*context*/,
                          const GetTasksRequest* /*request*/,
                          GetTasksResponse* response) override {
        auto* t1 = response->add_tasks();
        t1->set_id(1);
        t1->set_title("Task One");
        t1->set_status(TaskStatus::ACTIVE);

        auto* t2 = response->add_tasks();
        t2->set_id(2);
        t2->set_title("Task Two");
        t2->set_status(TaskStatus::IN_REVIEW);

        response->set_totalcount(2);
        return grpc::Status::OK;
    }

    grpc::Status UpdateTaskStatus(grpc::ServerContext* /*context*/,
                                  const UpdateTaskStatusRequest* request,
                                  UpdateTaskStatusResponse* response) override {
        auto* task = response->mutable_task();
        task->set_id(request->id());
        task->set_status(request->status());
        return grpc::Status::OK;
    }

    grpc::Status AssignWorker(grpc::ServerContext* /*context*/,
                              const AssignWorkerRequest* request,
                              AssignWorkerResponse* response) override {
        auto* task = response->mutable_task();
        task->set_id(request->id());
        task->add_workerids(request->workerid());
        return grpc::Status::OK;
    }

    grpc::Status RemoveWorker(grpc::ServerContext* /*context*/,
                              const RemoveWorkerRequest* request,
                              RemoveWorkerResponse* response) override {
        auto* task = response->mutable_task();
        task->set_id(request->id());
        return grpc::Status::OK;
    }

    grpc::Status DeleteTask(grpc::ServerContext* /*context*/,
                            const DeleteTaskRequest* /*request*/,
                            DeleteTaskResponse* response) override {
        response->set_success(true);
        return grpc::Status::OK;
    }

    grpc::Status GetComments(grpc::ServerContext* /*context*/,
                             const GetCommentsRequest* request,
                             GetCommentsResponse* response) override {
        auto* c1 = response->add_comments();
        c1->set_id(10);
        c1->set_taskid(request->taskid());
        c1->set_content("First remark");
        c1->set_type(CommentType::COMMENT_GENERAL);

        return grpc::Status::OK;
    }

    grpc::Status AddComment(grpc::ServerContext* /*context*/,
                            const AddCommentRequest* request,
                            AddCommentResponse* response) override {
        auto* c = response->mutable_comment();
        c->set_id(11);
        c->set_taskid(request->taskid());
        c->set_content(request->content());
        c->set_type(request->type());
        return grpc::Status::OK;
    }

    grpc::Status RejectTask(grpc::ServerContext* /*context*/,
                            const RejectTaskRequest* request,
                            RejectTaskResponse* response) override {
        if (request->reason().empty()) {
            return {grpc::StatusCode::INVALID_ARGUMENT,
                    "Reason cannot be empty"};
        }
        auto* task = response->mutable_task();
        task->set_id(request->taskid());
        task->set_status(TaskStatus::ACTIVE);

        auto* comment = response->mutable_comment();
        comment->set_id(99);
        comment->set_taskid(request->taskid());
        comment->set_content(request->reason());
        comment->set_type(CommentType::COMMENT_REVISION_REASON);
        return grpc::Status::OK;
    }

    grpc::Status
    SubscribeEvents(grpc::ServerContext* context,
                    const SubscribeEventsRequest* /*request*/,
                    grpc::ServerWriter<TaskEvent>* writer) override {
        TaskEvent ev;
        ev.set_type(TaskEvent::TASK_CREATED);
        ev.set_taskid(101);
        auto* task = ev.mutable_task();
        task->set_id(101);
        task->set_title("Live streamed task");
        task->set_status(TaskStatus::ACTIVE);

        writer->Write(ev);

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return grpc::Status::OK;
    }
};

// --- Test Fixture ---

class BackendClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        grpc::ServerBuilder builder;
        builder.AddListeningPort(
            "localhost:0", grpc::InsecureServerCredentials(), &_selectedPort);
        builder.RegisterService(&_utilsService);
        builder.RegisterService(&_userService);
        builder.RegisterService(&_taskService);
        _server = builder.BuildAndStart();
        ASSERT_NE(_server, nullptr);

        _serverAddress = "localhost:" + std::to_string(_selectedPort);
        _client =
            std::make_shared<BackendClient>(_serverAddress, "valid_token");
    }

    void TearDown() override {
        if (_server) {
            _server->Shutdown();
            _server->Wait();
        }
    }

    int _selectedPort{0};
    std::string _serverAddress;
    MockUtilsServiceImpl _utilsService;
    MockUserStoreServiceImpl _userService;
    MockTaskStoreServiceImpl _taskService;
    std::unique_ptr<grpc::Server> _server;
    std::shared_ptr<BackendClient> _client;
};

TEST(GrpcErrorMappingTest, MapsStatusCodesCorrectly) {
    EXPECT_EQ(MapGrpcStatus(grpc::Status::OK).code, AppErrorCode::OK);

    auto unauth = MapGrpcStatus(
        grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "token expired"));
    EXPECT_EQ(unauth.code, AppErrorCode::UNAUTHENTICATED);
    EXPECT_TRUE(unauth.IsAuthError());

    auto perm = MapGrpcStatus(
        grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "admin required"));
    EXPECT_EQ(perm.code, AppErrorCode::PERMISSION_DENIED);
    EXPECT_TRUE(perm.IsPermissionDenied());

    auto notFound = MapGrpcStatus(
        grpc::Status(grpc::StatusCode::NOT_FOUND, "missing item"));
    EXPECT_EQ(notFound.code, AppErrorCode::NOT_FOUND);
    EXPECT_TRUE(notFound.IsNotFound());

    auto unavail = MapGrpcStatus(
        grpc::Status(grpc::StatusCode::UNAVAILABLE, "endpoint down"));
    EXPECT_EQ(unavail.code, AppErrorCode::UNAVAILABLE);
    EXPECT_TRUE(unavail.IsUnavailable());

    auto invalidArg = MapGrpcStatus(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "bad length"));
    EXPECT_EQ(invalidArg.code, AppErrorCode::INVALID_ARGUMENT);

    auto internalErr = MapGrpcStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, "server crashed"));
    EXPECT_EQ(internalErr.code, AppErrorCode::INTERNAL_ERROR);
}

TEST_F(BackendClientTest, HealthRpc) {
    auto result = _client->Health();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().status(), HealthResponse::SERVING);
    EXPECT_EQ(result.Value().db_initialized(), "true");
    EXPECT_TRUE(result.Value().utilities_initialized());
}

TEST_F(BackendClientTest, UserStoreOperationsAndAuthHeader) {
    // 1. GetAllUsers with valid token
    auto usersResult = _client->GetAllUsers().Get();
    ASSERT_TRUE(usersResult.HasValue());
    EXPECT_EQ(usersResult.Value().size(), 2);
    EXPECT_EQ(usersResult.Value()[0].name(), "alice");

    // 2. GetAllUsers with bad token returns UNAUTHENTICATED
    _client->SetAuthToken("wrong_token");
    auto failUsers = _client->GetAllUsers().Get();
    EXPECT_FALSE(failUsers.HasValue());
    EXPECT_TRUE(failUsers.Error().IsAuthError());
    _client->SetAuthToken("valid_token");

    // 3. GetUser success & not found
    auto user1 = _client->GetUser(1);
    ASSERT_TRUE(user1.HasValue());
    EXPECT_EQ(user1.Value().id(), 1);

    auto user404 = _client->GetUser(999);
    EXPECT_FALSE(user404.HasValue());
    EXPECT_TRUE(user404.Error().IsNotFound());

    // 4. AddUser with invite key
    auto addFail = _client->AddUser("dave", "bad_invite");
    EXPECT_FALSE(addFail.HasValue());
    EXPECT_TRUE(addFail.Error().IsPermissionDenied());

    auto addSuccess = _client->AddUser("dave", "secret_invite_code");
    ASSERT_TRUE(addSuccess.HasValue());
    EXPECT_EQ(addSuccess.Value().name(), "dave");
    EXPECT_EQ(addSuccess.Value().id(), 42);
    EXPECT_EQ(addSuccess.Value().key(), "generated_token_hex_42");

    // 5. UpdateUser
    auto updated = _client->UpdateUser(42, "dave_updated");
    ASSERT_TRUE(updated.HasValue());
    EXPECT_EQ(updated.Value().name(), "dave_updated");

    // 6. DeleteUser
    EXPECT_FALSE(_client->DeleteUser(1).HasValue());
    auto deleteOk = _client->DeleteUser(42);
    ASSERT_TRUE(deleteOk.HasValue());
    EXPECT_TRUE(deleteOk.Value());
}

TEST_F(BackendClientTest, TaskStoreOperations) {
    // 1. CreateTask
    CreateTaskRequest createReq;
    createReq.set_title("Fix bug in TUI");
    createReq.set_description("Description details");
    createReq.set_authorid(1);
    createReq.set_status(TaskStatus::PLAN);
    createReq.add_workerids(2);

    auto created = _client->CreateTask(createReq);
    ASSERT_TRUE(created.HasValue());
    EXPECT_EQ(created.Value().id(), 100);
    EXPECT_EQ(created.Value().title(), "Fix bug in TUI");

    // 2. GetTasks
    auto tasksResult = _client->GetTasks();
    ASSERT_TRUE(tasksResult.HasValue());
    EXPECT_EQ(tasksResult.Value().tasks_size(), 2);
    EXPECT_EQ(tasksResult.Value().totalcount(), 2);

    // 3. GetTask
    auto task1 = _client->GetTask(1);
    ASSERT_TRUE(task1.HasValue());
    EXPECT_EQ(task1.Value().id(), 1);

    auto task404 = _client->GetTask(404);
    EXPECT_FALSE(task404.HasValue());
    EXPECT_TRUE(task404.Error().IsNotFound());

    // 4. UpdateTaskStatus
    auto statusUpdate = _client->UpdateTaskStatus(1, TaskStatus::COMPLETE);
    ASSERT_TRUE(statusUpdate.HasValue());
    EXPECT_EQ(statusUpdate.Value().status(), TaskStatus::COMPLETE);

    // 5. AssignWorker & RemoveWorker
    auto assignRes = _client->AssignWorker(1, 3);
    ASSERT_TRUE(assignRes.HasValue());

    auto removeRes = _client->RemoveWorker(1, 3);
    ASSERT_TRUE(removeRes.HasValue());

    // 6. Comments & RejectTask
    auto comments = _client->GetComments(1);
    ASSERT_TRUE(comments.HasValue());
    EXPECT_EQ(comments.Value().size(), 1);
    EXPECT_EQ(comments.Value()[0].content(), "First remark");

    auto addComment = _client->AddComment(1, "New comment content");
    ASSERT_TRUE(addComment.HasValue());
    EXPECT_EQ(addComment.Value().content(), "New comment content");

    auto rejectFail = _client->RejectTask(1, "");
    EXPECT_FALSE(rejectFail.HasValue());
    EXPECT_EQ(rejectFail.Error().code, AppErrorCode::INVALID_ARGUMENT);

    auto rejectSuccess = _client->RejectTask(1, "Please fix unit tests!");
    ASSERT_TRUE(rejectSuccess.HasValue());
    EXPECT_EQ(rejectSuccess.Value().task().status(), TaskStatus::ACTIVE);
    EXPECT_EQ(rejectSuccess.Value().comment().type(),
              CommentType::COMMENT_REVISION_REASON);
    EXPECT_EQ(rejectSuccess.Value().comment().content(),
              "Please fix unit tests!");
}

TEST_F(BackendClientTest, PeriodicHealthCheckerLifecycle) {
    std::atomic<int> healthCheckCount{0};
    std::atomic<bool> lastServingState{false};

    HealthChecker checker(*_client, std::chrono::milliseconds(50),
                          [&](const Result<HealthResponse>& res) {
                              healthCheckCount++;
                              if (res.HasValue() &&
                                  res.Value().status() ==
                                      HealthResponse::SERVING) {
                                  lastServingState = true;
                              }
                          });

    checker.Start();
    EXPECT_TRUE(checker.IsRunning());

    checker.TriggerCheck();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    EXPECT_GT(healthCheckCount.load(), 0);
    EXPECT_TRUE(lastServingState.load());

    checker.Stop();
    EXPECT_FALSE(checker.IsRunning());
}

TEST_F(BackendClientTest, EventSubscriberReceivesLiveEvents) {
    std::atomic<bool> receivedEvent{false};
    std::atomic<int64_t> receivedTaskId{0};

    EventSubscriber subscriber(*_client, [&](const TaskEvent& event) {
        if (event.type() == TaskEvent::TASK_CREATED) {
            receivedTaskId = event.taskid();
            receivedEvent = true;
        }
    });

    subscriber.Start();
    EXPECT_TRUE(subscriber.IsRunning());

    for (int i = 0; i < 50; ++i) {
        if (receivedEvent.load()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_TRUE(receivedEvent.load());
    EXPECT_EQ(receivedTaskId.load(), 101);

    subscriber.Stop();
    EXPECT_FALSE(subscriber.IsRunning());
}

} // namespace Aquarium::Grpc::Tests
