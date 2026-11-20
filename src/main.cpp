#include "Config/ConfigManager.hpp"

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
    using Aquarium::Config::ConfigManager;

    auto overrides = ConfigManager::ParseCommandLine(argc, argv);
    if (overrides.showHelp) {
        std::cout << "Usage: aquarium-client [options]\n"
                  << "Options:\n"
                  << "  -s, --server <host:port>  Target server address\n"
                  << "  -t, --token <token>       Authentication token\n"
                  << "  --smoke-test              Run smoke test and exit\n"
                  << "  -h, --help                Show this help message\n";
        return 0;
    }

    if (overrides.smokeTest) {
        std::cout << "Aquarium client smoke test passed." << std::endl;
        return 0;
    }

    ConfigManager configManager;
    configManager.Load();
    configManager.ApplyOverrides(overrides);

    std::string activeServer = configManager.GetActiveServer();
    auto session = configManager.GetActiveSession();
    std::string userDisplay =
        session.has_value() && !session->userName.empty()
            ? session->userName + " (ID: " + std::to_string(session->userId) +
                  ")"
            : "Unauthenticated";

    auto screen = ftxui::ScreenInteractive::Fullscreen();

    auto renderer = ftxui::Renderer([&] {
        return ftxui::vbox({
                   ftxui::text("Aquarium TUI Client") | ftxui::bold |
                       ftxui::center,
                   ftxui::separator(),
                   ftxui::hbox({
                       ftxui::text(" Active Server: ") | ftxui::bold,
                       ftxui::text(activeServer) |
                           ftxui::color(ftxui::Color::Cyan),
                       ftxui::filler(),
                       ftxui::text(" User: ") | ftxui::bold,
                       ftxui::text(userDisplay) |
                           ftxui::color(ftxui::Color::Yellow),
                       ftxui::text(" "),
                   }),
                   ftxui::separator(),
                   ftxui::text("Phase 2: Configuration & Session Storage") |
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
