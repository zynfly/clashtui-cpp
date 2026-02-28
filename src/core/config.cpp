#include "core/config.hpp"

#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;

std::string Config::expand_home(const std::string& path) {
    if (!path.empty() && path[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

Config::Config() {
    config_.mihomo_config_path = default_mihomo_config_path();
}

Config::~Config() = default;

bool Config::is_privileged() {
    return geteuid() == 0;
}

std::string Config::config_dir() {
    if (is_privileged()) {
        return "/etc/clashtui-cpp";
    }
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/clashtui-cpp";
}

std::string Config::mihomo_dir() {
    std::string dir = config_dir();
    if (dir.empty()) return "";
    return dir + "/mihomo";
}

std::string Config::default_mihomo_config_path() {
    std::string dir = mihomo_dir();
    if (dir.empty()) return "";
    return dir + "/config.yaml";
}

std::string Config::config_path() {
    std::string dir = config_dir();
    if (dir.empty()) return "";
    return dir + "/config.yaml";
}

bool Config::load() {
    std::string path = config_path();
    if (path.empty() || !fs::exists(path)) {
        return false;
    }

    try {
        YAML::Node root = YAML::LoadFile(path);

        // API section
        if (auto api = root["api"]) {
            config_.api_host = api["host"].as<std::string>(config_.api_host);
            config_.api_port = api["port"].as<int>(config_.api_port);
            config_.api_secret = api["secret"].as<std::string>(config_.api_secret);
            config_.api_timeout_ms = api["timeout_ms"].as<int>(config_.api_timeout_ms);
        }

        // Display section
        if (auto display = root["display"]) {
            config_.language = display["language"].as<std::string>(config_.language);
            config_.theme = display["theme"].as<std::string>(config_.theme);
        }

        // Subscriptions section
        if (auto subs = root["subscriptions"]) {
            config_.subscriptions.clear();
            for (const auto& sub : subs) {
                SubscriptionInfo info;
                info.name = sub["name"].as<std::string>("");
                info.url = sub["url"].as<std::string>("");
                info.last_updated = sub["last_updated"].as<std::string>("");
                info.auto_update = sub["auto_update"].as<bool>(true);
                info.update_interval_hours = sub["update_interval_hours"].as<int>(24);
                config_.subscriptions.push_back(std::move(info));
            }
        }

        // Mihomo section
        if (auto mihomo = root["mihomo"]) {
            config_.mihomo_config_path = mihomo["config_path"].as<std::string>(config_.mihomo_config_path);
            config_.mihomo_binary_path = mihomo["binary_path"].as<std::string>(config_.mihomo_binary_path);
            config_.mihomo_service_name = mihomo["service_name"].as<std::string>(config_.mihomo_service_name);

            // Migrate old-style path to unified layout
            if (config_.mihomo_config_path == "~/.config/mihomo/config.yaml" ||
                config_.mihomo_config_path == expand_home("~/.config/mihomo/config.yaml")) {
                config_.mihomo_config_path = default_mihomo_config_path();
            }
        }

        // Profiles section
        if (auto profiles = root["profiles"]) {
            config_.active_profile = profiles["active"].as<std::string>(config_.active_profile);
        }

        return true;
    } catch (...) {
        // Parse failed, use defaults
        return false;
    }
}

bool Config::save() {
    std::string dir = config_dir();
    std::string path = config_path();
    if (dir.empty() || path.empty()) return false;

    try {
        fs::create_directories(dir);

        YAML::Emitter out;
        out << YAML::BeginMap;

        // API section
        out << YAML::Key << "api" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "host" << YAML::Value << config_.api_host;
        out << YAML::Key << "port" << YAML::Value << config_.api_port;
        out << YAML::Key << "secret" << YAML::Value << config_.api_secret;
        out << YAML::Key << "timeout_ms" << YAML::Value << config_.api_timeout_ms;
        out << YAML::EndMap;

        // Display section
        out << YAML::Key << "display" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "language" << YAML::Value << config_.language;
        out << YAML::Key << "theme" << YAML::Value << config_.theme;
        out << YAML::EndMap;

        // Subscriptions section
        out << YAML::Key << "subscriptions" << YAML::Value << YAML::BeginSeq;
        for (const auto& sub : config_.subscriptions) {
            out << YAML::BeginMap;
            out << YAML::Key << "name" << YAML::Value << sub.name;
            out << YAML::Key << "url" << YAML::Value << sub.url;
            out << YAML::Key << "last_updated" << YAML::Value << sub.last_updated;
            out << YAML::Key << "auto_update" << YAML::Value << sub.auto_update;
            out << YAML::Key << "update_interval_hours" << YAML::Value << sub.update_interval_hours;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        // Mihomo section
        out << YAML::Key << "mihomo" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "config_path" << YAML::Value << config_.mihomo_config_path;
        out << YAML::Key << "binary_path" << YAML::Value << config_.mihomo_binary_path;
        out << YAML::Key << "service_name" << YAML::Value << config_.mihomo_service_name;
        out << YAML::EndMap;

        // Profiles section
        out << YAML::Key << "profiles" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "active" << YAML::Value << config_.active_profile;
        out << YAML::EndMap;

        out << YAML::EndMap;

        std::ofstream fout(path);
        if (!fout.is_open()) return false;
        fout << out.c_str();
        return true;
    } catch (...) {
        return false;
    }
}

AppConfig& Config::data() { return config_; }
const AppConfig& Config::data() const { return config_; }
