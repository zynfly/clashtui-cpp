#pragma once

#include <string>

// Forward-declare to avoid including installer.hpp here
enum class ServiceScope;

struct UpdateInfo {
    bool available = false;
    std::string latest_version;
    std::string current_version;
    std::string download_url;     // direct asset URL for current arch
    std::string changelog;
};

struct UpdateResult {
    bool success = false;
    std::string message;
};

/// Info about the clashtui-cpp daemon's systemd service
struct DaemonServiceInfo {
    bool is_systemd = false;      // systemd available on system
    bool service_exists = false;  // service unit file found
    bool service_active = false;  // service is running
    std::string service_name;     // e.g. "clashtui-cpp"
    ServiceScope scope;           // System or User
};

class Updater {
public:
    explicit Updater(const std::string& repo = "zynfly/clashtui-cpp");

    /// Check GitHub for latest release, compare against compiled-in version
    UpdateInfo check_for_update() const;

    /// Get compiled-in version
    static std::string current_version();

    /// Download and apply self-update (replace current binary).
    /// Daemon-aware: stops daemon before replace, restarts after.
    UpdateResult apply_self_update() const;

    /// Download and apply mihomo update (using config for paths).
    /// Daemon-aware: stops daemon before replace, restarts after.
    UpdateResult update_mihomo() const;

    /// Atomically replace a binary file using rename().
    static std::string atomic_replace_binary(const std::string& new_binary,
                                             const std::string& target_path);

    /// Detect clashtui-cpp daemon systemd service status
    static DaemonServiceInfo detect_daemon_service();

    /// Stop daemon service if running (daemon stop → mihomo also stops)
    static bool stop_daemon_if_running(const DaemonServiceInfo& info);

    /// Start daemon service
    static bool start_daemon(const DaemonServiceInfo& info);

private:
    std::string repo_;
    static std::string detect_arch_tag();
    static std::string get_self_path();
};
