#include "daemon/daemon.hpp"

#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;

Daemon::Daemon(Config& config)
    : config_(config), profile_mgr_(config) {}

Daemon::~Daemon() {
    request_stop();
    cleanup_socket();
}

std::string Daemon::socket_path() const {
    std::string dir = Config::config_dir();
    if (dir.empty()) return "";
    return dir + "/clashtui.sock";
}

void Daemon::cleanup_socket() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    std::string path = socket_path();
    if (!path.empty()) {
        unlink(path.c_str());
    }
}

bool Daemon::start_ipc_server() {
    std::string path = socket_path();
    if (path.empty()) return false;

    // Clean up any existing socket
    unlink(path.c_str());

    // Ensure directory exists
    try {
        fs::create_directories(fs::path(path).parent_path());
    } catch (...) {
        return false;
    }

    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) return false;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // Restrict permissions to owner only
    chmod(path.c_str(), 0600);

    if (listen(socket_fd_, 5) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        unlink(path.c_str());
        return false;
    }

    return true;
}

void Daemon::ipc_loop() {
    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;

    while (!stop_flag_.load()) {
        int ret = poll(&pfd, 1, 500); // 500ms timeout
        if (ret <= 0) continue;

        if (pfd.revents & POLLIN) {
            int client_fd = accept(socket_fd_, nullptr, nullptr);
            if (client_fd < 0) continue;

            // Read a single JSON line
            std::string buffer;
            char c;
            while (read(client_fd, &c, 1) == 1) {
                if (c == '\n') break;
                buffer += c;
                if (buffer.size() > 65536) break; // prevent abuse
            }

            if (!buffer.empty()) {
                std::string response = handle_command(buffer);
                response += "\n";
                // Write response (ignore partial writes for simplicity)
                ssize_t total = 0;
                while (total < (ssize_t)response.size()) {
                    ssize_t n = write(client_fd, response.data() + total,
                                      response.size() - total);
                    if (n <= 0) break;
                    total += n;
                }
            }

            close(client_fd);
        }
    }
}

std::string Daemon::handle_command(const std::string& json_line) {
    try {
        auto req = json::parse(json_line);
        std::string cmd = req.value("cmd", "");

        if (cmd == "status") {
            json data;
            data["mihomo_running"] = process_mgr_.is_running();
            data["mihomo_pid"] = process_mgr_.child_pid();
            data["active_profile"] = profile_mgr_.active_profile_name();
            return json({{"ok", true}, {"data", data}}).dump();
        }

        if (cmd == "profile_list") {
            auto profiles = profile_mgr_.list_profiles();
            json arr = json::array();
            for (const auto& p : profiles) {
                arr.push_back({
                    {"name", p.name},
                    {"filename", p.filename},
                    {"source_url", p.source_url},
                    {"last_updated", p.last_updated},
                    {"auto_update", p.auto_update},
                    {"update_interval_hours", p.update_interval_hours},
                    {"is_active", p.is_active}
                });
            }
            return json({{"ok", true}, {"data", arr}}).dump();
        }

        if (cmd == "profile_add") {
            std::string name = req.value("name", "");
            std::string url = req.value("url", "");
            auto result = profile_mgr_.add_profile(name, url);
            if (result.success) {
                return json({{"ok", true}}).dump();
            }
            return json({{"ok", false}, {"error", result.error}}).dump();
        }

        if (cmd == "profile_update") {
            std::string name = req.value("name", "");
            auto result = profile_mgr_.update_profile(name);
            if (result.success) {
                if (result.was_active) {
                    reload_mihomo();
                }
                return json({{"ok", true}}).dump();
            }
            return json({{"ok", false}, {"error", result.error}}).dump();
        }

        if (cmd == "profile_delete") {
            std::string name = req.value("name", "");
            if (profile_mgr_.delete_profile(name)) {
                return json({{"ok", true}}).dump();
            }
            return json({{"ok", false}, {"error", "Failed to delete profile"}}).dump();
        }

        if (cmd == "profile_switch") {
            std::string name = req.value("name", "");
            if (profile_mgr_.switch_active(name)) {
                reload_mihomo();
                return json({{"ok", true}}).dump();
            }
            return json({{"ok", false}, {"error", "Failed to switch profile"}}).dump();
        }

        if (cmd == "mihomo_start") {
            std::string binary = Config::expand_home(config_.data().mihomo_binary_path);
            std::string config_dir = fs::path(
                Config::expand_home(config_.data().mihomo_config_path)).parent_path().string();
            if (process_mgr_.start(binary, {"-d", config_dir})) {
                wait_for_mihomo();
                return json({{"ok", true}}).dump();
            }
            return json({{"ok", false}, {"error", "Failed to start mihomo"}}).dump();
        }

        if (cmd == "mihomo_stop") {
            if (process_mgr_.stop()) {
                return json({{"ok", true}}).dump();
            }
            return json({{"ok", false}, {"error", "Failed to stop mihomo"}}).dump();
        }

        if (cmd == "mihomo_restart") {
            if (process_mgr_.restart()) {
                wait_for_mihomo();
                reload_mihomo();
                return json({{"ok", true}}).dump();
            }
            return json({{"ok", false}, {"error", "Failed to restart mihomo"}}).dump();
        }

        return json({{"ok", false}, {"error", "Unknown command: " + cmd}}).dump();

    } catch (const std::exception& e) {
        return json({{"ok", false}, {"error", std::string("Parse error: ") + e.what()}}).dump();
    }
}

