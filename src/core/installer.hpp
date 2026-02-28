#pragma once

#include <string>
#include <functional>
#include <vector>
#include <atomic>

// ── Data structures ────────────────────────────────────────────

struct AssetInfo {
    std::string name;
    std::string download_url;
    int64_t size = 0;  // bytes
};

struct ReleaseInfo {
    std::string version;
    std::string changelog;
    std::vector<AssetInfo> assets;
    std::string checksums_url;  // URL to checksums.txt asset
};

struct PlatformInfo {
    std::string os;    // "linux", "darwin", "windows"
    std::string arch;  // "amd64", "arm64", "armv7", etc.
};

enum class ServiceScope {
    System,  // /etc/systemd/system/, requires sudo
    User,    // ~/.config/systemd/user/
    None     // no service management
};

struct UninstallProgress {
    enum class Phase {
        Idle,
        StoppingService,
        DisablingService,
        RemovingService,
        RemovingBinary,
        RemovingConfig,
        Complete,
        Failed
    };
    Phase phase = Phase::Idle;
    std::string message;
};

// ── Installer class ────────────────────────────────────────────

class Installer {
public:
    // ── Phase 1: Detection & version comparison ────────────────

    /// Check if mihomo binary exists at path
    static bool is_installed(const std::string& binary_path);

    /// Get version string from running `mihomo -v`
    static std::string get_running_version(const std::string& binary_path);

    /// Detect current platform OS and architecture
    static PlatformInfo detect_platform();

    /// Select the best matching asset for the given platform
    /// Returns empty AssetInfo if no match found
    static AssetInfo select_asset(const ReleaseInfo& release, const PlatformInfo& platform);

    /// Compare version strings like "v1.18.0", returns true if remote is newer
    static bool is_newer_version(const std::string& local_version, const std::string& remote_version);

    /// Fetch latest release info from GitHub API
    static ReleaseInfo fetch_latest_release();

    // ── Phase 2: Download pipeline + SHA256 ────────────────────

    /// Verify SHA256 checksum of a file using OpenSSL EVP
    static bool verify_sha256(const std::string& file_path, const std::string& expected_hash);

    /// Download a single file with progress reporting and cancellation
    /// on_progress receives (bytes_received, total_bytes); total_bytes may be 0 if unknown
    static bool download_single(const std::string& url,
                                const std::string& dest_path,
                                std::function<void(int64_t received, int64_t total)> on_progress = nullptr,
                                std::atomic<bool>* cancel_flag = nullptr);

    /// Get list of GitHub proxy mirrors (first entry is empty = direct)
    static std::vector<std::string> get_proxy_mirrors();

    /// Try downloading through mirrors, first success wins
    static bool download_with_fallback(const std::string& url,
                                       const std::string& dest_path,
                                       std::function<void(int64_t received, int64_t total)> on_progress = nullptr,
                                       std::atomic<bool>* cancel_flag = nullptr);

    /// Download checksums.txt and extract hash for a specific filename
    static std::string fetch_checksum_for_file(const std::string& checksums_url,
                                               const std::string& filename);

    /// Extract a .gz file: gunzip to dest, chmod +x
    static bool extract_gz(const std::string& gz_path, const std::string& dest_path);

    /// Full install pipeline: extract gz + copy to install path (with sudo if needed) + chmod
    static bool install_binary(const std::string& gz_path,
                               const std::string& install_path,
                               bool needs_sudo);

    /// Generate minimal mihomo config
    static bool generate_default_config(const std::string& config_path);

    // Keep legacy interface for backward compatibility
    static bool download_binary(const std::string& url,
                                const std::string& dest_path,
                                std::function<void(float)> on_progress = nullptr);

    // ── Phase 3: Systemd service management ────────────────────

    /// Check if systemd is available on the system
    static bool has_systemd();

    /// Generate systemd service unit file content
    static std::string generate_service_content(const std::string& binary_path,
                                                const std::string& config_dir,
                                                ServiceScope scope);

    /// Install systemd service: write unit file, daemon-reload, enable, start
    static bool install_service(const std::string& binary_path,
                                const std::string& config_dir,
                                const std::string& service_name,
                                ServiceScope scope);

    /// Start service
    static bool start_service(const std::string& service_name, ServiceScope scope);

    /// Stop service
    static bool stop_service(const std::string& service_name, ServiceScope scope);

    /// Enable service (auto-start on boot)
    static bool enable_service(const std::string& service_name, ServiceScope scope);

    /// Disable service
    static bool disable_service(const std::string& service_name, ServiceScope scope);

    /// Check if service is currently active/running
    static bool is_service_active(const std::string& service_name, ServiceScope scope);

    /// Remove service: stop, disable, delete unit file, daemon-reload
    static bool remove_service(const std::string& service_name, ServiceScope scope);

    // ── Phase 3b: Daemon service management ─────────────────────

    /// Generate systemd service unit file content for the clashtui-cpp daemon
    static std::string generate_daemon_service_content(
        const std::string& clashtui_binary_path, ServiceScope scope);

    /// Install systemd service for the clashtui-cpp daemon
    static bool install_daemon_service(
        const std::string& clashtui_binary_path,
        const std::string& service_name,
        ServiceScope scope);

    // ── Phase 4: Uninstall ─────────────────────────────────────

    /// Full uninstall: stop service -> disable -> remove service -> remove binary -> optionally remove config
    static bool uninstall(const std::string& binary_path,
                          const std::string& service_name,
                          ServiceScope scope,
                          bool remove_config,
                          const std::string& config_dir,
                          std::function<void(UninstallProgress)> on_progress = nullptr);

private:
    /// Helper: build the systemctl command prefix for a given scope
    static std::string systemctl_cmd(ServiceScope scope);

    /// Helper: get the service file path for a given scope and service name
    static std::string service_file_path(const std::string& service_name, ServiceScope scope);

    /// Helper: run a shell command and return exit code
    static int run_command(const std::string& cmd);

    /// Helper: run a shell command and capture stdout
    static std::string run_command_output(const std::string& cmd);

    /// Helper: parse a URL into scheme, host, port, path
    struct UrlParts {
        std::string scheme;
        std::string host;
        int port = 443;
        std::string path;
    };
    static UrlParts parse_url(const std::string& url);
};
