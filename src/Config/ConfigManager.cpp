#include "ConfigManager.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>

namespace Aquarium::Config {

void to_json(nlohmann::json& j, const ServerEntry& entry) {
    j = nlohmann::json{
        {"user_id", entry.userId},
        {"user_name", entry.userName},
        {"auth_token", entry.authToken},
    };
}

void from_json(const nlohmann::json& j, ServerEntry& entry) {
    if (j.contains("user_id") && j["user_id"].is_number()) {
        entry.userId = j["user_id"].get<std::int64_t>();
    }
    if (j.contains("user_name") && j["user_name"].is_string()) {
        entry.userName = j["user_name"].get<std::string>();
    }
    if (j.contains("auth_token") && j["auth_token"].is_string()) {
        entry.authToken = j["auth_token"].get<std::string>();
    }
}

std::optional<ServerEntry> ClientConfig::GetActiveSession() const {
    auto it = servers.find(activeServer);
    if (it != servers.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ClientConfig::SetActiveSession(const std::string& server,
                                    const ServerEntry& entry) {
    activeServer = server;
    servers[server] = entry;
}

void to_json(nlohmann::json& j, const ClientConfig& config) {
    j = nlohmann::json{
        {"active_server", config.activeServer},
        {"servers", config.servers},
    };
}

void from_json(const nlohmann::json& j, ClientConfig& config) {
    if (j.contains("active_server") && j["active_server"].is_string()) {
        config.activeServer = j["active_server"].get<std::string>();
    }
    if (j.contains("servers") && j["servers"].is_object()) {
        config.servers =
            j["servers"].get<std::unordered_map<std::string, ServerEntry>>();
    }
}

ConfigManager::ConfigManager(std::filesystem::path configPath)
    : _configPath(std::move(configPath)) {}

std::filesystem::path ConfigManager::GetDefaultConfigPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "aquarium" / "config.json";
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "aquarium" /
               "config.json";
    }

    return std::filesystem::current_path() / "config.json";
}

CliOverrides
ConfigManager::ParseCommandLine(std::span<const std::string_view> args) {
    CliOverrides overrides;
    for (std::size_t i = 1; i < args.size(); ++i) {
        std::string_view arg = args[i];
        if (arg == "--server" || arg == "-s") {
            if (i + 1 < args.size()) {
                overrides.server = std::string(args[++i]);
            }
        } else if (arg == "--token" || arg == "-t") {
            if (i + 1 < args.size()) {
                overrides.token = std::string(args[++i]);
            }
        } else if (arg == "--smoke-test") {
            overrides.smokeTest = true;
        } else if (arg == "--help" || arg == "-h") {
            overrides.showHelp = true;
        }
    }
    return overrides;
}

CliOverrides
ConfigManager::ParseCommandLine(int argc,
                                std::span<const std::string_view> argv) {
    if (argc <= 0) {
        return {};
    }
    const auto count = std::min(static_cast<std::size_t>(argc), argv.size());
    return ParseCommandLine(argv.first(count));
}

void ConfigManager::ApplyOverrides(const CliOverrides& overrides) {
    _overrides = overrides;

    if (!_overrides.server.has_value()) {
        const char* envServer = std::getenv("AQUARIUM_SERVER");
        if (envServer != nullptr && *envServer != '\0') {
            _overrides.server = std::string(envServer);
        }
    }

    if (!_overrides.token.has_value()) {
        const char* envToken = std::getenv("AQUARIUM_TOKEN");
        if (envToken != nullptr && *envToken != '\0') {
            _overrides.token = std::string(envToken);
        }
    }
}

bool ConfigManager::Load() {
    std::error_code ec;
    if (!std::filesystem::exists(_configPath, ec)) {
        auto parent = _configPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (!ec) {
                std::filesystem::permissions(
                    parent, std::filesystem::perms::owner_all,
                    std::filesystem::perm_options::replace, ec);
            }
        }
        _config = ClientConfig{};
        return Save();
    }

