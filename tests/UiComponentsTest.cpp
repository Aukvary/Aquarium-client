#include "Common/TimeUtils.hpp"
#include "State/AppState.hpp"
#include "State/Reducer.hpp"
#include "UI/Components/RemarksPane.hpp"
#include "UI/Components/StatusBar.hpp"
#include "UI/Components/TaskDetailsPane.hpp"
#include "UI/NavigationSidebar.hpp"
#include "UI/UIHelpers.hpp"
#include "UI/Views/AuthModal.hpp"
#include "UI/Views/CreateTaskView.hpp"
#include "UI/Views/ProjectModal.hpp"
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

    bool taskPassed = false;
    bool archiveToggled = false;
    bool workerAssigned = false;
    bool taskDeleted = false;

    Components::TaskDetailsCallbacks callbacks{
        .onPassTask =
            [&](std::int64_t id) {
                EXPECT_EQ(id, 42);
                taskPassed = true;
            },
        .onToggleArchive =
            [&](std::int64_t id) {
                EXPECT_EQ(id, 42);
                archiveToggled = true;
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
    EXPECT_NE(rendered.find("Pass"), std::string::npos);
    EXPECT_NE(rendered.find("Toggle Archive"), std::string::npos);

    EXPECT_TRUE(comp->OnEvent(Event::Return));
    EXPECT_TRUE(taskPassed);

    EXPECT_TRUE(comp->OnEvent(Event::ArrowRight));
    EXPECT_TRUE(comp->OnEvent(Event::Return));
    EXPECT_TRUE(archiveToggled);
    EXPECT_FALSE(workerAssigned);
    EXPECT_FALSE(taskDeleted);
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

    // Newer remark (c2) must be rendered above older remark (c1).
    auto posNewer = rendered.find("Fix compilation errors!");
    auto posOlder = rendered.find("General comment text");
    ASSERT_NE(posNewer, std::string::npos);
    ASSERT_NE(posOlder, std::string::npos);
    EXPECT_LT(posNewer, posOlder);
}

TEST(UiComponentsTest, RemarksPaneSupportsScrollingAndResetsOnNewRemark) {
    AppState state;
    state.selectedTaskId = 10;

    for (int i = 1; i <= 10; ++i) {
        Comment c;
        c.set_id(i);
        c.set_taskid(10);
        c.set_content("Remark item #" + std::to_string(i));
        c.set_type(CommentType::COMMENT_GENERAL);
        state.activeComments.push_back(c);
    }

    auto comp = Components::CreateRemarksPane(state);
    auto screen = Screen::Create(Dimension::Fixed(60), Dimension::Fixed(16));
    Render(screen, comp->Render());
    std::string rendered = screen.ToString();

    // Newest remark (#10) is visible at the top; oldest (#1) is off-screen.
    EXPECT_NE(rendered.find("Remark item #10"), std::string::npos);
    EXPECT_EQ(rendered.find("Remark item #1 "), std::string::npos);

    // Scroll down with keyboard while feed is focused.
    for (int i = 0; i < 40; ++i) {
        EXPECT_TRUE(comp->OnEvent(Event::ArrowDown));
    }
    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();

    EXPECT_EQ(rendered.find("Remark item #10"), std::string::npos);
    EXPECT_NE(rendered.find("Remark item #1"), std::string::npos);

    // Scroll back up with mouse wheel inside the feed area.
    Mouse wheelUp;
    wheelUp.button = Mouse::WheelUp;
    wheelUp.x = 10;
    wheelUp.y = 6;
    for (int i = 0; i < 15; ++i) {
        EXPECT_TRUE(comp->OnEvent(Event::Mouse("", wheelUp)));
    }
    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();

    EXPECT_NE(rendered.find("Remark item #10"), std::string::npos);

    // Scroll down to bottom with End, then add a new remark -> resets to top.
    EXPECT_TRUE(comp->OnEvent(Event::End));
    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_EQ(rendered.find("Remark item #10"), std::string::npos);

    Comment newest;
    newest.set_id(11);
    newest.set_taskid(10);
    newest.set_content("Brand new remark #11");
    newest.set_type(CommentType::COMMENT_GENERAL);
    state.activeComments.push_back(newest);

    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("Brand new remark #11"), std::string::npos);
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

TEST(UiComponentsTest, CreateTaskViewDynamicSessionAndAuthValidation) {
    AppState state;
    state.session.userId = 0; // Unauthenticated at startup

    auto comp = Views::CreateCreateTaskView(
        state, [](const State::Action&) {}, nullptr);

    // Type a title
    for (char c : std::string("Task Alpha")) {
        comp->OnEvent(Event::Character(c));
    }

    // Navigate down to 'Create Task' button
    for (int i = 0; i < 6; ++i) {
        comp->OnEvent(Event::ArrowDown);
    }

    // Press Return on Create Task button while unauthenticated
    comp->OnEvent(Event::Return);

    auto screen = Screen::Create(Dimension::Fixed(100), Dimension::Fixed(30));
    auto elem = comp->Render();
    Render(screen, elem);
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("Cannot create task: unauthenticated user"),
              std::string::npos);

    // Now dynamically authenticate (as happens when AuthModal finishes)
    state.session.userId = 2;
    state.session.userName = "aukvary";

    // Press Return again: view must dynamically see updated userId
    comp->OnEvent(Event::Return);
    elem = comp->Render();
    Render(screen, elem);
    rendered = screen.ToString();

    EXPECT_EQ(rendered.find("Cannot create task: unauthenticated user"),
              std::string::npos);
    EXPECT_NE(rendered.find("Cannot create task: offline or client missing"),
              std::string::npos);
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

TEST(UiComponentsTest, TaskBoardViewSearchQueryInputAndBackspaceRestoresTasks) {
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

    auto dispatch = [&](const State::Action& a) {
        state = State::Reducer::Reduce(state, a);
    };

    auto comp = Views::CreateTaskBoardView(state, dispatch, nullptr);

    // Initial render: both tasks visible
    auto screen = Screen::Create(Dimension::Fixed(160), Dimension::Fixed(25));
    Render(screen, comp->Render());
    std::string rendered = screen.ToString();
    EXPECT_NE(rendered.find("First task"), std::string::npos);
    EXPECT_NE(rendered.find("Second task"), std::string::npos);

    // Press '/' to focus search field
    EXPECT_TRUE(comp->OnEvent(Event::Character('/')));
    EXPECT_TRUE(state.filter.searchQuery.empty());

    // Type "First" into search
    for (char c : std::string("First")) {
        comp->OnEvent(Event::Character(c));
    }
    EXPECT_EQ(state.filter.searchQuery, "First");

    // Re-render: only First task should be visible
    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("First task"), std::string::npos);
    EXPECT_EQ(rendered.find("Second task"), std::string::npos);

    // Erase characters using Backspace until empty
    for (size_t i = 0; i < 5; ++i) {
        comp->OnEvent(Event::Backspace);
    }
    EXPECT_TRUE(state.filter.searchQuery.empty());

    // Re-render: BOTH tasks must be restored and displayed!
    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("First task"), std::string::npos);
    EXPECT_NE(rendered.find("Second task"), std::string::npos);

    // Press Return to unfocus search field
    comp->OnEvent(Event::Return);

    // Navigate between tasks using 'j' (down) and 'k' (up)
    EXPECT_TRUE(comp->OnEvent(Event::Character('j')));
    EXPECT_EQ(state.selectedTaskId, 2);

    EXPECT_TRUE(comp->OnEvent(Event::Character('k')));
    EXPECT_EQ(state.selectedTaskId, 1);
}

