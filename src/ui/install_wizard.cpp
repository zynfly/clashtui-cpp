#include "ui/install_wizard.hpp"
#include "core/config.hpp"
#include "core/installer.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using namespace ftxui;

static std::string shell_quote(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

// ── State machine ───────────────────────────────────────────────

enum class WizardMode {
    Check,               // Initial: detect installed or not
    NotInstalled,        // Show path selection + Enter to install
    Installed,           // Show version + service/update/uninstall menu
    FetchingRelease,     // Background: fetching release info
    ReadyToInstall,      // Show release info, Enter to download
    Downloading,         // Progress bar with proxy status
    Verifying,           // SHA256 check
    Installing,          // Extract + copy
    ServiceSetup,        // Y/N create systemd service
    Complete,            // Done, any key to return
    ConfirmUninstall,    // Confirm uninstall mihomo Y/N
    Uninstalling,        // Uninstall in progress
    ConfirmUninstallSelf,// Confirm uninstall clashtui-cpp Y/N
    Failed               // Error, Enter to retry, Esc to go back
};

// ── Impl ────────────────────────────────────────────────────────

struct InstallWizard::Impl {
    Callbacks callbacks;
    WizardMode mode = WizardMode::Check;

    int selected_path = 0;              // 0=/usr/local/bin, 1=~/.local/bin
    bool remove_config_on_uninstall = false;
    bool remove_self_config = false;    // for clashtui-cpp self-uninstall
    bool initial_check_done = false;   // Whether do_check() has been run
    bool service_status_cached = false;
    bool cached_service_active = false;
    bool cached_service_installed = false;  // whether unit file exists

    // Protected by mutex (written by worker thread, read by renderer)
    std::mutex mtx;
    float progress = 0.0f;
    std::string status_msg;
    std::string error_msg;
    std::string current_version;
    std::string latest_version;
    std::string changelog;
    std::string proxy_info;             // which proxy/mirror is being used
    ReleaseInfo release_info;
    AssetInfo selected_asset;
    PlatformInfo platform;

    // Background thread
    std::atomic<bool> cancel_flag{false};
    std::thread worker;
    bool is_upgrade = false;            // true if upgrading, false if fresh install

    ~Impl() {
        cancel_flag.store(true);
        if (worker.joinable()) worker.join();
    }

    void join_worker() {
        if (worker.joinable()) {
            worker.join();
        }
        cancel_flag.store(false);
    }

    // ── Helpers ─────────────────────────────────────────────────

    void set_mode(WizardMode m) {
        mode = m;
    }

    void set_error(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        error_msg = msg;
        set_mode(WizardMode::Failed);
    }

    void set_status(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        status_msg = msg;
    }

    void post_refresh() {
        if (callbacks.post_refresh) callbacks.post_refresh();
    }

    std::string get_install_path() const {
        if (selected_path == 0) {
            return "/usr/local/bin/mihomo";
        }
        return Config::expand_home("~/.local/bin/mihomo");
    }

    static std::string format_size(int64_t bytes) {
        if (bytes <= 0) return "?";
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        std::ostringstream oss;
        oss.precision(1);
        oss << std::fixed << mb << " MB";
        return oss.str();
    }

    // ── Background workers ──────────────────────────────────────

    void do_fetch_release() {
        join_worker();
        worker = std::thread([this]() {
            set_mode(WizardMode::FetchingRelease);
            set_status(T().install_fetching_release);
            post_refresh();

            try {
                auto release = Installer::fetch_latest_release();
                if (release.version.empty()) {
                    set_error(T().err_download_failed);
                    post_refresh();
                    return;
                }

                auto plat = Installer::detect_platform();
                auto asset = Installer::select_asset(release, plat);
                if (asset.name.empty()) {
                    set_error(T().install_no_asset);
                    post_refresh();
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(mtx);
                    release_info = release;
                    selected_asset = asset;
                    platform = plat;
                    latest_version = release.version;
                    changelog = release.changelog;
                }

                // If upgrading, check whether the version is actually newer
                if (is_upgrade && !current_version.empty()) {
                    if (!Installer::is_newer_version(current_version, release.version)) {
                        set_status(T().install_up_to_date);
                        set_mode(WizardMode::Installed);
                        post_refresh();
                        return;
                    }
                }

                set_mode(WizardMode::ReadyToInstall);
                post_refresh();
            } catch (...) {
                set_error(T().err_download_failed);
                post_refresh();
            }
        });
    }

    void do_download_and_install() {
        join_worker();
        worker = std::thread([this]() {
            // ── 1. Download ─────────────────────────────────────
            set_mode(WizardMode::Downloading);
            {
                std::lock_guard<std::mutex> lock(mtx);
                progress = 0.0f;
                proxy_info = T().install_trying_direct;
            }
            post_refresh();

            std::string tmp_path = "/tmp/clashtui-mihomo-download.gz";

            AssetInfo asset_copy;
            ReleaseInfo release_copy;
            {
                std::lock_guard<std::mutex> lock(mtx);
                asset_copy = selected_asset;
                release_copy = release_info;
            }

            bool ok = Installer::download_with_fallback(
                asset_copy.download_url, tmp_path,
                [this](int64_t received, int64_t total) {
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        progress = total > 0 ? static_cast<float>(received) / static_cast<float>(total) : 0.0f;
                    }
                    post_refresh();  // Call outside lock to avoid deadlock
                },
                &cancel_flag);

            if (cancel_flag.load()) return;

            if (!ok) {
                set_error(T().err_download_failed);
                post_refresh();
                return;
            }

            // ── 2. Verify checksum ─────────────────────────────
            set_mode(WizardMode::Verifying);
            set_status(T().install_verifying);
            post_refresh();

            if (!release_copy.checksums_url.empty()) {
                auto hash = Installer::fetch_checksum_for_file(
                    release_copy.checksums_url, asset_copy.name);
                if (!hash.empty()) {
                    if (!Installer::verify_sha256(tmp_path, hash)) {
                        set_error(T().install_checksum_fail);
                        post_refresh();
                        return;
                    }
                    set_status(T().install_checksum_ok);
                } else {
                    set_status(T().install_checksum_skip);
                }
            } else {
                set_status(T().install_checksum_skip);
            }
            post_refresh();

            if (cancel_flag.load()) return;

            // ── 3. Install binary ───────────────────────────────
            set_mode(WizardMode::Installing);
            set_status(T().install_installing);
            post_refresh();

            bool needs_sudo = (selected_path == 0);
            std::string install_path = get_install_path();

            // Ensure parent directory exists for user-local install
            if (!needs_sudo) {
                auto parent = fs::path(install_path).parent_path();
                std::error_code ec;
                fs::create_directories(parent, ec);
            }

            if (!Installer::install_binary(tmp_path, install_path, needs_sudo)) {
                set_error(T().err_download_failed);
                post_refresh();
                return;
            }

            // ── 4. Update config ────────────────────────────────
            if (callbacks.set_binary_path) {
                callbacks.set_binary_path(install_path);
            }
            if (callbacks.save_config) {
                callbacks.save_config();
            }

            // ── 5. Generate default config if needed ────────────
            if (callbacks.get_config_path) {
                auto cfg_path = callbacks.get_config_path();
                auto expanded = Config::expand_home(cfg_path);
                if (!expanded.empty() && !fs::exists(expanded)) {
                    auto cfg_dir = fs::path(expanded).parent_path();
                    std::error_code ec;
                    fs::create_directories(cfg_dir, ec);
                    Installer::generate_default_config(expanded);
                }
            }

            // ── 6. Service setup ────────────────────────────────
            if (Installer::has_systemd()) {
                set_mode(WizardMode::ServiceSetup);
                post_refresh();
                return;  // Wait for user Y/N
            }

            // No systemd, go straight to complete
            set_mode(WizardMode::Complete);
            set_status(T().install_complete);
            post_refresh();
        });
    }

    void do_create_service() {
        join_worker();
        worker = std::thread([this]() {
            std::string binary_path;
            if (callbacks.get_binary_path) binary_path = callbacks.get_binary_path();
            if (binary_path.empty()) binary_path = "/usr/local/bin/mihomo";
            std::string config_dir;
            if (callbacks.get_config_path) {
                auto cfg = Config::expand_home(callbacks.get_config_path());
                config_dir = fs::path(cfg).parent_path().string();
            }
            if (config_dir.empty()) {
                config_dir = Config::mihomo_dir();
            }

            std::string service_name = "mihomo";
            if (callbacks.get_service_name) {
                service_name = callbacks.get_service_name();
            }

            bool is_system = (selected_path == 0);
            ServiceScope scope = is_system ? ServiceScope::System : ServiceScope::User;

            bool ok = Installer::install_service(binary_path, config_dir, service_name, scope);
            if (ok) {
                set_status(T().service_created);
            } else {
                set_error(T().err_api_failed);
                post_refresh();
                return;
            }

            set_mode(WizardMode::Complete);
            post_refresh();
        });
    }

    void do_uninstall() {
        join_worker();
        worker = std::thread([this]() {
            set_mode(WizardMode::Uninstalling);
            set_status(T().uninstall_stopping);
            post_refresh();

            std::string binary_path;
            if (callbacks.get_binary_path) {
                binary_path = callbacks.get_binary_path();
            }
            if (binary_path.empty()) {
                binary_path = "/usr/local/bin/mihomo";
            }

            std::string service_name = "mihomo";
            if (callbacks.get_service_name) {
                service_name = callbacks.get_service_name();
            }

            bool is_system = (binary_path.find("/usr/") == 0);
            ServiceScope scope = is_system ? ServiceScope::System : ServiceScope::User;

            std::string config_dir = Config::mihomo_dir();

            bool ok = Installer::uninstall(
                binary_path, service_name, scope,
                remove_config_on_uninstall, config_dir,
                [this](UninstallProgress p) {
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        status_msg = p.message;
                    }
                    post_refresh();  // Call outside lock to avoid deadlock
                });

            if (ok) {
                set_status(T().uninstall_complete);
                set_mode(WizardMode::Complete);
            } else {
                set_error(T().uninstall_failed);
            }
            post_refresh();
        });
    }

    // ── Service operations (synchronous, quick) ─────────────────

    void do_service_start() {
        auto name = get_service_name();
        auto scope = get_scope();
        if (Installer::start_service(name, scope)) {
            set_status(T().service_started);
        } else {
            set_status(T().sub_failed);
        }
        refresh_service_status();
        post_refresh();
    }

    void do_service_stop() {
        auto name = get_service_name();
        auto scope = get_scope();
        if (Installer::stop_service(name, scope)) {
            set_status(T().service_stopped);
        } else {
            set_status(T().sub_failed);
        }
        refresh_service_status();
        post_refresh();
    }

    void do_service_install() {
        std::string binary_path;
        if (callbacks.get_binary_path) binary_path = callbacks.get_binary_path();
        if (binary_path.empty()) binary_path = "/usr/local/bin/mihomo";

        std::string config_dir = Config::mihomo_dir();
        if (callbacks.get_config_path) {
            auto cfg = Config::expand_home(callbacks.get_config_path());
            if (!cfg.empty()) config_dir = fs::path(cfg).parent_path().string();
        }

        auto name = get_service_name();
        auto scope = get_scope();
        if (Installer::install_service(binary_path, config_dir, name, scope)) {
            set_status(T().service_created);
        } else {
            set_status(T().sub_failed);
        }
        refresh_service_status();
        post_refresh();
    }

    void do_service_remove() {
        auto name = get_service_name();
        auto scope = get_scope();
        // Dependency: stop service before removing
        if (cached_service_active) {
            Installer::stop_service(name, scope);
        }
        if (Installer::remove_service(name, scope)) {
            set_status(T().service_removed);
        } else {
            set_status(T().sub_failed);
        }
        refresh_service_status();
        post_refresh();
    }

    // ── Self-uninstall (clashtui-cpp itself) ─────────────────────

    void do_uninstall_self() {
        join_worker();
        worker = std::thread([this]() {
            set_mode(WizardMode::Uninstalling);
            post_refresh();

            // Step 1: Stop and remove clashtui-cpp daemon service if exists
            std::string daemon_svc = "clashtui-cpp";
            auto scope = get_scope();
            auto daemon_svc_path = Installer::get_service_file_path(daemon_svc, scope);

            if (fs::exists(daemon_svc_path)) {
                set_status(T().uninstall_stopping);
                post_refresh();
                Installer::stop_service(daemon_svc, scope);

                set_status(T().uninstall_disabling);
                post_refresh();
                Installer::disable_service(daemon_svc, scope);

                set_status(T().uninstall_removing_svc);
                post_refresh();
                Installer::remove_service(daemon_svc, scope);
            }

            // Step 2: Remove the clashtui-cpp binary
            set_status(T().uninstall_removing_bin);
            post_refresh();

            // Find our own binary path
            std::string self_binary;
            {
                std::error_code ec;
                auto exe = fs::read_symlink("/proc/self/exe", ec);
                if (!ec) self_binary = exe.string();
            }

            if (!self_binary.empty() && fs::exists(self_binary)) {
                bool needs_sudo = (self_binary.find("/usr/") == 0);
                if (needs_sudo) {
                    std::string cmd = "sudo rm -f " + shell_quote(self_binary);
                    (void)std::system(cmd.c_str());
                } else {
                    std::error_code ec;
                    fs::remove(self_binary, ec);
                }
            }

            // Step 3: Optionally remove config directory
            if (remove_self_config) {
                set_status(T().uninstall_removing_cfg);
                post_refresh();

                auto config_dir = Config::config_dir();
                std::error_code ec;
                fs::remove_all(config_dir, ec);
            }

            set_status(T().uninstall_self_complete);
            set_mode(WizardMode::Complete);
            post_refresh();
        });
    }

    // ── Perform initial check ───────────────────────────────────

    void do_check() {
        bool installed = false;
        if (callbacks.is_installed) {
            installed = callbacks.is_installed();
        }

        if (installed) {
            if (callbacks.get_version) {
                std::lock_guard<std::mutex> lock(mtx);
                current_version = callbacks.get_version();
            }
            refresh_service_status();
            set_mode(WizardMode::Installed);
        } else {
            // Detect platform for display
            platform = Installer::detect_platform();
            set_mode(WizardMode::NotInstalled);
        }
    }

    // ── Render helpers ──────────────────────────────────────────

    Element render_check() {
        return vbox({
            text(" " + std::string(T().install_checking)) | dim,
        });
    }

    Element render_not_installed() {
        auto plat_str = platform.os + "-" + platform.arch;
        auto opt_system = text("   /usr/local/bin/mihomo (" +
                               std::string(T().install_needs_sudo) + ")");
        auto opt_user = text("   ~/.local/bin/mihomo (" +
                             std::string(T().install_user_only) + ")");

        if (selected_path == 0) {
            opt_system = hbox({text(" > "), opt_system}) | inverted;
            opt_user = hbox({text("   "), opt_user});
        } else {
            opt_system = hbox({text("   "), opt_system});
            opt_user = hbox({text(" > "), opt_user}) | inverted;
        }

        return vbox({
            text(" " + std::string(T().install_not_found)) | color(Color::Yellow),
            hbox({
                text(" " + std::string(T().install_platform) + ": ") | dim,
                text(plat_str),
            }),
            separator(),
            text(" " + std::string(T().install_select_path) + ":") | dim,
            opt_system,
            opt_user,
            separator(),
            text(" Enter = install, Esc = cancel") | dim,
        });
    }

    ServiceScope get_scope() const {
        std::string binary_path;
        if (callbacks.get_binary_path) binary_path = callbacks.get_binary_path();
        bool is_system = (!binary_path.empty() && binary_path.find("/usr/") == 0);
        return is_system ? ServiceScope::System : ServiceScope::User;
    }

    std::string get_service_name() const {
        std::string service_name = "mihomo";
        if (callbacks.get_service_name) service_name = callbacks.get_service_name();
        return service_name;
    }

    void refresh_service_status() {
        auto service_name = get_service_name();
        auto scope = get_scope();
        cached_service_active = Installer::is_service_active(service_name, scope);
        // Check if service unit file exists on disk
        auto svc_path = Installer::get_service_file_path(service_name, scope);
        cached_service_installed = fs::exists(svc_path);
        service_status_cached = true;
    }

    Element render_installed() {
        std::string ver;
        std::string status_copy;
        {
            std::lock_guard<std::mutex> lock(mtx);
            ver = current_version;
            status_copy = status_msg;
        }

        bool active = cached_service_active;
        bool svc_installed = cached_service_installed;
        bool has_sd = Installer::has_systemd();

        Elements content;

        // Header: installed status + service badge
        {
            auto svc_badge = active
                ? text(" [" + std::string(T().service_active) + "] ") | color(Color::Green)
                : svc_installed
                    ? text(" [" + std::string(T().service_inactive) + "] ") | color(Color::Yellow)
                    : text(" [" + std::string(T().service_not_installed) + "] ") | color(Color::GrayDark);
            content.push_back(hbox({
                text(" " + std::string(T().install_installed)) | color(Color::Green),
                filler(),
                has_sd ? svc_badge : text(""),
            }));
        }

        if (!ver.empty()) {
            content.push_back(hbox({
                text(" Version: ") | dim,
                text(ver),
            }));
        }

        // Show status message (e.g. "up to date", "service started")
        if (!status_copy.empty()) {
            content.push_back(text(" " + status_copy) | color(Color::Green));
        }

        // Service management section
        if (has_sd) {
            content.push_back(separator());
            if (svc_installed) {
                // Service is installed: show start/stop toggle
                if (active) {
                    content.push_back(text(" [1] " + std::string(T().service_stop)) | dim);
                } else {
                    content.push_back(text(" [1] " + std::string(T().service_start)) | dim);
                }
                // Service is installed: offer removal
                content.push_back(text(" [2] " + std::string(T().service_remove)) | dim);
            } else {
                // Service not installed: offer install
                content.push_back(text(" [2] " + std::string(T().service_install)) | dim);
            }
        }

        // Update & uninstall section
        content.push_back(separator());
        content.push_back(text(" [U] " + std::string(T().install_check_update)) | dim);
        content.push_back(text(" [X] " + std::string(T().uninstall_title)) | dim);
        content.push_back(text(" [D] " + std::string(T().uninstall_self_title)) | dim);

        // Hint
        content.push_back(separator());
        content.push_back(text(" Esc = back") | dim);

        return vbox(std::move(content));
    }

    Element render_fetching_release() {
        return vbox({
            text(" " + std::string(T().install_fetching_release)) | color(Color::Yellow),
            separator(),
            text(" ...") | dim,
        });
    }

    Element render_ready_to_install() {
        std::string ver, asset_name, chlog;
        int64_t asset_size = 0;
        bool upgrade = false;
        {
            std::lock_guard<std::mutex> lock(mtx);
            ver = latest_version;
            asset_name = selected_asset.name;
            asset_size = selected_asset.size;
            chlog = changelog;
            upgrade = is_upgrade;
        }

        Elements content;

        if (upgrade) {
            content.push_back(hbox({
                text(" " + std::string(T().install_upgrade_available)) | color(Color::Yellow) | bold,
            }));
            content.push_back(hbox({
                text(" " + current_version + " -> ") | dim,
                text(ver) | color(Color::Green),
            }));
        } else {
            content.push_back(hbox({
                text(" " + std::string(T().install_ready)) | color(Color::Green) | bold,
            }));
        }

        content.push_back(hbox({
            text(" Version: ") | dim,
            text(ver),
        }));
        content.push_back(hbox({
            text(" File: ") | dim,
            text(asset_name),
            text(" (" + format_size(asset_size) + ")") | dim,
        }));

        // Show changelog (truncated to a few lines)
        if (!chlog.empty()) {
            content.push_back(separator());
            // Split changelog into lines, show first 5
            std::istringstream iss(chlog);
            std::string line;
            int count = 0;
            while (std::getline(iss, line) && count < 5) {
                if (!line.empty()) {
                    content.push_back(text(" " + line) | dim);
                    ++count;
                }
            }
            if (count == 5) {
                content.push_back(text(" ...") | dim);
            }
        }

        content.push_back(separator());
        content.push_back(text(" " + std::string(T().install_confirm_download)) | dim);
        content.push_back(text(" Esc = cancel") | dim);

        return vbox(std::move(content));
    }

    Element render_downloading() {
        float prog;
        std::string proxy;
        {
            std::lock_guard<std::mutex> lock(mtx);
            prog = progress;
            proxy = proxy_info;
        }

        int pct = static_cast<int>(prog * 100.0f);

        return vbox({
            text(" " + std::string(T().install_downloading)) | color(Color::Yellow),
            separator(),
            hbox({
                text(" " + std::to_string(pct) + "% ") | dim,
                gauge(prog) | flex,
            }),
            text(" " + proxy) | dim,
            separator(),
            text(" Esc = cancel") | dim,
        });
    }

    Element render_verifying() {
        std::string status_copy;
        {
            std::lock_guard<std::mutex> lock(mtx);
            status_copy = status_msg;
        }

        return vbox({
            text(" " + std::string(T().install_verifying)) | color(Color::Yellow),
            text(" " + status_copy) | dim,
        });
    }

    Element render_installing() {
        std::string status_copy;
        {
            std::lock_guard<std::mutex> lock(mtx);
            status_copy = status_msg;
        }

        return vbox({
            text(" " + std::string(T().install_installing)) | color(Color::Yellow),
            text(" " + status_copy) | dim,
        });
    }

    Element render_service_setup() {
        bool is_system = (selected_path == 0);
        auto scope_str = is_system ? T().service_system_level : T().service_user_level;

        return vbox({
            text(" " + std::string(T().service_setup)) | bold,
            separator(),
            text(" " + std::string(T().service_create_prompt)) | dim,
            hbox({
                text(" Type: ") | dim,
                text(scope_str),
            }),
            separator(),
            text(" [Y] " + std::string(T().confirm) + "  [N] " +
                 std::string(T().service_skipped)) | dim,
        });
    }

    Element render_complete() {
        std::string status_copy;
        {
            std::lock_guard<std::mutex> lock(mtx);
            status_copy = status_msg;
        }

        return vbox({
            text(" " + std::string(T().install_complete)) | color(Color::Green) | bold,
            separator(),
            text(" " + status_copy) | dim,
            separator(),
            text(" Enter = OK, Esc = back") | dim,
        });
    }

    Element render_confirm_uninstall() {
        auto opt_cfg = remove_config_on_uninstall
                           ? text(" [x] " + std::string(T().uninstall_remove_config))
                           : text(" [ ] " + std::string(T().uninstall_remove_config));

        return vbox({
            text(" " + std::string(T().uninstall_title)) | color(Color::Red) | bold,
            separator(),
            text(" " + std::string(T().uninstall_confirm)) | color(Color::Yellow),
            separator(),
            opt_cfg | dim,
            separator(),
            text(" [Y] " + std::string(T().confirm) +
                 "  [N] " + std::string(T().cancel)) | dim,
            text(" Up/Down = toggle option") | dim,
        });
    }

    Element render_uninstalling() {
        std::string status_copy;
        {
            std::lock_guard<std::mutex> lock(mtx);
            status_copy = status_msg;
        }

        return vbox({
            text(" " + std::string(T().uninstall_title)) | color(Color::Yellow),
            separator(),
            text(" " + status_copy) | dim,
        });
    }

    Element render_confirm_uninstall_self() {
        std::string cfg_label = std::string(T().uninstall_self_remove_config)
                                + " (" + Config::config_dir() + ")";
        auto opt_cfg = remove_self_config
                           ? text(" [x] " + cfg_label)
                           : text(" [ ] " + cfg_label);

        return vbox({
            text(" " + std::string(T().uninstall_self_title)) | color(Color::Red) | bold,
            separator(),
            text(" " + std::string(T().uninstall_self_confirm)) | color(Color::Yellow),
            separator(),
            opt_cfg | dim,
            separator(),
            text(" [Y] " + std::string(T().confirm) +
                 "  [N] " + std::string(T().cancel)) | dim,
            text(" Up/Down = toggle option") | dim,
        });
    }

    Element render_failed() {
        std::string err;
        {
            std::lock_guard<std::mutex> lock(mtx);
            err = error_msg;
        }

        return vbox({
            text(" Error") | color(Color::Red) | bold,
            separator(),
            text(" " + err) | color(Color::Red),
            separator(),
            text(" Enter = retry, Esc = back") | dim,
        });
    }
};

