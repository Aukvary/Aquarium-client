#include "Common/TimeUtils.hpp"
#include "State/AppState.hpp"
#include "UI/Components/RemarksPane.hpp"
#include "UI/Components/StatusBar.hpp"
#include "UI/Components/TaskDetailsPane.hpp"
#include "UI/NavigationSidebar.hpp"
#include "UI/UIHelpers.hpp"
#include "UI/Views/AuthModal.hpp"
#include "UI/Views/CreateTaskView.hpp"
#include "UI/Views/ReviewView.hpp"
#include "UI/Views/TaskBoardView.hpp"

#include <chrono>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>
#include <string>

namespace Aquarium::UI::Tests {

using namespace ftxui;
using namespace aquarium::api;
using State::AppState;
using State::ConnectionStatus;
using State::ToastMessage;
using State::ToastSeverity;
using State::ViewType;

TEST(UiComponentsTest, StatusBarRendersConnectionAndToast) {
    AppState state;
    state.connectionStatus = ConnectionStatus::Connected;
    state.activeToast = ToastMessage{
        .message = "Task #7 created successfully",
        .severity = ToastSeverity::Success,
        .expiryTime =
            std::chrono::steady_clock::now() + std::chrono::seconds(5),
    };

    auto comp = Components::CreateStatusBar(state);
    auto elem = comp->Render();

    auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(1));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("[HEALTH: OK]"), std::string::npos);
    EXPECT_NE(rendered.find("[OK]"), std::string::npos);
    EXPECT_NE(rendered.find("Task #7 created successfully"), std::string::npos);
    EXPECT_NE(rendered.find("[q:Quit]"), std::string::npos);
}

TEST(UiComponentsTest, StatusBarRendersOfflineState) {
    AppState state;
    state.connectionStatus = ConnectionStatus::Offline;

    auto comp = Components::CreateStatusBar(state);
    auto elem = comp->Render();

    auto screen = Screen::Create(Dimension::Fixed(100), Dimension::Fixed(1));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("[OFFLINE]"), std::string::npos);
}

TEST(UiComponentsTest, NavigationSidebarRendersAndHandlesHotkeys) {
    AppState state;
    state.currentView = ViewType::Tasks;

    ViewType lastNavigated = ViewType::Tasks;
    auto comp = CreateNavigationSidebar(
        state, [&](ViewType view) { lastNavigated = view; });

    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(30), Dimension::Fixed(15));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("NAVIGATION"), std::string::npos);
    EXPECT_NE(rendered.find("[1] Tasks"), std::string::npos);
    EXPECT_NE(rendered.find("[2] Review"), std::string::npos);
    EXPECT_NE(rendered.find("[3] Create"), std::string::npos);

    // Test hotkeys 1, 2, 3
    EXPECT_TRUE(comp->OnEvent(Event::Character('2')));
    EXPECT_EQ(lastNavigated, ViewType::Review);

    EXPECT_TRUE(comp->OnEvent(Event::Character('3')));
    EXPECT_EQ(lastNavigated, ViewType::CreateTask);

    EXPECT_TRUE(comp->OnEvent(Event::Character('1')));
    EXPECT_EQ(lastNavigated, ViewType::Tasks);

    // Unhandled event
    EXPECT_FALSE(comp->OnEvent(Event::Character('x')));
}

TEST(UiComponentsTest, TaskDetailsPaneRendersSelectedTask) {
    AppState state;
    Task task;
    task.set_id(42);
    task.set_title("Optimize SQL query indexing");
    task.set_description("Add composite index on status and created_at.");
    task.set_status(TaskStatus::ACTIVE);
    task.set_authorid(1);
    task.add_workerids(2);
    task.set_checkerid(3);

    User u1;
    u1.set_id(1);
    u1.set_name("Alice");
    User u2;
    u2.set_id(2);
    u2.set_name("Bob");
    User u3;
    u3.set_id(3);
    u3.set_name("Charlie");
    state.users = {u1, u2, u3};

    state.tasks = {task};
    state.selectedTaskId = 42;

    bool statusChanged = false;
    bool workerAssigned = false;
    bool taskDeleted = false;

    Components::TaskDetailsCallbacks callbacks{
        .onChangeStatus =
            [&](std::int64_t id) {
                EXPECT_EQ(id, 42);
                statusChanged = true;
            },
        .onAssignWorker =
            [&](std::int64_t id) {
                EXPECT_EQ(id, 42);
                workerAssigned = true;
            },
        .onDeleteTask =
            [&](std::int64_t id) {
                EXPECT_EQ(id, 42);
                taskDeleted = true;
            },
    };

    auto comp = Components::CreateTaskDetailsPane(state, callbacks);
    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(20));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("#42"), std::string::npos);
    EXPECT_NE(rendered.find("Optimize SQL query indexing"), std::string::npos);
    EXPECT_NE(rendered.find("[ACTIVE]"), std::string::npos);
    EXPECT_NE(rendered.find("Alice"), std::string::npos);
    EXPECT_NE(rendered.find("Bob"), std::string::npos);
    EXPECT_NE(rendered.find("Charlie"), std::string::npos);
    EXPECT_NE(rendered.find("Add composite index"), std::string::npos);
    EXPECT_NE(rendered.find("Change Status"), std::string::npos);
}