TEST(UiComponentsTest, ProjectModalRendersProjectsAndHandlesSelectAddDelete) {
    AppState state;
    std::vector<Views::ProjectEntryView> projects = {
        {.server = "alpha:8081", .userId = 1, .userName = "alice"},
        {.server = "beta:50051", .userId = 2, .userName = "bob"},
    };
    std::string selectedServer;
    bool addClicked = false;
    std::string deletedServer;
    bool deletedForceLocal = false;
    std::string errorMsg;

    Views::ProjectModalCallbacks callbacks{
        .getProjects = [&] { return projects; },
        .onSelectProject = [&](const std::string& s) { selectedServer = s; },
        .onAddProject = [&] { addClicked = true; },
        .onDeleteProject =
            [&](const std::string& s, bool forceLocal) {
                deletedServer = s;
                deletedForceLocal = forceLocal;
            },
        .getErrorMessage = [&] { return errorMsg; },
        .isBusy = [] { return false; },
        .onClearError = [&] { errorMsg.clear(); },
    };

    auto comp = Views::CreateProjectModal(state, callbacks);
    auto screen = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(22));
    Render(screen, comp->Render());
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("SELECT PROJECT"), std::string::npos);
    EXPECT_NE(rendered.find("alpha:8081"), std::string::npos);
    EXPECT_NE(rendered.find("alice"), std::string::npos);
    EXPECT_NE(rendered.find("beta:50051"), std::string::npos);
    EXPECT_NE(rendered.find("Connect"), std::string::npos);
    EXPECT_NE(rendered.find("Add Project"), std::string::npos);
    EXPECT_NE(rendered.find("Delete Project"), std::string::npos);

    // Move down with 'j' to beta:50051 and press Return to connect
    EXPECT_TRUE(comp->OnEvent(Event::Character('j')));
    EXPECT_TRUE(comp->OnEvent(Event::Return));
    EXPECT_EQ(selectedServer, "beta:50051");

    // Press 'a' to add project
    EXPECT_TRUE(comp->OnEvent(Event::Character('a')));
    EXPECT_TRUE(addClicked);

    // Press 'd' to open delete confirmation for beta:50051
    EXPECT_TRUE(comp->OnEvent(Event::Character('d')));
    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("DELETE PROJECT"), std::string::npos);
    EXPECT_NE(rendered.find("beta:50051"), std::string::npos);
    EXPECT_NE(rendered.find("bob"), std::string::npos);
    EXPECT_NE(rendered.find("will be deleted from the server"),
              std::string::npos);

    // Confirm delete (focused on Confirm Delete button)
    EXPECT_TRUE(comp->OnEvent(Event::Return));
    EXPECT_EQ(deletedServer, "beta:50051");
    EXPECT_FALSE(deletedForceLocal);

    // Simulate server failure: Remove Locally button appears
    errorMsg = "Server unavailable";
    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("Server unavailable"), std::string::npos);
    EXPECT_NE(rendered.find("Remove Locally"), std::string::npos);

    // Escape cancels delete confirmation back to project list
    EXPECT_TRUE(comp->OnEvent(Event::Escape));
    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("SELECT PROJECT"), std::string::npos);
    EXPECT_TRUE(errorMsg.empty());
}

