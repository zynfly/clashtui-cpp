#include <gtest/gtest.h>
#include "api/mihomo_client.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// End-to-end tests against a real mihomo instance.
// Requires mihomo running at 127.0.0.1:9090.
// These tests are tagged with E2E and can be filtered with --gtest_filter=E2E*

class E2E : public ::testing::Test {
protected:
    MihomoClient client{"127.0.0.1", 9090, ""};
};

TEST_F(E2E, TestConnection) {
    EXPECT_TRUE(client.test_connection());
}

TEST_F(E2E, GetVersion) {
    auto info = client.get_version();
    EXPECT_FALSE(info.version.empty());
    EXPECT_TRUE(info.version.find("v1.") != std::string::npos ||
                info.version.find("v2.") != std::string::npos);
}

TEST_F(E2E, GetConfig) {
    auto cfg = client.get_config();
    EXPECT_FALSE(cfg.mode.empty());
    // Should be one of the valid modes
    EXPECT_TRUE(cfg.mode == "rule" || cfg.mode == "global" || cfg.mode == "direct");
    EXPECT_GT(cfg.mixed_port, 0);
}

TEST_F(E2E, SetModeAndVerify) {
    // Save original mode
    auto original = client.get_config().mode;

    // Switch to global
    EXPECT_TRUE(client.set_mode("global"));
    auto cfg1 = client.get_config();
    EXPECT_EQ(cfg1.mode, "global");

    // Switch to direct
    EXPECT_TRUE(client.set_mode("direct"));
    auto cfg2 = client.get_config();
    EXPECT_EQ(cfg2.mode, "direct");

    // Switch to rule
    EXPECT_TRUE(client.set_mode("rule"));
    auto cfg3 = client.get_config();
    EXPECT_EQ(cfg3.mode, "rule");

    // Restore original
    client.set_mode(original);
}

TEST_F(E2E, GetProxyGroups) {
    auto groups = client.get_proxy_groups();
    EXPECT_FALSE(groups.empty());

    // Should have our test groups
    bool has_proxy = groups.count("PROXY") > 0;
    bool has_auto = groups.count("AUTO") > 0;
    EXPECT_TRUE(has_proxy || has_auto);

    // Verify group structure
    for (auto& [name, group] : groups) {
        EXPECT_FALSE(group.name.empty());
        EXPECT_FALSE(group.type.empty());
        EXPECT_FALSE(group.all.empty());
    }
}

TEST_F(E2E, GetProxyNodes) {
    auto nodes = client.get_proxy_nodes();
    // Should have at least DIRECT and REJECT
    EXPECT_GE(nodes.size(), 1u);

    bool has_direct = nodes.count("DIRECT") > 0;
    EXPECT_TRUE(has_direct);

    if (has_direct) {
        EXPECT_EQ(nodes["DIRECT"].type, "Direct");
    }
}

TEST_F(E2E, SelectProxy) {
    auto groups = client.get_proxy_groups();
    if (groups.count("PROXY") > 0) {
        // Select DIRECT in PROXY group
        EXPECT_TRUE(client.select_proxy("PROXY", "DIRECT"));

        // Verify selection
        auto updated = client.get_proxy_groups();
        EXPECT_EQ(updated["PROXY"].now, "DIRECT");
    }
}

TEST_F(E2E, TestDelay) {
    auto result = client.test_delay("DIRECT");
    // DIRECT should succeed with some delay
    EXPECT_EQ(result.name, "DIRECT");
    // Note: delay test for DIRECT may or may not succeed depending on network
    // Just verify it doesn't crash and returns a valid result structure
    EXPECT_TRUE(result.success || !result.error.empty());
}

TEST_F(E2E, GetConnections) {
    auto stats = client.get_connections();
    // Should return valid stats (may be 0 if no active connections)
    EXPECT_GE(stats.active_connections, 0);
    EXPECT_GE(stats.upload_total, 0);
    EXPECT_GE(stats.download_total, 0);
}

TEST_F(E2E, StreamLogsShortDuration) {
    std::atomic<bool> stop{false};
    std::vector<LogEntry> received;
    std::mutex mtx;

    // Stream for 2 seconds in background
    std::thread t([&]() {
        client.stream_logs("info",
            [&](LogEntry entry) {
                std::lock_guard<std::mutex> lock(mtx);
                received.push_back(std::move(entry));
            },
            stop);
    });

    // Trigger some log activity by switching modes
    client.set_mode("global");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    client.set_mode("rule");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    stop.store(true);
    t.join();

    // May or may not have received logs depending on timing
    // Just verify no crash
}

TEST_F(E2E, ReloadConfig) {
    // Reload the current config (mihomo default path)
    // Note: The actual path depends on how mihomo was started
    // We test with the typical ~/.config/mihomo/config.yaml
    const char* home = std::getenv("HOME");
    if (home) {
        std::string config_path = std::string(home) + "/.config/mihomo/config.yaml";
        // This may succeed or fail depending on the actual config path
        // Just verify no crash
        client.reload_config(config_path);
    }
}

TEST_F(E2E, RapidModeSwitch) {
    // Stress test: rapid mode switching should not crash
    for (int i = 0; i < 5; i++) {
        client.set_mode("global");
        client.set_mode("rule");
        client.set_mode("direct");
    }
    // Restore
    client.set_mode("rule");
    auto cfg = client.get_config();
    EXPECT_EQ(cfg.mode, "rule");
}
