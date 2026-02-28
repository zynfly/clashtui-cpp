#include "api/mihomo_client.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <chrono>
#include <thread>

using json = nlohmann::json;

struct MihomoClient::Impl {
    std::string host;
    int port;
    std::string secret;
    int timeout_sec = 5;

    std::unique_ptr<httplib::Client> make_client() {
        auto cli = std::make_unique<httplib::Client>(host, port);
        cli->set_connection_timeout(timeout_sec, 0);
        cli->set_read_timeout(timeout_sec, 0);
        cli->set_write_timeout(timeout_sec, 0);
        return cli;
    }

    httplib::Headers auth_headers() {
        httplib::Headers headers;
        if (!secret.empty()) {
            headers.emplace("Authorization", "Bearer " + secret);
        }
        headers.emplace("Content-Type", "application/json");
        return headers;
    }
};

MihomoClient::MihomoClient(const std::string& host, int port, const std::string& secret)
    : impl_(std::make_unique<Impl>()) {
    impl_->host = host;
    impl_->port = port;
    impl_->secret = secret;
}

MihomoClient::~MihomoClient() = default;

// ── Connection test ─────────────────────────────────────────

bool MihomoClient::test_connection() {
    try {
        auto cli = impl_->make_client();
        auto res = cli->Get("/version", impl_->auth_headers());
        return res && res->status == 200;
    } catch (...) {
        return false;
    }
}

// ── Version ─────────────────────────────────────────────────

VersionInfo MihomoClient::get_version() {
    VersionInfo info;
    try {
        auto cli = impl_->make_client();
        auto res = cli->Get("/version", impl_->auth_headers());
        if (res && res->status == 200) {
            auto j = json::parse(res->body);
            info.version = j.value("version", "");
            info.premium = j.value("premium", false);
        }
    } catch (...) {}
    return info;
}

// ── Config ──────────────────────────────────────────────────

ClashConfig MihomoClient::get_config() {
    ClashConfig cfg;
    try {
        auto cli = impl_->make_client();
        auto res = cli->Get("/configs", impl_->auth_headers());
        if (res && res->status == 200) {
            auto j = json::parse(res->body);
            cfg.mode = j.value("mode", "rule");
            cfg.mixed_port = j.value("mixed-port", 0);
            cfg.socks_port = j.value("socks-port", 0);
            cfg.port = j.value("port", 0);
            cfg.allow_lan = j.value("allow-lan", false);
            cfg.log_level = j.value("log-level", "info");
        }
    } catch (...) {}
    return cfg;
}

bool MihomoClient::set_mode(const std::string& mode) {
    try {
        auto cli = impl_->make_client();
        json body;
        body["mode"] = mode;
        auto res = cli->Patch("/configs", impl_->auth_headers(),
                              body.dump(), "application/json");
        return res && (res->status == 200 || res->status == 204);
    } catch (...) {
        return false;
    }
}

bool MihomoClient::reload_config(const std::string& config_path) {
    try {
        auto cli = impl_->make_client();
        // Longer timeout for config reload
        cli->set_read_timeout(10, 0);
        json body;
        body["path"] = config_path;
        auto res = cli->Put("/configs", impl_->auth_headers(),
                            body.dump(), "application/json");
        return res && (res->status == 200 || res->status == 204);
    } catch (...) {
        return false;
    }
}

bool MihomoClient::reload_config_and_wait(const std::string& config_path, int max_wait_ms) {
    if (!reload_config(config_path)) return false;

    // Poll until mihomo has loaded the new config (non-empty proxy groups)
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= max_wait_ms) break;

        auto groups = get_proxy_groups();
        if (!groups.empty()) return true;
    }
    return true; // Return true anyway since reload itself succeeded
}

// ── Proxy management ────────────────────────────────────────

std::map<std::string, ProxyGroup> MihomoClient::get_proxy_groups() {
    std::map<std::string, ProxyGroup> groups;
    try {
        auto cli = impl_->make_client();
        auto res = cli->Get("/proxies", impl_->auth_headers());
        if (!res || res->status != 200) return groups;

        auto j = json::parse(res->body);
        auto& proxies = j["proxies"];

        for (auto& [name, proxy] : proxies.items()) {
            std::string type = proxy.value("type", "");
            // Only include group types
            if (type == "Selector" || type == "URLTest" ||
                type == "Fallback" || type == "LoadBalance") {
                ProxyGroup group;
                group.name = name;
                group.type = type;
                group.now = proxy.value("now", "");
                if (proxy.contains("all")) {
                    for (auto& item : proxy["all"]) {
                        group.all.push_back(item.get<std::string>());
                    }
                }
                groups[name] = std::move(group);
            }
        }
    } catch (...) {}
    return groups;
}