TEST(UiComponentsTest, TaskDetailsPaneEmptyState) {
    AppState state;
    state.selectedTaskId = std::nullopt;

    auto comp = Components::CreateTaskDetailsPane(state);
    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(60), Dimension::Fixed(10));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("Select a task from the list"), std::string::npos);
}

TEST(UiComponentsTest, RemarksPaneRendersCategorizedFeed) {
    AppState state;
    state.selectedTaskId = 10;

    Comment c1;
    c1.set_id(1);
    c1.set_taskid(10);
    c1.set_content("General comment text");
    c1.set_type(CommentType::COMMENT_GENERAL);

    Comment c2;
    c2.set_id(2);
    c2.set_taskid(10);
    c2.set_content("Fix compilation errors!");
    c2.set_type(CommentType::COMMENT_REVISION_REASON);

    state.activeComments = {c1, c2};

    std::string sentComment;
    Components::RemarksCallbacks callbacks{
        .onAddComment =
            [&](std::int64_t id, const std::string& content) {
                EXPECT_EQ(id, 10);
                sentComment = content;
            },
    };

    auto comp = Components::CreateRemarksPane(state, callbacks);
    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(20));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("REMARKS & HISTORY"), std::string::npos);
    EXPECT_NE(rendered.find("[GENERAL]"), std::string::npos);
    EXPECT_NE(rendered.find("General comment text"), std::string::npos);
    EXPECT_NE(rendered.find("[REVISION REASON]"), std::string::npos);
    EXPECT_NE(rendered.find("Fix compilation errors!"), std::string::npos);
    EXPECT_NE(rendered.find("Send"), std::string::npos);
}

TEST(UiComponentsTest, TaskBoardViewRendersThreeColumns) {
    AppState state;
    Task t1;
    t1.set_id(1);
    t1.set_title("First task");
    t1.set_status(TaskStatus::ACTIVE);

    Task t2;
    t2.set_id(2);
    t2.set_title("Second task");
    t2.set_status(TaskStatus::IN_REVIEW);

    state.tasks = {t1, t2};
    state.selectedTaskId = 1;

    bool filterDispatched = false;
    auto dispatch = [&](const State::Action& a) {
        if (std::holds_alternative<State::SetStatusFilterAction>(a)) {
            filterDispatched = true;
        }
    };

    auto comp = Views::CreateTaskBoardView(state, dispatch, nullptr);
    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(160), Dimension::Fixed(25));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("FILTERS:"), std::string::npos);
    EXPECT_NE(rendered.find("ALL"), std::string::npos);
    EXPECT_NE(rendered.find("ACTIVE"), std::string::npos);
    EXPECT_NE(rendered.find("#1"), std::string::npos);
    EXPECT_NE(rendered.find("First task"), std::string::npos);
    EXPECT_NE(rendered.find("REMARKS & HISTORY"), std::string::npos);

    // Test 'f' hotkey cycles filter
    EXPECT_TRUE(comp->OnEvent(Event::Character('f')));
    EXPECT_TRUE(filterDispatched);
}

