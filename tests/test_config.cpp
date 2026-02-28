#include <gtest/gtest.h>
#include "core/config.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;

class ConfigTest : public ::testing::Test {
protected:
    std::string test_dir;
    std::string original_home;
    bool had_home = false;

    void SetUp() override {
        // Create a temp directory for test config
        test_dir = fs::temp_directory_path() / "clashtui-test-config";
        fs::create_directories(test_dir);

        // Save and override HOME
        const char* home = std::getenv("HOME");
        if (home) {
            had_home = true;
            original_home = home;
        }
        setenv("HOME", test_dir.c_str(), 1);
    }

    void TearDown() override {
        // Restore HOME
        if (had_home) {
            setenv("HOME", original_home.c_str(), 1);
        }
        // Cleanup
        fs::remove_all(test_dir);
    }
};

TEST_F(ConfigTest, DefaultValues) {
    Config cfg;
    EXPECT_EQ(cfg.data().api_host, "127.0.0.1");
    EXPECT_EQ(cfg.data().api_port, 9090);
    EXPECT_EQ(cfg.data().api_secret, "");
    EXPECT_EQ(cfg.data().api_timeout_ms, 5000);
    EXPECT_EQ(cfg.data().language, "zh");
    EXPECT_EQ(cfg.data().theme, "default");
    EXPECT_TRUE(cfg.data().subscriptions.empty());
    EXPECT_EQ(cfg.data().mihomo_binary_path, "/usr/local/bin/mihomo");
    EXPECT_EQ(cfg.data().mihomo_service_name, "mihomo");
}

TEST_F(ConfigTest, ConfigDirPath) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    std::string dir = Config::config_dir();
    EXPECT_FALSE(dir.empty());
    EXPECT_NE(dir.find(".config/clashtui-cpp"), std::string::npos);
}

TEST_F(ConfigTest, ConfigFilePath) {
    std::string path = Config::config_path();
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("config.yaml"), std::string::npos);
}

TEST_F(ConfigTest, LoadNonExistentReturnsFalse) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    Config cfg;
    EXPECT_FALSE(cfg.load());
}

TEST_F(ConfigTest, SaveAndLoad) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    // Save
    Config cfg1;
    cfg1.data().api_host = "10.0.0.1";
    cfg1.data().api_port = 7890;
    cfg1.data().api_secret = "test-secret";
    cfg1.data().language = "en";

    SubscriptionInfo sub;
    sub.name = "test-sub";
    sub.url = "https://example.com/sub";
    sub.last_updated = "2026-01-01T00:00:00";
    sub.auto_update = false;
    sub.update_interval_hours = 12;
    cfg1.data().subscriptions.push_back(sub);

    ASSERT_TRUE(cfg1.save());

    // Verify file exists
    EXPECT_TRUE(fs::exists(Config::config_path()));

    // Load into new instance
    Config cfg2;
    ASSERT_TRUE(cfg2.load());
    EXPECT_EQ(cfg2.data().api_host, "10.0.0.1");
    EXPECT_EQ(cfg2.data().api_port, 7890);
    EXPECT_EQ(cfg2.data().api_secret, "test-secret");
    EXPECT_EQ(cfg2.data().language, "en");

    ASSERT_EQ(cfg2.data().subscriptions.size(), 1u);
    EXPECT_EQ(cfg2.data().subscriptions[0].name, "test-sub");
    EXPECT_EQ(cfg2.data().subscriptions[0].url, "https://example.com/sub");
    EXPECT_FALSE(cfg2.data().subscriptions[0].auto_update);
    EXPECT_EQ(cfg2.data().subscriptions[0].update_interval_hours, 12);
}

TEST_F(ConfigTest, SaveCreatesDirectory) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    // Remove config dir if it exists
    fs::remove_all(Config::config_dir());
    EXPECT_FALSE(fs::exists(Config::config_dir()));

    Config cfg;
    ASSERT_TRUE(cfg.save());
    EXPECT_TRUE(fs::exists(Config::config_dir()));
}

TEST_F(ConfigTest, LoadMalformedYamlUsesDefaults) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    // Write invalid YAML
    fs::create_directories(Config::config_dir());
    std::ofstream out(Config::config_path());
    out << "{{{{invalid yaml!!!!";
    out.close();

    Config cfg;
    EXPECT_FALSE(cfg.load());
    // Should still have defaults
    EXPECT_EQ(cfg.data().api_host, "127.0.0.1");
    EXPECT_EQ(cfg.data().api_port, 9090);
}
