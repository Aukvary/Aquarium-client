#include "State/AppState.hpp"

#include "State/Actions.hpp"
#include "State/Reducer.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <TaskStoreService.pb.h>
#include <UserStoreService.pb.h>
#include <vector>

namespace Aquarium::State::Tests {

using namespace aquarium::api;

class AppStateTest : public ::testing::Test {
protected:
    AppState _state;
};

TEST_F(AppStateTest, InitialStateDefaults) {
    EXPECT_EQ(_state.currentView, ViewType::Tasks);
    EXPECT_EQ(_state.activeModal, ModalType::None);
    EXPECT_EQ(_state.connectionStatus, ConnectionStatus::Connecting);
    EXPECT_FALSE(_state.activeToast.has_value());
    EXPECT_FALSE(_state.selectedTaskId.has_value());
    EXPECT_FALSE(_state.filter.statusFilter.has_value());
    EXPECT_TRUE(_state.filter.searchQuery.empty());
    EXPECT_FALSE(_state.session.IsAuthenticated());
}

TEST_F(AppStateTest, NavigationAndModalTransitions) {
    _state = Reducer::Reduce(_state, NavigateToViewAction{ViewType::Review});
    EXPECT_EQ(_state.currentView, ViewType::Review);

    _state =
        Reducer::Reduce(_state, NavigateToViewAction{ViewType::CreateTask});
    EXPECT_EQ(_state.currentView, ViewType::CreateTask);

    _state = Reducer::Reduce(_state, OpenModalAction{ModalType::AuthModal});
    EXPECT_EQ(_state.activeModal, ModalType::AuthModal);

    _state = Reducer::Reduce(_state, CloseModalAction{});
    EXPECT_EQ(_state.activeModal, ModalType::None);
}

TEST_F(AppStateTest, TaskSelectionAndCommentClearing) {
    _state.selectedTaskId = 1;
    Comment c;
    c.set_id(10);
    c.set_taskid(1);
    c.set_content("Remark");
    _state.activeComments.push_back(c);

    _state = Reducer::Reduce(_state, SelectTaskAction{1});
    EXPECT_EQ(_state.selectedTaskId, 1);
    EXPECT_EQ(_state.activeComments.size(), 1);

    _state = Reducer::Reduce(_state, SelectTaskAction{2});
    EXPECT_EQ(_state.selectedTaskId, 2);
    EXPECT_TRUE(_state.activeComments.empty());

    _state = Reducer::Reduce(_state, SelectTaskAction{std::nullopt});
    EXPECT_FALSE(_state.selectedTaskId.has_value());
}

TEST_F(AppStateTest, FiltersAndSearchQuery) {
    _state = Reducer::Reduce(_state, SetStatusFilterAction{TaskStatus::ACTIVE});
    ASSERT_TRUE(_state.filter.statusFilter.has_value());
    EXPECT_EQ(*_state.filter.statusFilter, TaskStatus::ACTIVE);

    _state = Reducer::Reduce(_state, SetStatusFilterAction{std::nullopt});
    EXPECT_FALSE(_state.filter.statusFilter.has_value());

    _state = Reducer::Reduce(_state, SetSearchQueryAction{"deploy pipeline"});
    EXPECT_EQ(_state.filter.searchQuery, "deploy pipeline");
}

TEST_F(AppStateTest, ToastLifecycle) {
    _state =
        Reducer::Reduce(_state, ShowToastAction{
                                    .message = "Task #42 updated",
                                    .severity = ToastSeverity::Success,
                                    .duration = std::chrono::milliseconds(5000),
                                });

    ASSERT_TRUE(_state.activeToast.has_value());
    EXPECT_EQ(_state.activeToast->message, "Task #42 updated");
    EXPECT_EQ(_state.activeToast->severity, ToastSeverity::Success);
    EXPECT_FALSE(_state.activeToast->IsExpired());

    _state = Reducer::Reduce(_state, DismissToastAction{});
    EXPECT_FALSE(_state.activeToast.has_value());
}

TEST_F(AppStateTest, SessionAndConnectionStatus) {
    _state = Reducer::Reduce(
        _state, SetConnectionStatusAction{ConnectionStatus::Connected});
    EXPECT_EQ(_state.connectionStatus, ConnectionStatus::Connected);

    _state = Reducer::Reduce(_state, SetSessionAction{
                                         .server = "192.168.1.10:50051",
                                         .userId = 7,
                                         .userName = "developer",
                                         .token = "bearer_secret_xyz",
                                     });

    EXPECT_EQ(_state.session.serverHost, "192.168.1.10:50051");
    EXPECT_EQ(_state.session.userId, 7);
    EXPECT_EQ(_state.session.userName, "developer");
    EXPECT_EQ(_state.session.authToken, "bearer_secret_xyz");
    EXPECT_TRUE(_state.session.IsAuthenticated());
}

TEST_F(AppStateTest, TasksLoadedAutoSelectsFirstTask) {
    Task t1;
    t1.set_id(101);
    t1.set_title("Task A");
    Task t2;
    t2.set_id(102);
    t2.set_title("Task B");

    _state.isLoadingTasks = true;
    _state = Reducer::Reduce(_state, TasksLoadedAction{{t1, t2}});

    EXPECT_FALSE(_state.isLoadingTasks);
    EXPECT_EQ(_state.tasks.size(), 2);
    ASSERT_TRUE(_state.selectedTaskId.has_value());
    EXPECT_EQ(*_state.selectedTaskId, 101);

    _state.selectedTaskId = 999;
    _state = Reducer::Reduce(_state, TasksLoadedAction{{t2}});
    EXPECT_EQ(_state.tasks.size(), 1);
    ASSERT_TRUE(_state.selectedTaskId.has_value());
    EXPECT_EQ(*_state.selectedTaskId, 102);
}

TEST_F(AppStateTest, CommentsAndUsersLoaded) {
    _state.selectedTaskId = 50;

    Comment c1;
    c1.set_id(1);
    c1.set_taskid(50);
    c1.set_content("Remark for 50");

    _state = Reducer::Reduce(
        _state, CommentsLoadedAction{.taskId = 40, .comments = {c1}});
    EXPECT_TRUE(_state.activeComments.empty());

    _state = Reducer::Reduce(
        _state, CommentsLoadedAction{.taskId = 50, .comments = {c1}});
    EXPECT_EQ(_state.activeComments.size(), 1);
    EXPECT_EQ(_state.activeComments[0].content(), "Remark for 50");

    User u;
    u.set_id(3);
    u.set_name("reviewer");
    _state = Reducer::Reduce(_state, UsersLoadedAction{{u}});
    EXPECT_EQ(_state.users.size(), 1);
    EXPECT_NE(_state.FindUser(3), nullptr);
    EXPECT_EQ(_state.FindUser(3)->name(), "reviewer");
    EXPECT_EQ(_state.FindUser(99), nullptr);
}

TEST_F(AppStateTest, TaskMutationActions) {
    Task t;
    t.set_id(10);
    t.set_title("Initial title");
    t.set_status(TaskStatus::PLAN);

    _state = Reducer::Reduce(_state, TaskCreatedAction{t});
    ASSERT_EQ(_state.tasks.size(), 1);
    EXPECT_EQ(_state.tasks[0].id(), 10);
    EXPECT_EQ(_state.selectedTaskId, 10);

    _state = Reducer::Reduce(_state, TaskCreatedAction{t});
    EXPECT_EQ(_state.tasks.size(), 1);

    t.set_title("Updated title");
    _state = Reducer::Reduce(_state, TaskUpdatedAction{t});
    EXPECT_EQ(_state.tasks[0].title(), "Updated title");

    _state = Reducer::Reduce(
        _state,
        TaskStatusChangedAction{.taskId = 10, .newStatus = TaskStatus::ACTIVE});
    EXPECT_EQ(_state.tasks[0].status(), TaskStatus::ACTIVE);

    Comment c;
    c.set_id(100);
    c.set_taskid(10);
    c.set_content("New comment");
    _state = Reducer::Reduce(_state, CommentAddedAction{c});
    ASSERT_EQ(_state.activeComments.size(), 1);
    EXPECT_EQ(_state.activeComments[0].id(), 100);

    _state = Reducer::Reduce(_state, CommentAddedAction{c});
    EXPECT_EQ(_state.activeComments.size(), 1);

    t.set_status(TaskStatus::ACTIVE);
    Comment rejComment;
    rejComment.set_id(101);
    rejComment.set_taskid(10);
    rejComment.set_content("Please revise");
    rejComment.set_type(CommentType::COMMENT_REVISION_REASON);

    _state = Reducer::Reduce(
        _state, TaskRejectedAction{.task = t, .comment = rejComment});
    EXPECT_EQ(_state.tasks[0].status(), TaskStatus::ACTIVE);
    EXPECT_EQ(_state.activeComments.size(), 2);
    EXPECT_EQ(_state.activeComments.back().content(), "Please revise");

    _state = Reducer::Reduce(_state, TaskDeletedAction{10});
    EXPECT_TRUE(_state.tasks.empty());
    EXPECT_FALSE(_state.selectedTaskId.has_value());
    EXPECT_TRUE(_state.activeComments.empty());
}

TEST_F(AppStateTest, GetFilteredTasksLogic) {
    Task t1;
    t1.set_id(1);
    t1.set_title("Implement gRPC client");
    t1.set_status(TaskStatus::ACTIVE);

    Task t2;
    t2.set_id(2);
    t2.set_title("Review merge request");
    t2.set_status(TaskStatus::IN_REVIEW);

    Task t3;
    t3.set_id(3);
    t3.set_title("Database migrations");
    t3.set_status(TaskStatus::COMPLETE);

    _state.tasks = {t1, t2, t3};

    EXPECT_EQ(_state.GetFilteredTasks().size(), 3);

    _state.filter.statusFilter = TaskStatus::ACTIVE;
    auto activeTasks = _state.GetFilteredTasks();
    ASSERT_EQ(activeTasks.size(), 1);
    EXPECT_EQ(activeTasks[0]->id(), 1);
    _state.filter.statusFilter = std::nullopt;

    _state.currentView = ViewType::Review;
    auto reviewTasks = _state.GetFilteredTasks();
    ASSERT_EQ(reviewTasks.size(), 1);
    EXPECT_EQ(reviewTasks[0]->id(), 2);
    _state.currentView = ViewType::Tasks;

    _state.filter.searchQuery = "GRPC";
    auto searchRes = _state.GetFilteredTasks();
    ASSERT_EQ(searchRes.size(), 1);
    EXPECT_EQ(searchRes[0]->id(), 1);

    _state.filter.searchQuery = "3";
    auto idRes = _state.GetFilteredTasks();
    ASSERT_EQ(idRes.size(), 1);
    EXPECT_EQ(idRes[0]->id(), 3);

    t1.set_description("Client networking in C++");
    _state.tasks[0] = t1;
    _state.filter.searchQuery = "networking";
    EXPECT_EQ(_state.GetFilteredTasks().size(), 1);

    Task tRu;
    tRu.set_id(4);
    tRu.set_title("Тестовая задача");
    tRu.set_description("Описание задачи");
    _state.tasks.push_back(tRu);
    _state.filter.searchQuery = "ТЕСТОВАЯ";
    auto ruRes = _state.GetFilteredTasks();
    ASSERT_EQ(ruRes.size(), 1);
    EXPECT_EQ(ruRes[0]->id(), 4);

    _state.tasks.push_back(tRu);
    _state.filter.searchQuery = "задача";
    auto dedupRes = _state.GetFilteredTasks();
    EXPECT_EQ(dedupRes.size(), 1);

    _state.filter.searchQuery = "   ";
    EXPECT_EQ(_state.GetFilteredTasks().size(), 4);
}

TEST_F(AppStateTest, ExitProjectClearsSessionAndDataAndOpensProjectModal) {
    _state = Reducer::Reduce(_state, SetSessionAction{
                                         .server = "localhost:8081",
                                         .userId = 10,
                                         .userName = "alice",
                                         .token = "tok_123",
                                     });
    _state = Reducer::Reduce(
        _state, SetConnectionStatusAction{ConnectionStatus::Connected});
    _state = Reducer::Reduce(_state, NavigateToViewAction{ViewType::Review});
    _state = Reducer::Reduce(_state, SetSearchQueryAction{"bug"});

    Task t;
    t.set_id(1);
    t.set_title("Fix bug");
    _state = Reducer::Reduce(_state, TaskCreatedAction{t});

    User u;
    u.set_id(10);
    u.set_name("alice");
    _state = Reducer::Reduce(_state, UsersLoadedAction{{u}});

    Comment c;
    c.set_id(1);
    c.set_taskid(1);
    c.set_content("Hello");
    _state = Reducer::Reduce(_state, CommentAddedAction{c});

    _state = Reducer::Reduce(_state, ExitProjectAction{});

    EXPECT_FALSE(_state.session.IsAuthenticated());
    EXPECT_TRUE(_state.session.serverHost.empty());
    EXPECT_EQ(_state.session.userId, 0);
    EXPECT_TRUE(_state.session.userName.empty());
    EXPECT_TRUE(_state.tasks.empty());
    EXPECT_FALSE(_state.selectedTaskId.has_value());
    EXPECT_TRUE(_state.activeComments.empty());
    EXPECT_TRUE(_state.users.empty());
    EXPECT_TRUE(_state.filter.searchQuery.empty());
    EXPECT_EQ(_state.connectionStatus, ConnectionStatus::Offline);
    EXPECT_EQ(_state.currentView, ViewType::Tasks);
    EXPECT_EQ(_state.activeModal, ModalType::ProjectModal);
}

} // namespace Aquarium::State::Tests
