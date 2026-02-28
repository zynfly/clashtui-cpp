#include <gtest/gtest.h>
#include "api/mihomo_client.hpp"

// These tests verify data structures and client construction.
// API call tests require a running mihomo instance and are skipped by default.

TEST(MihomoClientTest, Construction) {
    MihomoClient client("127.0.0.1", 9090, "");
    // Should not crash
}

TEST(MihomoClientTest, ConstructionWithSecret) {
    MihomoClient client("127.0.0.1", 9090, "my-secret");
    // Should not crash
}

TEST(MihomoClientTest, TestConnectionNoServer) {
    MihomoClient client("127.0.0.1", 1, ""); // Port 1 = unlikely to have server
    EXPECT_FALSE(client.test_connection());
}

// ── Data structure tests ────────────────────────────────────

TEST(DataStructTest, VersionInfoDefaults) {
    VersionInfo info;
    EXPECT_TRUE(info.version.empty());
    EXPECT_FALSE(info.premium);
}

TEST(DataStructTest, ClashConfigDefaults) {
    ClashConfig cfg;
    EXPECT_TRUE(cfg.mode.empty());
    EXPECT_EQ(cfg.mixed_port, 0);
    EXPECT_EQ(cfg.socks_port, 0);
    EXPECT_EQ(cfg.port, 0);
    EXPECT_FALSE(cfg.allow_lan);
}

TEST(DataStructTest, ProxyNodeDefaults) {
    ProxyNode node;
    EXPECT_TRUE(node.name.empty());
    EXPECT_EQ(node.delay, -1); // untested
    EXPECT_TRUE(node.alive);
    EXPECT_TRUE(node.delay_history.empty());
}

TEST(DataStructTest, ProxyGroupDefaults) {
    ProxyGroup group;
    EXPECT_TRUE(group.name.empty());
    EXPECT_TRUE(group.type.empty());
    EXPECT_TRUE(group.now.empty());
    EXPECT_TRUE(group.all.empty());
}

TEST(DataStructTest, ConnectionStatsDefaults) {
    ConnectionStats stats;
    EXPECT_EQ(stats.active_connections, 0);
    EXPECT_EQ(stats.upload_total, 0);
    EXPECT_EQ(stats.download_total, 0);
    EXPECT_EQ(stats.upload_speed, 0);
    EXPECT_EQ(stats.download_speed, 0);
}

TEST(DataStructTest, DelayResultDefaults) {
    DelayResult result;
    EXPECT_TRUE(result.name.empty());
    EXPECT_EQ(result.delay, 0);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.empty());
}

TEST(DataStructTest, LogEntryDefaults) {
    LogEntry entry;
    EXPECT_TRUE(entry.type.empty());
    EXPECT_TRUE(entry.payload.empty());
}

// ── GetVersion/GetConfig on unreachable server ──────────────

TEST(MihomoClientTest, GetVersionNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    auto info = client.get_version();
    EXPECT_TRUE(info.version.empty());
}

TEST(MihomoClientTest, GetConfigNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    auto cfg = client.get_config();
    EXPECT_TRUE(cfg.mode.empty());
}

TEST(MihomoClientTest, SetModeNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    EXPECT_FALSE(client.set_mode("global"));
}

TEST(MihomoClientTest, GetProxiesNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    auto groups = client.get_proxy_groups();
    EXPECT_TRUE(groups.empty());
    auto nodes = client.get_proxy_nodes();
    EXPECT_TRUE(nodes.empty());
}

TEST(MihomoClientTest, SelectProxyNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    EXPECT_FALSE(client.select_proxy("group", "node"));
}

TEST(MihomoClientTest, TestDelayNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    auto result = client.test_delay("node");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST(MihomoClientTest, GetConnectionsNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    auto stats = client.get_connections();
    EXPECT_EQ(stats.active_connections, 0);
}

TEST(MihomoClientTest, CloseConnectionsNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    EXPECT_FALSE(client.close_all_connections());
}

TEST(MihomoClientTest, ReloadConfigNoServer) {
    MihomoClient client("127.0.0.1", 1, "");
    EXPECT_FALSE(client.reload_config("/tmp/nonexistent.yaml"));
}