// ── Constructor / Destructor ────────────────────────────────────

InstallWizard::InstallWizard() : impl_(std::make_unique<Impl>()) {}
InstallWizard::~InstallWizard() = default;

void InstallWizard::set_callbacks(Callbacks cb) {
    impl_->callbacks = std::move(cb);
}

// ── Component ───────────────────────────────────────────────────

Component InstallWizard::component() {
    auto self = impl_.get();

    return Renderer([self](bool /*focused*/) -> Element {
        // Auto-transition from Check mode: run in background thread to avoid blocking UI
        if (self->mode == WizardMode::Check && !self->initial_check_done) {
            self->initial_check_done = true;
            self->join_worker();
            self->worker = std::thread([self]() {
                self->do_check();
            });
        }

        Element body;
        switch (self->mode) {
            case WizardMode::Check:
                body = self->render_check();
                break;
            case WizardMode::NotInstalled:
                body = self->render_not_installed();
                break;
            case WizardMode::Installed:
                body = self->render_installed();
                break;
            case WizardMode::FetchingRelease:
                body = self->render_fetching_release();
                break;
            case WizardMode::ReadyToInstall:
                body = self->render_ready_to_install();
                break;
            case WizardMode::Downloading:
                body = self->render_downloading();
                break;
            case WizardMode::Verifying:
                body = self->render_verifying();
                break;
            case WizardMode::Installing:
                body = self->render_installing();
                break;
            case WizardMode::ServiceSetup:
                body = self->render_service_setup();
                break;
            case WizardMode::Complete:
                body = self->render_complete();
                break;
            case WizardMode::ConfirmUninstall:
                body = self->render_confirm_uninstall();
                break;
            case WizardMode::Uninstalling:
                body = self->render_uninstalling();
                break;
            case WizardMode::ConfirmUninstallSelf:
                body = self->render_confirm_uninstall_self();
                break;
            case WizardMode::Failed:
                body = self->render_failed();
                break;
        }

        return vbox({
            text(" " + std::string(T().install_title)) | bold,
            separator(),
            body,
        }) | border;
    }) | CatchEvent([self](Event event) -> bool {
        auto mode = self->mode;

        // ── Esc: cancel / go back ───────────────────────────────
        if (event == Event::Escape) {
            switch (mode) {
                case WizardMode::Downloading:
                case WizardMode::FetchingRelease:
                case WizardMode::Verifying:
                case WizardMode::Installing:
                case WizardMode::Uninstalling:
                    // Signal cancellation; worker will check cancel_flag and exit
                    self->cancel_flag.store(true);
                    self->join_worker();  // join_worker resets cancel_flag after join
                    self->set_mode(WizardMode::Check);
                    self->initial_check_done = false;  // Re-check on next view
                    {
                        std::lock_guard<std::mutex> lock(self->mtx);
                        self->status_msg.clear();
                        self->error_msg.clear();
                    }
                    return true;

                case WizardMode::ReadyToInstall:
                case WizardMode::ServiceSetup:
                case WizardMode::ConfirmUninstall:
                case WizardMode::ConfirmUninstallSelf:
                case WizardMode::Failed:
                    self->set_mode(WizardMode::Check);
                    self->initial_check_done = false;
                    {
                        std::lock_guard<std::mutex> lock(self->mtx);
                        self->status_msg.clear();
                        self->error_msg.clear();
                    }
                    return true;

                case WizardMode::Complete:
                    self->set_mode(WizardMode::Check);
                    self->initial_check_done = false;
                    self->service_status_cached = false;
                    {
                        std::lock_guard<std::mutex> lock(self->mtx);
                        self->status_msg.clear();
                        self->error_msg.clear();
                    }
                    return true;

                default:
                    // NotInstalled, Installed, Check: let parent handle (close panel)
                    return false;
            }
        }

        // ── Enter ───────────────────────────────────────────────
        if (event == Event::Return) {
            switch (mode) {
                case WizardMode::NotInstalled:
                    self->is_upgrade = false;
                    self->do_fetch_release();
                    return true;

                case WizardMode::ReadyToInstall:
                    self->do_download_and_install();
                    return true;

                case WizardMode::Failed: {
                    // Retry: go back to Check and re-evaluate
                    self->set_mode(WizardMode::Check);
                    self->initial_check_done = false;
                    {
                        std::lock_guard<std::mutex> lock(self->mtx);
                        self->error_msg.clear();
                        self->status_msg.clear();
                    }
                    return true;
                }

                case WizardMode::Complete:
                    self->set_mode(WizardMode::Check);
                    self->initial_check_done = false;
                    self->service_status_cached = false;
                    {
                        std::lock_guard<std::mutex> lock(self->mtx);
                        self->status_msg.clear();
                    }
                    return true;

                default:
                    break;
            }
            return false;
        }

        // ── Arrow Up / k ───────────────────────────────────────
        if (event == Event::ArrowUp ||
            (event.is_character() && event.character() == "k")) {
            if (mode == WizardMode::NotInstalled) {
                if (self->selected_path > 0) self->selected_path--;
                return true;
            }
            if (mode == WizardMode::ConfirmUninstall) {
                self->remove_config_on_uninstall = !self->remove_config_on_uninstall;
                return true;
            }
            if (mode == WizardMode::ConfirmUninstallSelf) {
                self->remove_self_config = !self->remove_self_config;
                return true;
            }
            return false;
        }

        // ── Arrow Down / j ─────────────────────────────────────
        if (event == Event::ArrowDown ||
            (event.is_character() && event.character() == "j")) {
            if (mode == WizardMode::NotInstalled) {
                if (self->selected_path < 1) self->selected_path++;
                return true;
            }
            if (mode == WizardMode::ConfirmUninstall) {
                self->remove_config_on_uninstall = !self->remove_config_on_uninstall;
                return true;
            }
            if (mode == WizardMode::ConfirmUninstallSelf) {
                self->remove_self_config = !self->remove_self_config;
                return true;
            }
            return false;
        }

        // ── Character keys ─────────────────────────────────────
        if (event.is_character()) {
            auto ch = event.character();

            // U/u: Check for updates (from Installed state)
            if ((ch == "U" || ch == "u") && mode == WizardMode::Installed) {
                self->is_upgrade = true;
                self->do_fetch_release();
                return true;
            }

            // 1: Service start/stop toggle (from Installed state)
            if (ch == "1" && mode == WizardMode::Installed) {
                if (Installer::has_systemd() && self->cached_service_installed) {
                    if (self->cached_service_active) {
                        self->do_service_stop();
                    } else {
                        self->do_service_start();
                    }
                    return true;
                }
            }

            // 2: Service install/remove toggle (from Installed state)
            if (ch == "2" && mode == WizardMode::Installed) {
                if (Installer::has_systemd()) {
                    if (self->cached_service_installed) {
                        self->do_service_remove();
                    } else {
                        self->do_service_install();
                    }
                    return true;
                }
            }

            // X/x: Uninstall mihomo (from Installed state)
            if ((ch == "X" || ch == "x") && mode == WizardMode::Installed) {
                self->remove_config_on_uninstall = false;
                self->set_mode(WizardMode::ConfirmUninstall);
                return true;
            }

            // D/d: Uninstall clashtui-cpp (from Installed state)
            if ((ch == "D" || ch == "d") && mode == WizardMode::Installed) {
                self->remove_self_config = false;
                self->set_mode(WizardMode::ConfirmUninstallSelf);
                return true;
            }

            // Y/y: Confirm
            if (ch == "Y" || ch == "y") {
                if (mode == WizardMode::ServiceSetup) {
                    self->do_create_service();
                    return true;
                }
                if (mode == WizardMode::ConfirmUninstall) {
                    self->do_uninstall();
                    return true;
                }
                if (mode == WizardMode::ConfirmUninstallSelf) {
                    self->do_uninstall_self();
                    return true;
                }
            }

            // N/n: Decline
            if (ch == "N" || ch == "n") {
                if (mode == WizardMode::ServiceSetup) {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    self->status_msg = T().service_skipped;
                    self->set_mode(WizardMode::Complete);
                    return true;
                }
                if (mode == WizardMode::ConfirmUninstall) {
                    self->set_mode(WizardMode::Installed);
                    return true;
                }
                if (mode == WizardMode::ConfirmUninstallSelf) {
                    self->set_mode(WizardMode::Installed);
                    return true;
                }
            }
        }

        return false;
    });
}
