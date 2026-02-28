#include <gtest/gtest.h>
#include "api/mihomo_client.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <signal.h>
#include <sys/wait.h>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// End-to-end tests against a managed mihomo instance.
// The test fixture automatically starts mihomo before tests and stops it after.
// Set MIHOMO_PATH env var to override the mihomo binary location.

static constexpr int E2E_PORT = 19090;  // Use non-standard port to avoid conflicts

class E2E : public ::testing::Test {
protected:
    static pid_t mihomo_pid_;
    static std::string tmp_dir_;

    static std::string find_mihomo() {
        // 1. Environment variable
        if (auto* p = std::getenv("MIHOMO_PATH")) {
            if (fs::exists(p)) return p;
        }
        // 2. Common locations
        for (const char* path : {
            "/usr/local/bin/mihomo",
            "/usr/bin/mihomo",
            "/tmp/mihomo",
        }) {
            if (fs::exists(path)) return path;
        }
        // 3. ~/.local/bin
        if (auto* home = std::getenv("HOME")) {
            auto p = std::string(home) + "/.local/bin/mihomo";
            if (fs::exists(p)) return p;
        }
        return "";
    }

    static void SetUpTestSuite() {
        auto binary = find_mihomo();
        if (binary.empty()) {
            GTEST_SKIP() << "mihomo binary not found, skipping E2E tests";
            return;
        }

        // Create temp directory with minimal config
        tmp_dir_ = "/tmp/clashtui-e2e-" + std::to_string(getpid());
        fs::create_directories(tmp_dir_);

        std::string config_path = tmp_dir_ + "/config.yaml";
        {
            std::ofstream f(config_path);
            f << "mixed-port: 17890\n"
              << "external-controller: 127.0.0.1:" << E2E_PORT << "\n"
              << "mode: rule\n"
              << "log-level: info\n"
              << "proxies:\n"
              << "  - name: test-ss\n"
              << "    type: ss\n"
              << "    server: 1.2.3.4\n"
              << "    port: 8388\n"
              << "    cipher: aes-256-gcm\n"
              << "    password: test\n"
              << "proxy-groups:\n"
              << "  - name: PROXY\n"
              << "    type: select\n"
              << "    proxies:\n"
              << "      - test-ss\n"
              << "      - DIRECT\n"
              << "  - name: AUTO\n"
              << "    type: url-test\n"
              << "    proxies:\n"
              << "      - test-ss\n"
              << "      - DIRECT\n"
              << "    url: http://www.gstatic.com/generate_204\n"
              << "    interval: 300\n"
              << "rules:\n"
              << "  - MATCH,PROXY\n";
        }

        // Fork and exec mihomo
        mihomo_pid_ = fork();
        if (mihomo_pid_ == 0) {
            // Child: redirect stdout/stderr to /dev/null
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            execl(binary.c_str(), binary.c_str(), "-d", tmp_dir_.c_str(), nullptr);
            _exit(1);  // exec failed
        }

        // Wait for mihomo to be ready
        MihomoClient client{"127.0.0.1", E2E_PORT, ""};
        bool ready = false;
        for (int i = 0; i < 30; ++i) {  // up to 3 seconds
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (client.test_connection()) {
                ready = true;
                break;
            }
        }

        if (!ready) {
            // Cleanup on failure
            if (mihomo_pid_ > 0) {
                kill(mihomo_pid_, SIGTERM);
                waitpid(mihomo_pid_, nullptr, 0);
                mihomo_pid_ = -1;
            }
            GTEST_SKIP() << "mihomo failed to start within 3 seconds";
        }
    }

    static void TearDownTestSuite() {
        if (mihomo_pid_ > 0) {
            kill(mihomo_pid_, SIGTERM);
            int status;
            waitpid(mihomo_pid_, &status, 0);
            mihomo_pid_ = -1;
        }
        // Cleanup temp dir
        if (!tmp_dir_.empty()) {
            std::error_code ec;
            fs::remove_all(tmp_dir_, ec);
        }
    }

    MihomoClient client{"127.0.0.1", E2E_PORT, ""};
};

pid_t E2E::mihomo_pid_ = -1;
std::string E2E::tmp_dir_;

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
    EXPECT_TRUE(cfg.mode == "rule" || cfg.mode == "global" || cfg.mode == "direct");
    EXPECT_GT(cfg.mixed_port, 0);
}

TEST_F(E2E, SetModeAndVerify) {
    auto original = client.get_config().mode;

    EXPECT_TRUE(client.set_mode("global"));
    auto cfg1 = client.get_config();
    EXPECT_EQ(cfg1.mode, "global");

    EXPECT_TRUE(client.set_mode("direct"));
    auto cfg2 = client.get_config();
    EXPECT_EQ(cfg2.mode, "direct");

    EXPECT_TRUE(client.set_mode("rule"));
    auto cfg3 = client.get_config();
    EXPECT_EQ(cfg3.mode, "rule");

    client.set_mode(original);
}

TEST_F(E2E, GetProxyGroups) {
    auto groups = client.get_proxy_groups();
    EXPECT_FALSE(groups.empty());

    bool has_proxy = groups.count("PROXY") > 0;
    bool has_auto = groups.count("AUTO") > 0;
    EXPECT_TRUE(has_proxy || has_auto);

    for (auto& [name, group] : groups) {
        EXPECT_FALSE(group.name.empty());
        EXPECT_FALSE(group.type.empty());
        EXPECT_FALSE(group.all.empty());
    }
}

TEST_F(E2E, GetProxyNodes) {
    auto nodes = client.get_proxy_nodes();
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
        EXPECT_TRUE(client.select_proxy("PROXY", "DIRECT"));

        auto updated = client.get_proxy_groups();
        EXPECT_EQ(updated["PROXY"].now, "DIRECT");
    }
}

TEST_F(E2E, TestDelay) {
    auto result = client.test_delay("DIRECT");
    EXPECT_EQ(result.name, "DIRECT");
    EXPECT_TRUE(result.success || !result.error.empty());
}

TEST_F(E2E, GetConnections) {
    auto stats = client.get_connections();
    EXPECT_GE(stats.active_connections, 0);
    EXPECT_GE(stats.upload_total, 0);
    EXPECT_GE(stats.download_total, 0);
}

TEST_F(E2E, StreamLogsShortDuration) {
    std::atomic<bool> stop{false};
    std::vector<LogEntry> received;
    std::mutex mtx;

    std::thread t([&]() {
        client.stream_logs("info",
            [&](LogEntry entry) {
                std::lock_guard<std::mutex> lock(mtx);
                received.push_back(std::move(entry));
            },
            stop);
    });

    client.set_mode("global");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    client.set_mode("rule");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    stop.store(true);
    t.join();
}

TEST_F(E2E, ReloadConfig) {
    if (!tmp_dir_.empty()) {
        std::string config_path = tmp_dir_ + "/config.yaml";
        client.reload_config(config_path);
    }
}

TEST_F(E2E, RapidModeSwitch) {
    for (int i = 0; i < 5; i++) {
        client.set_mode("global");
        client.set_mode("rule");
        client.set_mode("direct");
    }
    client.set_mode("rule");
    auto cfg = client.get_config();
    EXPECT_EQ(cfg.mode, "rule");
}
