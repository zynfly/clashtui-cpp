#include <gtest/gtest.h>
#include "daemon/daemon.hpp"
#include "core/config.hpp"

#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;
using json = nlohmann::json;

class DaemonIPCTest : public ::testing::Test {
protected:
    std::string original_home_;
    std::string temp_dir_;

    void SetUp() override {
        const char* home = std::getenv("HOME");
        if (home) original_home_ = home;

        temp_dir_ = "/tmp/ct_d_" + std::to_string(::getpid());
        fs::create_directories(temp_dir_);
        setenv("HOME", temp_dir_.c_str(), 1);
    }

    void TearDown() override {
        if (!original_home_.empty()) {
            setenv("HOME", original_home_.c_str(), 1);
        } else {
            unsetenv("HOME");
        }
        try {
            fs::remove_all(temp_dir_);
        } catch (...) {}
    }

    std::string socket_path() {
        return temp_dir_ + "/.config/clashtui-cpp/clashtui.sock";
    }

    bool wait_for_socket(int timeout_ms = 5000) {
        int waited = 0;
        while (waited < timeout_ms) {
            if (fs::exists(socket_path())) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            waited += 50;
        }
        return false;
    }

    json send_ipc(const json& cmd) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return json();

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::string path = socket_path();
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return json();
        }

        // Set read timeout
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string msg = cmd.dump() + "\n";
        write(fd, msg.data(), msg.size());

        // Read response
        std::string buf;
        char c;
        while (read(fd, &c, 1) == 1) {
            if (c == '\n') break;
            buf += c;
        }
        close(fd);

        if (buf.empty()) return json();
        try {
            return json::parse(buf);
        } catch (...) {
            return json();
        }
    }
};

TEST_F(DaemonIPCTest, StatusCommand) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    Config config;
    // Set a nonexistent binary so mihomo won't actually start
    config.data().mihomo_binary_path = "/nonexistent/mihomo";
    Daemon daemon(config);

    // Run daemon in a thread
    std::thread t([&]() { daemon.run(); });

    ASSERT_TRUE(wait_for_socket());

    auto resp = send_ipc({{"cmd", "status"}});
    EXPECT_FALSE(resp.empty());
    if (!resp.empty()) {
        EXPECT_TRUE(resp.value("ok", false));
        EXPECT_TRUE(resp.contains("data"));
        EXPECT_FALSE(resp["data"].value("mihomo_running", true));
    }

    daemon.request_stop();
    t.join();
}

TEST_F(DaemonIPCTest, ProfileListEmpty) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    Config config;
    config.data().mihomo_binary_path = "/nonexistent/mihomo";
    Daemon daemon(config);

    std::thread t([&]() { daemon.run(); });
    ASSERT_TRUE(wait_for_socket());

    auto resp = send_ipc({{"cmd", "profile_list"}});
    EXPECT_FALSE(resp.empty());
    if (!resp.empty()) {
        EXPECT_TRUE(resp.value("ok", false));
        EXPECT_TRUE(resp["data"].is_array());
        EXPECT_TRUE(resp["data"].empty());
    }

    daemon.request_stop();
    t.join();
}

TEST_F(DaemonIPCTest, UnknownCommand) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    Config config;
    config.data().mihomo_binary_path = "/nonexistent/mihomo";
    Daemon daemon(config);

    std::thread t([&]() { daemon.run(); });
    ASSERT_TRUE(wait_for_socket());

    auto resp = send_ipc({{"cmd", "nonexistent_cmd"}});
    EXPECT_FALSE(resp.empty());
    if (!resp.empty()) {
        EXPECT_FALSE(resp.value("ok", true));
        EXPECT_TRUE(resp.contains("error"));
    }

    daemon.request_stop();
    t.join();
}

TEST_F(DaemonIPCTest, ProfileAddEmptyName) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    Config config;
    config.data().mihomo_binary_path = "/nonexistent/mihomo";
    Daemon daemon(config);

    std::thread t([&]() { daemon.run(); });
    ASSERT_TRUE(wait_for_socket());

    auto resp = send_ipc({{"cmd", "profile_add"}, {"name", ""}, {"url", "http://example.com"}});
    EXPECT_FALSE(resp.empty());
    if (!resp.empty()) {
        EXPECT_FALSE(resp.value("ok", true));
    }

    daemon.request_stop();
    t.join();
}
