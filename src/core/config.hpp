#pragma once

#include <string>
#include <vector>

struct SubscriptionInfo {
    std::string name;
    std::string url;
    std::string last_updated;
    bool auto_update = true;
    int update_interval_hours = 24;
};

struct AppConfig {
    // API connection
    std::string api_host = "127.0.0.1";
    int api_port = 9090;
    std::string api_secret;
    int api_timeout_ms = 5000;

    // Display
    std::string language = "zh";
    std::string theme = "default";

    // Subscriptions
    std::vector<SubscriptionInfo> subscriptions;

    // Mihomo
    std::string mihomo_config_path;
    std::string mihomo_binary_path = "/usr/local/bin/mihomo";
    std::string mihomo_service_name = "mihomo";

    // Proxy
    bool proxy_enabled = false;  // remembered on/off state for shell init

    // Profiles (daemon mode)
    std::string active_profile;  // name of the currently active profile
};

class Config {
public:
    Config();
    ~Config();

    bool load();
    bool save();

    AppConfig& data();
    const AppConfig& data() const;

    static bool is_privileged();
    static std::string config_dir();
    static std::string config_path();
    static std::string mihomo_dir();
    static std::string default_mihomo_config_path();
    static std::string expand_home(const std::string& path);

private:
    AppConfig config_;
};
