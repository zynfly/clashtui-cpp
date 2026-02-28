#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <vector>
#include <sys/types.h>

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    /// Start a child process with the given binary and arguments
    bool start(const std::string& binary_path, const std::vector<std::string>& args = {});

    /// Stop the child process (SIGTERM, wait, then SIGKILL if needed)
    bool stop();

    /// Restart the child process with the same binary and args
    bool restart();

    /// Check if the child process is currently running
    bool is_running() const;

    /// Get the PID of the child process (-1 if not running)
    pid_t child_pid() const;

    /// Enable or disable automatic restart on crash
    void set_auto_restart(bool enable);

    /// Callback invoked when the child process exits unexpectedly
    std::function<void(int exit_code)> on_crash;

private:
    std::string binary_path_;
    std::vector<std::string> args_;
    std::atomic<pid_t> child_pid_{-1};
    std::atomic<bool> auto_restart_{true};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> monitor_running_{false};
    std::thread monitor_thread_;

    void monitor_loop();
    void stop_monitor();
    bool do_start();
};
