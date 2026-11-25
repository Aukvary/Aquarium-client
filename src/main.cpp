#include "Config/ConfigManager.hpp"
#include "Grpc/BackendClient.hpp"
#include "UI/MainScreen.hpp"

#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
    using Aquarium::Config::ConfigManager;
    using Aquarium::Grpc::BackendClient;
    using Aquarium::UI::MainScreen;

    std::vector<std::string_view> args;
    if (argc > 0 && argv != nullptr) {
        const std::span<char*> rawArgs(argv, static_cast<std::size_t>(argc));
        args.assign(rawArgs.begin(), rawArgs.end());
    }
    auto overrides = ConfigManager::ParseCommandLine(args);
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

    auto configManager = std::make_shared<ConfigManager>();
    configManager->Load();
    configManager->ApplyOverrides(overrides);

    auto backendClient = std::make_shared<BackendClient>(
        configManager->GetActiveServer(), configManager->GetActiveToken());

    MainScreen mainScreen(configManager, backendClient);
    mainScreen.Run();

    return 0;
}