    std::ifstream file(_configPath);
    if (!file.is_open()) {
        return false;
    }

    auto json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded()) {
        return false;
    }

    try {
        _config = json.get<ClientConfig>();
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool ConfigManager::Save() {
    std::error_code ec;
    auto parent = _configPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }

    auto tempPath = _configPath;
    tempPath += ".tmp";

    {
        std::ofstream file(tempPath, std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json j = _config;
        file << j.dump(4) << "\n";
        if (!file.good()) {
            return false;
        }
    }

    std::filesystem::permissions(tempPath,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);

    std::filesystem::rename(tempPath, _configPath, ec);
    return !ec;
}

const ClientConfig& ConfigManager::GetConfig() const noexcept {
    return _config;
}

ClientConfig& ConfigManager::GetMutableConfig() noexcept {
    return _config;
}

std::string ConfigManager::GetActiveServer() const {
    if (_overrides.server.has_value()) {
        return *_overrides.server;
    }
    return _config.activeServer;
}

std::string ConfigManager::GetActiveToken() const {
    if (_overrides.token.has_value()) {
        return *_overrides.token;
    }
    auto session = GetActiveSession();
    if (session.has_value()) {
        return session->authToken;
    }
    return "";
}

std::optional<ServerEntry> ConfigManager::GetActiveSession() const {
    auto currentServer = GetActiveServer();
    auto it = _config.servers.find(currentServer);
    if (it != _config.servers.end()) {
        ServerEntry session = it->second;
        if (_overrides.token.has_value()) {
            session.authToken = *_overrides.token;
        }
        return session;
    }

    if (_overrides.token.has_value()) {
        ServerEntry session;
        session.authToken = *_overrides.token;
        return session;
    }

    return std::nullopt;
}

bool ConfigManager::UpdateActiveSession(std::int64_t userId,
                                        std::string userName,
                                        std::string token) {
    auto currentServer = GetActiveServer();
    _config.activeServer = currentServer;
    _config.servers[currentServer] = ServerEntry{
        .userId = userId,
        .userName = std::move(userName),
        .authToken = std::move(token),
    };
    return Save();
}

std::vector<SavedProject> ConfigManager::GetSavedProjects() const {
    std::vector<SavedProject> projects;
    projects.reserve(_config.servers.size());
    for (const auto& [server, entry] : _config.servers) {
        projects.push_back(SavedProject{
            .server = server,
            .entry = entry,
        });
    }
    std::ranges::sort(projects,
                      [](const SavedProject& lhs, const SavedProject& rhs) {
                          return lhs.server < rhs.server;
                      });
    return projects;
}

bool ConfigManager::SaveProjectSession(const std::string& server,
                                       std::int64_t userId,
                                       std::string userName,
                                       std::string token) {
    _overrides.server.reset();
    _overrides.token.reset();
    _config.activeServer = server;
    _config.servers[server] = ServerEntry{
        .userId = userId,
        .userName = std::move(userName),
        .authToken = std::move(token),
    };
    return Save();
}

bool ConfigManager::SwitchActiveProject(const std::string& server) {
    if (!_config.servers.contains(server)) {
        return false;
    }
    _overrides.server.reset();
    _overrides.token.reset();
    _config.activeServer = server;
    return Save();
}

bool ConfigManager::RemoveProject(const std::string& server) {
    auto erased = _config.servers.erase(server);
    if (erased == 0) {
        return false;
    }
    if (_overrides.server.has_value() && *_overrides.server == server) {
        _overrides.server.reset();
        _overrides.token.reset();
    }
    if (_config.activeServer == server) {
        _config.activeServer.clear();
    }
    return Save();
}

const std::filesystem::path& ConfigManager::GetConfigPath() const noexcept {
    return _configPath;
}

} // namespace Aquarium::Config