std::map<std::string, ProxyNode> MihomoClient::get_proxy_nodes() {
    std::map<std::string, ProxyNode> nodes;
    try {
        auto cli = impl_->make_client();
        auto res = cli->Get("/proxies", impl_->auth_headers());
        if (!res || res->status != 200) return nodes;

        auto j = json::parse(res->body);
        auto& proxies = j["proxies"];

        for (auto& [name, proxy] : proxies.items()) {
            std::string type = proxy.value("type", "");
            // Skip group types, only include actual nodes
            if (type == "Selector" || type == "URLTest" ||
                type == "Fallback" || type == "LoadBalance") {
                continue;
            }

            ProxyNode node;
            node.name = name;
            node.type = type;
            node.server = proxy.value("server", "");
            node.port = proxy.value("port", 0);
            node.alive = proxy.value("alive", true);
            if (proxy.contains("history") && proxy["history"].is_array()) {
                for (auto& h : proxy["history"]) {
                    int d = h.value("delay", 0);
                    node.delay_history.push_back(d);
                }
                if (!node.delay_history.empty()) {
                    node.delay = node.delay_history.back();
                }
            }

            nodes[name] = std::move(node);
        }
    } catch (...) {}
    return nodes;
}

bool MihomoClient::select_proxy(const std::string& group, const std::string& proxy) {
    try {
        auto cli = impl_->make_client();
        json body;
        body["name"] = proxy;
        std::string path = "/proxies/" + group;
        auto res = cli->Put(path, impl_->auth_headers(),
                            body.dump(), "application/json");
        return res && (res->status == 200 || res->status == 204);
    } catch (...) {
        return false;
    }
}

DelayResult MihomoClient::test_delay(const std::string& proxy_name,
                                      const std::string& test_url,
                                      int timeout_ms) {
    DelayResult result;
    result.name = proxy_name;
    try {
        auto cli = impl_->make_client();
        // Set a longer read timeout for delay testing
        cli->set_read_timeout(timeout_ms / 1000 + 2, 0);

        std::string path = "/proxies/" + proxy_name + "/delay"
                           "?url=" + test_url +
                           "&timeout=" + std::to_string(timeout_ms);
        auto res = cli->Get(path, impl_->auth_headers());

        if (res && res->status == 200) {
            auto j = json::parse(res->body);
            result.delay = j.value("delay", 0);
            result.success = result.delay > 0;
        } else if (res) {
            result.success = false;
            try {
                auto j = json::parse(res->body);
                result.error = j.value("message", "timeout");
            } catch (...) {
                result.error = "timeout";
            }
        } else {
            result.error = "connection failed";
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    } catch (...) {
        result.error = "unknown error";
    }
    return result;
}

// ── Connections ─────────────────────────────────────────────

ConnectionStats MihomoClient::get_connections() {
    ConnectionStats stats;
    try {
        auto cli = impl_->make_client();
        auto res = cli->Get("/connections", impl_->auth_headers());
        if (res && res->status == 200) {
            auto j = json::parse(res->body);
            stats.upload_total = j.value("uploadTotal", (int64_t)0);
            stats.download_total = j.value("downloadTotal", (int64_t)0);
            if (j.contains("connections") && j["connections"].is_array()) {
                stats.active_connections = static_cast<int>(j["connections"].size());
            }
        }
    } catch (...) {}
    return stats;
}

bool MihomoClient::close_all_connections() {
    try {
        auto cli = impl_->make_client();
        auto res = cli->Delete("/connections", impl_->auth_headers());
        return res && (res->status == 200 || res->status == 204);
    } catch (...) {
        return false;
    }
}

// ── Log streaming (SSE) ────────────────────────────────────

void MihomoClient::stream_logs(const std::string& level,
                                std::function<void(LogEntry)> callback,
                                std::atomic<bool>& stop_flag) {
    try {
        auto cli = impl_->make_client();
        // SSE connections are long-lived
        cli->set_read_timeout(0, 0); // no timeout

        std::string path = "/logs?level=" + level;
        std::string buffer;

        cli->Get(path, impl_->auth_headers(),
            [&](const httplib::Response& /*response*/) -> bool {
                return !stop_flag.load();
            },
            [&](const char* data, size_t data_length) -> bool {
                if (stop_flag.load()) return false;

                buffer.append(data, data_length);

                // Process complete lines
                size_t pos;
                while ((pos = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, pos);
                    buffer.erase(0, pos + 1);

                    // Skip empty lines
                    if (line.empty() || line == "\r") continue;

                    // Remove trailing \r
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }

                    // Parse JSON log entry
                    try {
                        auto j = json::parse(line);
                        LogEntry entry;
                        entry.type = j.value("type", "info");
                        entry.payload = j.value("payload", "");
                        callback(std::move(entry));
                    } catch (...) {
                        // Not valid JSON, skip
                    }
                }

                return !stop_flag.load();
            });
    } catch (...) {
        // Connection closed or error, just return
    }
}
