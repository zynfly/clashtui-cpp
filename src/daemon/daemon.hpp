#pragma once

#include "core/config.hpp"
#include "core/profile_manager.hpp"
#include "daemon/process_manager.hpp"
#include "api/mihomo_client.hpp"

#include <memory>
#include <atomic>
#include <thread>
#include <string>

class Daemon {
public:
    explicit Daemon(Config& config);
    ~Daemon();

    /// Main loop — blocks until stop is requested
    int run();

    /// Request graceful stop (called from signal handler)
    void request_stop();

private:
    Config& config_;
    ProfileManager profile_mgr_;
    ProcessManager process_mgr_;
    std::unique_ptr<MihomoClient> client_;
    std::atomic<bool> stop_flag_{false};
    int socket_fd_ = -1;

    // IPC
    std::string socket_path() const;
    bool start_ipc_server();
    void ipc_loop();
    std::string handle_command(const std::string& json_line);
    void cleanup_socket();

    // Auto-update
    std::thread auto_update_thread_;
    void auto_update_loop();

    // Helper
    bool reload_mihomo();
    bool wait_for_mihomo(int timeout_sec = 10);
};
