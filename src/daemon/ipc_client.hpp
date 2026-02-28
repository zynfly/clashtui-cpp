#pragma once

#include "core/profile_manager.hpp"

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

class DaemonClient {
public:
    /// Check if the daemon is running (socket exists and responds)
    bool is_daemon_running();

    /// List profiles managed by the daemon
    std::vector<ProfileInfo> list_profiles();

    /// Add a new profile
    bool add_profile(const std::string& name, const std::string& url, std::string& err);

    /// Update (re-download) a profile
    bool update_profile(const std::string& name, std::string& err);

    /// Delete a profile
    bool delete_profile(const std::string& name, std::string& err);

    /// Switch the active profile
    bool switch_profile(const std::string& name, std::string& err);

    /// Get the name of the active profile
    std::string get_active_profile();

    struct DaemonStatus {
        bool mihomo_running = false;
        int mihomo_pid = -1;
        std::string active_profile;
    };

    /// Get daemon status
    DaemonStatus get_status();

    /// Request mihomo start/stop/restart
    bool mihomo_start(std::string& err);
    bool mihomo_stop(std::string& err);
    bool mihomo_restart(std::string& err);

private:
    std::string socket_path() const;

    /// Send a JSON command and receive response
    /// Returns empty json on connection failure
    nlohmann::json send_command(const nlohmann::json& cmd);
};