TEST(UiComponentsTest, ProjectModalRendersEmptyState) {
    AppState state;
    Views::ProjectModalCallbacks callbacks;
    callbacks.getProjects = [] {
        return std::vector<Views::ProjectEntryView>{};
    };

    auto comp = Views::CreateProjectModal(state, callbacks);
    auto screen = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(20));
    Render(screen, comp->Render());
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("No saved projects in config"), std::string::npos);
    EXPECT_NE(rendered.find("Add Project"), std::string::npos);
    // Escape on empty list is consumed without closing modal
    EXPECT_TRUE(comp->OnEvent(Event::Escape));
}

TEST(UiComponentsTest, TaskBoardViewScopeToggleSwitchesAllAndMyTasks) {
    AppState state;
    state.session.userId = 1;

    Task checkerOnlyTask;
    checkerOnlyTask.set_id(1);
    checkerOnlyTask.set_title("Other task");
    checkerOnlyTask.set_authorid(1);
    checkerOnlyTask.add_workerids(2);
    checkerOnlyTask.set_checkerid(1);
    checkerOnlyTask.set_status(TaskStatus::ACTIVE);

    Task myWorkerTask;
    myWorkerTask.set_id(2);
    myWorkerTask.set_title("My worker task");
    myWorkerTask.add_workerids(1);
    myWorkerTask.set_status(TaskStatus::ACTIVE);

    state.tasks = {checkerOnlyTask, myWorkerTask};
    state.selectedTaskId = 1;

    auto dispatch = [&](const State::Action& a) {
        state = State::Reducer::Reduce(state, a);
    };

    auto comp = Views::CreateTaskBoardView(state, dispatch, nullptr);
    auto screen = Screen::Create(Dimension::Fixed(160), Dimension::Fixed(25));
    Render(screen, comp->Render());
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("[Show: All Tasks]"), std::string::npos);
    EXPECT_NE(rendered.find("Other task"), std::string::npos);
    EXPECT_NE(rendered.find("My worker task"), std::string::npos);

    // Scope toggle button is the initial focus in TaskBoardView: press Return
    EXPECT_TRUE(comp->OnEvent(Event::Return));
    EXPECT_TRUE(state.filter.onlyMyTasks);

    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("[Show: My Tasks]"), std::string::npos);
    EXPECT_EQ(rendered.find("Other task"), std::string::npos);
    EXPECT_NE(rendered.find("My worker task"), std::string::npos);

    // Hotkey 'm' toggles back to all tasks
    EXPECT_TRUE(comp->OnEvent(Event::Character('m')));
    EXPECT_FALSE(state.filter.onlyMyTasks);

    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("[Show: All Tasks]"), std::string::npos);
    EXPECT_NE(rendered.find("Other task"), std::string::npos);
}

