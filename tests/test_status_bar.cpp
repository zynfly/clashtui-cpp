#include <gtest/gtest.h>
#include "ui/status_bar.hpp"

#include <thread>
#include <vector>

// StatusBar::format_speed is private, so we test via a helper
// We'll test the public interface instead

TEST(StatusBarTest, DefaultState) {
    StatusBar bar;
    // Should not crash on construction
    auto comp = bar.component();
    EXPECT_NE(comp, nullptr);
}

TEST(StatusBarTest, SetConnected) {
    StatusBar bar;
    bar.set_connected(true);
    // No crash, thread-safe
    bar.set_connected(false);
}

TEST(StatusBarTest, SetMode) {
    StatusBar bar;
    bar.set_mode("global");
    bar.set_mode("rule");
    bar.set_mode("direct");
    // No crash
}

TEST(StatusBarTest, SetConnections) {
    StatusBar bar;
    bar.set_connections(0, 0, 0);
    bar.set_connections(100, 1024, 2048);
    bar.set_connections(999, 1024 * 1024, 1024 * 1024 * 10);
    // No crash
}

TEST(StatusBarTest, ThreadSafety) {
    StatusBar bar;
    // Simulate concurrent access
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&bar, i]() {
            bar.set_connected(i % 2 == 0);
            bar.set_mode(i % 2 == 0 ? "global" : "rule");
            bar.set_connections(i * 10, i * 1024, i * 2048);
        });
    }
    for (auto& t : threads) t.join();
    // No crash or data race
}
