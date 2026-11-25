#include "Config/ConfigManager.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Aquarium::Config::Tests {

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        _tempDir = std::filesystem::temp_directory_path() /
                   ("aquarium_cfg_test_" + std::to_string(std::rand()));
        std::filesystem::create_directories(_tempDir);
        _testConfigPath = _tempDir / "nested" / "config.json";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(_tempDir, ec);
        unsetenv("AQUARIUM_SERVER");
        unsetenv("AQUARIUM_TOKEN");
    }

    std::filesystem::path _tempDir;
    std::filesystem::path _testConfigPath;
};

TEST_F(ConfigTest, SerializationAndDeserialization) {
    ClientConfig original;
    original.activeServer = "aquarium.internal:50051";
    original.servers["aquarium.internal:50051"] = ServerEntry{
        .userId = 101,
        .userName = "charlie",
        .authToken = "bearer_token_xyz_999",
    };
    original.servers["backup:50051"] = ServerEntry{
        .userId = 102,
        .userName = "charlie_backup",
        .authToken = "backup_tok_111",
    };

    nlohmann::json j = original;
    EXPECT_EQ(j["active_server"], "aquarium.internal:50051");
    EXPECT_TRUE(j["servers"].is_object());
    EXPECT_EQ(j["servers"]["aquarium.internal:50051"]["user_id"], 101);
    EXPECT_EQ(j["servers"]["aquarium.internal:50051"]["user_name"], "charlie");
    EXPECT_EQ(j["servers"]["aquarium.internal:50051"]["auth_token"],
              "bearer_token_xyz_999");

    ClientConfig deserialized = j.get<ClientConfig>();
    EXPECT_EQ(deserialized.activeServer, original.activeServer);
    EXPECT_EQ(deserialized.servers.size(), 2);
    EXPECT_EQ(deserialized, original);

    auto session = deserialized.GetActiveSession();
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->userId, 101);
    EXPECT_EQ(session->userName, "charlie");
    EXPECT_EQ(session->authToken, "bearer_token_xyz_999");
}

TEST_F(ConfigTest, AutoCreatesDirectoryAndDefaultConfigWhenMissing) {
    EXPECT_FALSE(std::filesystem::exists(_testConfigPath));

    ConfigManager manager(_testConfigPath);
    bool loadResult = manager.Load();

    EXPECT_TRUE(loadResult);
    EXPECT_TRUE(std::filesystem::exists(_testConfigPath));
    EXPECT_EQ(manager.GetActiveServer(), "localhost:50051");
    EXPECT_EQ(manager.GetActiveToken(), "");
    EXPECT_FALSE(manager.GetActiveSession().has_value());

    std::ifstream file(_testConfigPath);
    ASSERT_TRUE(file.is_open());
    auto json = nlohmann::json::parse(file);
    EXPECT_EQ(json["active_server"], "localhost:50051");
    EXPECT_TRUE(json["servers"].is_object());
}

TEST_F(ConfigTest, AtomicSaveAndReloadSession) {
    ConfigManager manager1(_testConfigPath);
    ASSERT_TRUE(manager1.Load());

    EXPECT_TRUE(manager1.UpdateActiveSession(42, "bob", "secret_token_12345"));
    EXPECT_EQ(manager1.GetActiveServer(), "localhost:50051");
    EXPECT_EQ(manager1.GetActiveToken(), "secret_token_12345");

    auto session1 = manager1.GetActiveSession();
    ASSERT_TRUE(session1.has_value());
    EXPECT_EQ(session1->userId, 42);
    EXPECT_EQ(session1->userName, "bob");
    EXPECT_EQ(session1->authToken, "secret_token_12345");

    ConfigManager manager2(_testConfigPath);
    ASSERT_TRUE(manager2.Load());

    EXPECT_EQ(manager2.GetActiveServer(), "localhost:50051");
    EXPECT_EQ(manager2.GetActiveToken(), "secret_token_12345");
    auto session2 = manager2.GetActiveSession();
    ASSERT_TRUE(session2.has_value());
    EXPECT_EQ(session2->userId, 42);
    EXPECT_EQ(session2->userName, "bob");
    EXPECT_EQ(session2->authToken, "secret_token_12345");
}

TEST_F(ConfigTest, MalformedJsonHandling) {
    std::filesystem::create_directories(_testConfigPath.parent_path());
    {
        std::ofstream file(_testConfigPath);
        file << "{ corrupted json string: [ ";
    }

    ConfigManager manager(_testConfigPath);
    EXPECT_FALSE(manager.Load());
}

TEST_F(ConfigTest, CommandLineArgumentOverrides) {
    ConfigManager manager(_testConfigPath);
    ASSERT_TRUE(manager.Load());
    manager.UpdateActiveSession(1, "saved_user", "saved_token");

    std::array<std::string_view, 5> argv = {
        "aquarium-client", "--server",           "custom.prod.host:8443",
        "--token",         "cli_override_token",
    };

    int argc = sizeof(argv) / sizeof(argv[0]);

    auto overrides = ConfigManager::ParseCommandLine(argc, argv);
    EXPECT_EQ(overrides.server, "custom.prod.host:8443");
    EXPECT_EQ(overrides.token, "cli_override_token");

    manager.ApplyOverrides(overrides);
    EXPECT_EQ(manager.GetActiveServer(), "custom.prod.host:8443");
    EXPECT_EQ(manager.GetActiveToken(), "cli_override_token");
}

TEST_F(ConfigTest, EnvironmentVariableOverrides) {
    setenv("AQUARIUM_SERVER", "env.host.local:50051", 1);
    setenv("AQUARIUM_TOKEN", "env_tok_777", 1);

    ConfigManager manager(_testConfigPath);
    ASSERT_TRUE(manager.Load());

    manager.ApplyOverrides({});
    EXPECT_EQ(manager.GetActiveServer(), "env.host.local:50051");
    EXPECT_EQ(manager.GetActiveToken(), "env_tok_777");

    CliOverrides cli;
    cli.server = "cli.preferred.host:50051";
    manager.ApplyOverrides(cli);
    EXPECT_EQ(manager.GetActiveServer(), "cli.preferred.host:50051");
    EXPECT_EQ(manager.GetActiveToken(), "env_tok_777");
}

} // namespace Aquarium::Config::Tests
