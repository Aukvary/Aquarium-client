#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <iostream>
#include <string>

#include <TaskStoreService.grpc.pb.h>
#include <UserStoreService.grpc.pb.h>
#include <UtilsService.grpc.pb.h>

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--smoke-test") {
        std::cout << "Aquarium client smoke test passed." << std::endl;
        return 0;
    }

    auto screen = ftxui::ScreenInteractive::Fullscreen();

    auto renderer = ftxui::Renderer([&] {
        return ftxui::vbox({
                   ftxui::text("Aquarium TUI Client") | ftxui::bold |
                       ftxui::center,
                   ftxui::separator(),
                   ftxui::text("Phase 1: Project Skeleton & Build System") |
                       ftxui::center,
                   ftxui::separator(),
                   ftxui::text("Press 'q' or Esc to exit.") | ftxui::dim |
                       ftxui::center,
               }) |
               ftxui::border;
    });

    auto catch_event = ftxui::CatchEvent(renderer, [&](ftxui::Event event) {
        if (event == ftxui::Event::Character('q') ||
            event == ftxui::Event::Escape) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(catch_event);
    return 0;
}