bool Daemon::reload_mihomo() {
    std::string deployed = profile_mgr_.deploy_active_to_mihomo();
    if (deployed.empty() || !client_) return false;
    return client_->reload_config(deployed);
}

bool Daemon::wait_for_mihomo(int timeout_sec) {
    if (!client_) {
        client_ = std::make_unique<MihomoClient>(
            config_.data().api_host,
            config_.data().api_port,
            config_.data().api_secret
        );
    }

    for (int i = 0; i < timeout_sec * 10; ++i) {
        if (client_->test_connection()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

void Daemon::auto_update_loop() {
    while (!stop_flag_.load()) {
        // Check every 60 seconds
        for (int i = 0; i < 600 && !stop_flag_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (stop_flag_.load()) break;

        auto due = profile_mgr_.profiles_due_for_update();
        for (const auto& name : due) {
            if (stop_flag_.load()) break;
            auto result = profile_mgr_.update_profile(name);
            if (result.success && result.was_active) {
                reload_mihomo();
            }
        }
    }
}

void Daemon::request_stop() {
    stop_flag_.store(true);
}

int Daemon::run() {
    // 1. Start IPC server
    if (!start_ipc_server()) {
        return 1;
    }

    // 2. Start mihomo process
    std::string binary = Config::expand_home(config_.data().mihomo_binary_path);
    std::string config_path = Config::expand_home(config_.data().mihomo_config_path);
    std::string config_dir = fs::path(config_path).parent_path().string();

    process_mgr_.set_auto_restart(true);
    if (!binary.empty() && fs::exists(binary)) {
        process_mgr_.start(binary, {"-d", config_dir});
    }

    // 3. Wait for mihomo API
    if (process_mgr_.is_running()) {
        wait_for_mihomo();

        // 4. Deploy and load active profile if set
        std::string deployed = profile_mgr_.deploy_active_to_mihomo();
        if (!deployed.empty() && client_) {
            client_->reload_config(deployed);
        }
    }

    // 5. Start auto-update thread
    auto_update_thread_ = std::thread(&Daemon::auto_update_loop, this);

    // 6. IPC main loop
    ipc_loop();

    // 7. Cleanup
    stop_flag_.store(true);

    if (auto_update_thread_.joinable()) {
        auto_update_thread_.join();
    }

    process_mgr_.stop();
    cleanup_socket();

    return 0;
}
