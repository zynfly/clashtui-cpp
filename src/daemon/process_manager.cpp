#include "daemon/process_manager.hpp"

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <chrono>
#include <cstring>

ProcessManager::ProcessManager() = default;

ProcessManager::~ProcessManager() {
    stop();
}

bool ProcessManager::do_start() {
    pid_t pid = fork();
    if (pid < 0) {
        return false; // fork failed
    }

    if (pid == 0) {
        // Child process
        // Build argv array for execvp
        std::vector<const char*> argv;
        argv.push_back(binary_path_.c_str());
        for (const auto& arg : args_) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        // Replace child process with target binary
        execvp(binary_path_.c_str(), const_cast<char* const*>(argv.data()));

        // If execvp returns, it failed
        _exit(127);
    }

    // Parent process
    child_pid_ = pid;
    return true;
}

bool ProcessManager::start(const std::string& binary_path, const std::vector<std::string>& args) {
    // Stop existing process if running
    if (is_running()) {
        stop();
    }

    binary_path_ = binary_path;
    args_ = args;
    stop_requested_.store(false);

    if (!do_start()) {
        return false;
    }

    // Start monitor thread
    monitor_running_.store(true);
    monitor_thread_ = std::thread(&ProcessManager::monitor_loop, this);

    return true;
}

bool ProcessManager::stop() {
    stop_requested_.store(true);

    if (child_pid_ > 0) {
        // Send SIGTERM first
        if (kill(child_pid_, SIGTERM) == 0) {
            // Wait up to 5 seconds for graceful exit
            for (int i = 0; i < 50; ++i) {
                int status;
                pid_t result = waitpid(child_pid_, &status, WNOHANG);
                if (result > 0) {
                    child_pid_ = -1;
                    stop_monitor();
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Force kill if still running
            kill(child_pid_, SIGKILL);
            int status;
            waitpid(child_pid_, &status, 0);
        }
        child_pid_ = -1;
    }

    stop_monitor();
    return true;
}

bool ProcessManager::restart() {
    stop();
    stop_requested_.store(false);

    if (!do_start()) {
        return false;
    }

    // Restart monitor
    monitor_running_.store(true);
    monitor_thread_ = std::thread(&ProcessManager::monitor_loop, this);

    return true;
}

bool ProcessManager::is_running() const {
    if (child_pid_ <= 0) return false;

    // Check if process exists without sending a signal
    if (kill(child_pid_, 0) == 0) {
        return true;
    }
    return false;
}

pid_t ProcessManager::child_pid() const {
    return child_pid_;
}

void ProcessManager::set_auto_restart(bool enable) {
    auto_restart_.store(enable);
}

void ProcessManager::stop_monitor() {
    monitor_running_.store(false);
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void ProcessManager::monitor_loop() {
    while (monitor_running_.load() && !stop_requested_.load()) {
        if (child_pid_ > 0) {
            int status;
            pid_t result = waitpid(child_pid_, &status, WNOHANG);

            if (result > 0) {
                // Child exited
                int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                child_pid_ = -1;

                if (!stop_requested_.load()) {
                    // Unexpected exit
                    if (on_crash) {
                        on_crash(exit_code);
                    }

                    if (auto_restart_.load()) {
                        // Wait before restarting
                        for (int i = 0; i < 30 && !stop_requested_.load(); ++i) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                        if (!stop_requested_.load()) {
                            do_start();
                        }
                    }
                }
            }
        }

        // Check every 500ms
        for (int i = 0; i < 5 && monitor_running_.load() && !stop_requested_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