TEST(UiComponentsTest, ReviewViewScopeToggleFiltersByCurrentUser) {
    AppState state;
    state.session.userId = 1;
    state.currentView = ViewType::Review;

    Task workerOnlyReview;
    workerOnlyReview.set_id(10);
    workerOnlyReview.set_title("Other review");
    workerOnlyReview.add_workerids(1);
    workerOnlyReview.set_checkerid(3);
    workerOnlyReview.set_status(TaskStatus::IN_REVIEW);

    Task myCheckReview;
    myCheckReview.set_id(11);
    myCheckReview.set_title("My check review");
    myCheckReview.add_workerids(2);
    myCheckReview.set_checkerid(1);
    myCheckReview.set_status(TaskStatus::IN_REVIEW);

    state.tasks = {workerOnlyReview, myCheckReview};
    state.selectedTaskId = 10;

    auto dispatch = [&](const State::Action& a) {
        state = State::Reducer::Reduce(state, a);
    };

    auto comp = Views::CreateReviewView(state, dispatch, nullptr);
    auto screen = Screen::Create(Dimension::Fixed(160), Dimension::Fixed(25));
    Render(screen, comp->Render());
    std::string rendered = screen.ToString();

    EXPECT_NE(rendered.find("IN REVIEW (2)"), std::string::npos);
    EXPECT_NE(rendered.find("[Show: All Tasks]"), std::string::npos);
    EXPECT_NE(rendered.find("Other review"), std::string::npos);
    EXPECT_NE(rendered.find("My check review"), std::string::npos);

    // Click/activate the scope toggle button via Return
    EXPECT_TRUE(comp->OnEvent(Event::Return));
    EXPECT_TRUE(state.filter.onlyMyTasks);

    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("IN REVIEW (1)"), std::string::npos);
    EXPECT_NE(rendered.find("[Show: My Tasks]"), std::string::npos);
    EXPECT_EQ(rendered.find("Other review"), std::string::npos);
    EXPECT_NE(rendered.find("My check review"), std::string::npos);

    // Hotkey 'm' switches back to all tasks
    EXPECT_TRUE(comp->OnEvent(Event::Character('m')));
    EXPECT_FALSE(state.filter.onlyMyTasks);

    screen.Clear();
    Render(screen, comp->Render());
    rendered = screen.ToString();
    EXPECT_NE(rendered.find("IN REVIEW (2)"), std::string::npos);
    EXPECT_NE(rendered.find("Other review"), std::string::npos);
}

} // namespace Aquarium::UI::Tests
