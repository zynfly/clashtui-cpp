#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

struct VersionInfo {
    std::string version;
    bool premium = false;
};

struct ClashConfig {
    std::string mode;
    int mixed_port = 0;
    int socks_port = 0;
    int port = 0;
    bool allow_lan = false;
    std::string log_level;
};

struct ProxyNode {
    std::string name;
    std::string type;
    std::string server;
    int port = 0;
    int delay = -1; // -1 = untested, 0 = timeout/fail
    bool alive = true;
    std::vector<int> delay_history;
};

struct ProxyGroup {
    std::string name;
    std::string type; // "Selector", "URLTest", "Fallback", "LoadBalance"
    std::string now;  // currently active proxy name
    std::vector<std::string> all;
};

struct ConnectionStats {
    int active_connections = 0;
    int64_t upload_total = 0;
    int64_t download_total = 0;
    int64_t upload_speed = 0;
    int64_t download_speed = 0;
};

struct DelayResult {
    std::string name;
    int delay = 0; // 0 = failed
    bool success = false;
    std::string error;
};

struct LogEntry {
    std::string type;    // "info", "warning", "error", "debug"
    std::string payload;
};

class MihomoClient {
public:
    explicit MihomoClient(const std::string& host, int port, const std::string& secret);
    ~MihomoClient();

    bool test_connection();
    VersionInfo get_version();
    ClashConfig get_config();
    bool set_mode(const std::string& mode);

    /// Reload mihomo config from a specific YAML file path
    /// PUT /configs {"path": "..."}
    bool reload_config(const std::string& config_path);

    /// Reload config and wait until mihomo has applied it
    /// (polls /proxies until non-empty groups appear, up to max_wait_ms)
    bool reload_config_and_wait(const std::string& config_path, int max_wait_ms = 3000);

    std::map<std::string, ProxyGroup> get_proxy_groups();
    std::map<std::string, ProxyNode> get_proxy_nodes();
    bool select_proxy(const std::string& group, const std::string& proxy);
    DelayResult test_delay(const std::string& proxy_name,
                           const std::string& test_url = "http://www.gstatic.com/generate_204",
                           int timeout_ms = 5000);

    ConnectionStats get_connections();
    bool close_all_connections();

    void stream_logs(const std::string& level,
                     std::function<void(LogEntry)> callback,
                     std::atomic<bool>& stop_flag);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
