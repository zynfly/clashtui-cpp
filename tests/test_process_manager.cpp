#include <gtest/gtest.h>
#include "daemon/process_manager.hpp"

#include <chrono>
#include <thread>

TEST(ProcessManagerTest, Construction) {
    ProcessManager pm;
    EXPECT_FALSE(pm.is_running());
    EXPECT_EQ(pm.child_pid(), -1);
}

TEST(ProcessManagerTest, StartSleep) {
    ProcessManager pm;
    pm.set_auto_restart(false);
    EXPECT_TRUE(pm.start("/bin/sleep", {"60"}));
    EXPECT_TRUE(pm.is_running());
    EXPECT_GT(pm.child_pid(), 0);

    pm.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(pm.is_running());
}

TEST(ProcessManagerTest, StopSendsSignal) {
    ProcessManager pm;
    pm.set_auto_restart(false);
    EXPECT_TRUE(pm.start("/bin/sleep", {"60"}));
    EXPECT_TRUE(pm.is_running());

    EXPECT_TRUE(pm.stop());
    EXPECT_FALSE(pm.is_running());
    EXPECT_EQ(pm.child_pid(), -1);
}

TEST(ProcessManagerTest, Restart) {
    ProcessManager pm;
    pm.set_auto_restart(false);
    EXPECT_TRUE(pm.start("/bin/sleep", {"60"}));
    pid_t first_pid = pm.child_pid();
    EXPECT_GT(first_pid, 0);

    EXPECT_TRUE(pm.restart());
    EXPECT_TRUE(pm.is_running());
    pid_t second_pid = pm.child_pid();
    EXPECT_GT(second_pid, 0);
    EXPECT_NE(first_pid, second_pid);

    pm.stop();
}

TEST(ProcessManagerTest, StartInvalidBinary) {
    ProcessManager pm;
    pm.set_auto_restart(false);
    // fork will succeed but execvp will fail, child exits with 127
    EXPECT_TRUE(pm.start("/nonexistent/binary"));
    // Wait long enough for the child to exec-fail and monitor to detect it
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_FALSE(pm.is_running());

    pm.stop();
}

TEST(ProcessManagerTest, AutoRestartDisabled) {
    ProcessManager pm;
    pm.set_auto_restart(false);
    // Start a process that exits immediately
    EXPECT_TRUE(pm.start("/bin/true"));
    // Wait for monitor to detect exit
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_FALSE(pm.is_running());

    pm.stop();
}

TEST(ProcessManagerTest, OnCrashCallback) {
    ProcessManager pm;
    pm.set_auto_restart(false);

    int crash_code = -999;
    pm.on_crash = [&](int code) { crash_code = code; };

    EXPECT_TRUE(pm.start("/bin/false")); // exits with code 1
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // /bin/false exits with 1
    EXPECT_NE(crash_code, -999); // callback was called
    EXPECT_GT(crash_code, 0);    // non-zero exit code
    pm.stop();
}

TEST(ProcessManagerTest, DoubleStop) {
    ProcessManager pm;
    pm.set_auto_restart(false);
    EXPECT_TRUE(pm.start("/bin/sleep", {"60"}));
    EXPECT_TRUE(pm.stop());
    EXPECT_TRUE(pm.stop()); // should not crash
}
