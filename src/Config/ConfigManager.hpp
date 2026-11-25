#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Aquarium::Config {

struct ServerEntry {
    std::int64_t userId{0};
    std::string userName;
    std::string authToken;

    bool operator==(const ServerEntry& other) const = default;
};

void to_json(nlohmann::json& j, const ServerEntry& entry);
void from_json(const nlohmann::json& j, ServerEntry& entry);

struct ClientConfig {
    std::string activeServer{"localhost:50051"};
    std::unordered_map<std::string, ServerEntry> servers;

    [[nodiscard]] std::optional<ServerEntry> GetActiveSession() const;
    void SetActiveSession(const std::string& server, const ServerEntry& entry);

    bool operator==(const ClientConfig& other) const = default;
};

void to_json(nlohmann::json& j, const ClientConfig& config);
void from_json(const nlohmann::json& j, ClientConfig& config);

struct CliOverrides {
    std::optional<std::string> server;
    std::optional<std::string> token;
    bool smokeTest{false};
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
    template <std::size_t COUNT>
    [[nodiscard]] static CliOverrides
    ParseCommandLine(int argc, std::array<std::string_view, COUNT> argv);

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

    [[nodiscard]] const std::filesystem::path& GetConfigPath() const noexcept;
};

} // namespace Aquarium::Config