TEST(UiComponentsTest, AuthModalRendersAndValidates) {
    AppState state;
    state.session.serverHost = "127.0.0.1:50051";

    bool submitted = false;
    bool cancelled = false;

    Views::AuthModalCallbacks callbacks{
        .onSubmit = [&](const std::string&, const std::string&,
                        const std::string&) { submitted = true; },
        .onCancel = [&]() { cancelled = true; },
    };

    auto comp = Views::CreateAuthModal(state, callbacks);
    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(20));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("REGISTRATION / LOGIN"), std::string::npos);
    EXPECT_NE(rendered.find("127.0.0.1:50051"), std::string::npos);
    EXPECT_NE(rendered.find("Register & Connect"), std::string::npos);
    EXPECT_NE(rendered.find("Cancel"), std::string::npos);

    // Pressing Return with empty username should trigger validation error
    EXPECT_TRUE(comp->OnEvent(Event::Return));
    EXPECT_FALSE(submitted);

    elem = comp->Render();
    Render(screen, elem);
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("Username cannot be empty"), std::string::npos);

    // Pressing Escape triggers cancel
    EXPECT_TRUE(comp->OnEvent(Event::Escape));
    EXPECT_TRUE(cancelled);
}

TEST(UiComponentsTest, ReviewViewRendersInReviewTasksAndActions) {
    AppState state;
    Task t1;
    t1.set_id(1);
    t1.set_title("Active task");
    t1.set_status(TaskStatus::ACTIVE);

    Task t2;
    t2.set_id(2);
    t2.set_title("PR needs review");
    t2.set_status(TaskStatus::IN_REVIEW);

    state.tasks = {t1, t2};
    state.selectedTaskId = 2;

    auto comp =
        Views::CreateReviewView(state, [](const State::Action&) {}, nullptr);
    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(160), Dimension::Fixed(25));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("IN REVIEW (1)"), std::string::npos);
    EXPECT_NE(rendered.find("#2"), std::string::npos);
    EXPECT_NE(rendered.find("PR needs review"), std::string::npos);
    EXPECT_NE(rendered.find("REVIEW ACTIONS"), std::string::npos);
    EXPECT_NE(rendered.find("Approve & Verify"), std::string::npos);
    EXPECT_NE(rendered.find("Return for Revision:"), std::string::npos);
    EXPECT_NE(rendered.find("Reject with Reason"), std::string::npos);
    EXPECT_NE(rendered.find("REMARKS & HISTORY"), std::string::npos);
}

TEST(UiComponentsTest, CreateTaskViewRendersFormAndHandlesValidation) {
    AppState state;
    User u;
    u.set_id(1);
    u.set_name("Developer");
    state.users = {u};

    bool navigatedBack = false;
    auto dispatch = [&](const State::Action& a) {
        if (std::holds_alternative<State::NavigateToViewAction>(a)) {
            navigatedBack = true;
        }
    };

    auto comp = Views::CreateCreateTaskView(state, dispatch, nullptr);
    auto elem = comp->Render();
    auto screen = Screen::Create(Dimension::Fixed(100), Dimension::Fixed(30));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("CREATE NEW TASK"), std::string::npos);
    EXPECT_NE(rendered.find("Title:"), std::string::npos);
    EXPECT_NE(rendered.find("Description:"), std::string::npos);
    EXPECT_NE(rendered.find("Worker IDs:"), std::string::npos);
    EXPECT_NE(rendered.find("Available Team Members: [1] Developer"),
              std::string::npos);
    EXPECT_NE(rendered.find("Checker ID:"), std::string::npos);
    EXPECT_NE(rendered.find("Status:"), std::string::npos);
    EXPECT_NE(rendered.find("Due Date:"), std::string::npos);
    EXPECT_NE(rendered.find("Create Task"), std::string::npos);
    EXPECT_NE(rendered.find("Cancel"), std::string::npos);

    // Escape triggers navigation back
    EXPECT_TRUE(comp->OnEvent(Event::Escape));
    EXPECT_TRUE(navigatedBack);
}

TEST(UiComponentsTest, TimeUtilsParseDate) {
    auto valid = Common::ParseDate("2026-11-20");
    ASSERT_TRUE(valid.has_value());
    EXPECT_GT(valid->seconds(), 0);

    auto invalid = Common::ParseDate("not-a-date");
    EXPECT_FALSE(invalid.has_value());

    auto empty = Common::ParseDate("");
    EXPECT_FALSE(empty.has_value());
}

TEST(UiComponentsTest, TerminalTooSmallWarningRendersProperly) {
    auto elem = RenderTerminalTooSmallWarning(60, 15, 80, 24);
    auto screen = Screen::Create(Dimension::Fixed(60), Dimension::Fixed(15));
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("WARNING: TERMINAL WINDOW TOO SMALL"),
              std::string::npos);
    EXPECT_NE(rendered.find("Current size: 60x15"), std::string::npos);
    EXPECT_NE(rendered.find("Minimum required size: 80x24"), std::string::npos);
}

} // namespace Aquarium::UI::Tests
