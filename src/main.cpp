#include "Config/ConfigManager.hpp"
#include "Grpc/BackendClient.hpp"
#include "UI/MainScreen.hpp"

#include <cstdlib>
#include <iostream>
#include <ranges>
#include <span>
#include <string_view>

int main(int argc, char* argv[]) {
    using Aquarium::Config::ConfigManager;
    using Aquarium::Grpc::BackendClient;
    using Aquarium::UI::MainScreen;

    std::span<char*> args_s(argv, static_cast<std::size_t>(argc));
    auto overrides = ConfigManager::ParseCommandLine(
        args_s | std::ranges::views::drop(1) |
        std::ranges::views::transform(
            [](char* raw) -> std::string_view { return raw; }));

    if (overrides.showHelp) {
        std::cout << "Usage: aquarium-client [options]\n"
                     "Options:\n"
                     "  -s, --server <host:port>  Target server address\n"
                     "  -t, --token <token>       Authentication token\n"
                     "  -h, --help                Show this help message\n";
        return 0;
    }

    ConfigManager configManager;
    configManager.Load();
    configManager.ApplyOverrides(overrides);

    MainScreen mainScreen(configManager);
    mainScreen.Run();

    return 0;
}
