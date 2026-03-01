#include "daemon/ipc_client.hpp"
#include "core/config.hpp"

#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

using json = nlohmann::json;

std::string DaemonClient::socket_path() const {
    // Try user-specific path first
    std::string dir = Config::config_dir();
    if (!dir.empty()) {
        std::string path = dir + "/clashtui.sock";
        if (access(path.c_str(), F_OK) == 0) {
            return path;
        }
    }

    // Fall back to system path (daemon running as root)
    std::string sys_path = Config::system_config_dir() + "/clashtui.sock";
    if (access(sys_path.c_str(), F_OK) == 0) {
        return sys_path;
    }

    // Neither exists; return default user path
    if (!dir.empty()) return dir + "/clashtui.sock";
    return "";
}

json DaemonClient::send_command(const json& cmd) {
    std::string path = socket_path();
    if (path.empty()) return json();

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return json();

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return json();
    }

    // Set read timeout
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Send command
    std::string msg = cmd.dump() + "\n";
    ssize_t total = 0;
    while (total < (ssize_t)msg.size()) {
        ssize_t n = write(fd, msg.data() + total, msg.size() - total);
        if (n <= 0) {
            close(fd);
            return json();
        }
        total += n;
    }

    // Read response
    std::string buffer;
    char c;
    while (read(fd, &c, 1) == 1) {
        if (c == '\n') break;
        buffer += c;
        if (buffer.size() > 65536) break;
    }

    close(fd);

    if (buffer.empty()) return json();

    try {
        return json::parse(buffer);
    } catch (...) {
        return json();
    }
}

bool DaemonClient::is_daemon_running() {
    auto resp = send_command({{"cmd", "status"}});
    return !resp.empty() && resp.value("ok", false);
}

std::vector<ProfileInfo> DaemonClient::list_profiles() {
    std::vector<ProfileInfo> profiles;
    auto resp = send_command({{"cmd", "profile_list"}});
    if (resp.empty() || !resp.value("ok", false)) return profiles;

    try {
        for (const auto& item : resp["data"]) {
            ProfileInfo info;
            info.name = item.value("name", "");
            info.filename = item.value("filename", "");
            info.source_url = item.value("source_url", "");
            info.last_updated = item.value("last_updated", "");
            info.auto_update = item.value("auto_update", true);
            info.update_interval_hours = item.value("update_interval_hours", 24);
            info.is_active = item.value("is_active", false);
            profiles.push_back(std::move(info));
        }
    } catch (...) {}

    return profiles;
}

bool DaemonClient::add_profile(const std::string& name, const std::string& url, std::string& err) {
    auto resp = send_command({{"cmd", "profile_add"}, {"name", name}, {"url", url}});
    if (resp.empty()) {
        err = "Cannot connect to daemon";
        return false;
    }
    if (resp.value("ok", false)) return true;
    err = resp.value("error", "Unknown error");
    return false;
}

bool DaemonClient::update_profile(const std::string& name, std::string& err) {
    auto resp = send_command({{"cmd", "profile_update"}, {"name", name}});
    if (resp.empty()) {
        err = "Cannot connect to daemon";
        return false;
    }
    if (resp.value("ok", false)) return true;
    err = resp.value("error", "Unknown error");
    return false;
}

bool DaemonClient::delete_profile(const std::string& name, std::string& err) {
    auto resp = send_command({{"cmd", "profile_delete"}, {"name", name}});
    if (resp.empty()) {
        err = "Cannot connect to daemon";
        return false;
    }
    if (resp.value("ok", false)) return true;
    err = resp.value("error", "Unknown error");
    return false;
}

bool DaemonClient::switch_profile(const std::string& name, std::string& err) {
    auto resp = send_command({{"cmd", "profile_switch"}, {"name", name}});
    if (resp.empty()) {
        err = "Cannot connect to daemon";
        return false;
    }
    if (resp.value("ok", false)) return true;
    err = resp.value("error", "Unknown error");
    return false;
}

std::string DaemonClient::get_active_profile() {
    auto status = get_status();
    return status.active_profile;
}

DaemonClient::DaemonStatus DaemonClient::get_status() {
    DaemonStatus status;
    auto resp = send_command({{"cmd", "status"}});
    if (resp.empty() || !resp.value("ok", false)) return status;

    try {
        auto& data = resp["data"];
        status.mihomo_running = data.value("mihomo_running", false);
        status.mihomo_pid = data.value("mihomo_pid", -1);
        status.active_profile = data.value("active_profile", "");
    } catch (...) {}

    return status;
}

bool DaemonClient::mihomo_start(std::string& err) {
    auto resp = send_command({{"cmd", "mihomo_start"}});
    if (resp.empty()) {
        err = "Cannot connect to daemon";
        return false;
    }
    if (resp.value("ok", false)) return true;
    err = resp.value("error", "Unknown error");
    return false;
}

bool DaemonClient::mihomo_stop(std::string& err) {
    auto resp = send_command({{"cmd", "mihomo_stop"}});
    if (resp.empty()) {
        err = "Cannot connect to daemon";
        return false;
    }
    if (resp.value("ok", false)) return true;
    err = resp.value("error", "Unknown error");
    return false;
}

bool DaemonClient::mihomo_restart(std::string& err) {
    auto resp = send_command({{"cmd", "mihomo_restart"}});
    if (resp.empty()) {
        err = "Cannot connect to daemon";
        return false;
    }
    if (resp.value("ok", false)) return true;
    err = resp.value("error", "Unknown error");
    return false;
}
