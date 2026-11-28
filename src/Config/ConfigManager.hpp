#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Aquarium::Config {

struct ServerEntry {
    std::int64_t userId{0};
    std::string userName;
    std::string authToken;

    bool operator==(const ServerEntry& other) const = default;
};

struct SavedProject {
    std::string server;
    ServerEntry entry;

    bool operator==(const SavedProject& other) const = default;
};

void to_json(nlohmann::json& j, const ServerEntry& entry);
void from_json(const nlohmann::json& j, ServerEntry& entry);

struct ClientConfig {
    std::string activeServer{"localhost:50051"};
    std::unordered_map<std::string, ServerEntry> servers;

    [[nodiscard]] std::optional<ServerEntry> GetActiveSession() const;
    void SetActiveSession(const char* server, const ServerEntry& entry);
    void SetActiveSession(std::string_view server, const ServerEntry& entry);
    void SetActiveSession(std::string&& server, const ServerEntry& entry);

    bool operator==(const ClientConfig& other) const = default;
};

void to_json(nlohmann::json& j, const ClientConfig& config);
void from_json(const nlohmann::json& j, ClientConfig& config);

struct CliOverrides {
    std::optional<std::string_view> server;
    std::optional<std::string_view> token;
    bool showHelp{false};
};

class ConfigManager {
private:
    std::filesystem::path _configPath;
    ClientConfig _config;
    CliOverrides _overrides;

public:
    explicit ConfigManager(
        std::filesystem::path configPath = GetDefaultConfigPath());

    [[nodiscard]] static std::filesystem::path GetDefaultConfigPath();
    template <std::ranges::input_range Range>
        requires std::convertible_to<std::ranges::range_reference_t<Range>,
                                     std::string_view>
    [[nodiscard]] static CliOverrides ParseCommandLine(Range&& args) {
        CliOverrides overrides;
        for (auto it = std::ranges::begin(args); it != std::ranges::end(args);
             ++it) {
            std::string_view arg = *it;
            if (arg == "--server" || arg == "-s") {
                if (++it == std::ranges::end(args)) {
                    break;
                }
                overrides.server = *it;
            } else if (arg == "--token" || arg == "-t") {
                if (++it == std::ranges::end(args)) {
                    break;
                }
                overrides.token = *it;
            } else if (arg == "--help" || arg == "-h") {
                overrides.showHelp = true;
            }
        }
        return overrides;
    }

    bool Load();
    bool Save();

    void ApplyOverrides(const CliOverrides& overrides);

    [[nodiscard]] const ClientConfig& GetConfig() const noexcept;
    [[nodiscard]] ClientConfig& GetMutableConfig() noexcept;

    [[nodiscard]] std::string GetActiveServer() const;
    [[nodiscard]] std::string GetActiveToken() const;
    [[nodiscard]] std::optional<ServerEntry> GetActiveSession() const;

    bool UpdateActiveSession(std::int64_t userId,
                             std::string userName,
                             std::string token);

    [[nodiscard]] std::vector<SavedProject> GetSavedProjects() const;
    bool SaveProjectSession(const std::string& server,
                            std::int64_t userId,
                            std::string userName,
                            std::string token);
    bool SwitchActiveProject(const std::string& server);
    bool RemoveProject(const std::string& server);

    [[nodiscard]] const std::filesystem::path& GetConfigPath() const noexcept;
};

} // namespace Aquarium::Config
