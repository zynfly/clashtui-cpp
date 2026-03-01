#include "core/profile_manager.hpp"
#include "core/config.hpp"
#include "core/subscription.hpp"

#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

ProfileManager::ProfileManager(Config& config) : config_(config) {}

std::string ProfileManager::profiles_dir() const {
    std::string dir = Config::config_dir();
    if (!dir.empty()) {
        std::string path = dir + "/profiles";
        if (fs::exists(path)) {
            return path;
        }
    }

    // Fall back to system profiles dir (read-only for non-root)
    std::string sys_path = Config::system_config_dir() + "/profiles";
    if (fs::exists(sys_path)) {
        return sys_path;
    }

    // Neither exists; return default user path (will be created on write)
    if (!dir.empty()) return dir + "/profiles";
    return "";
}

std::string ProfileManager::sanitize_filename(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            result += c;
        } else if (c == ' ') {
            result += '_';
        }
        // skip other chars
    }
    if (result.empty()) result = "profile";
    return result;
}

std::string ProfileManager::profile_path(const std::string& name) const {
    std::string dir = profiles_dir();
    if (dir.empty()) return "";
    return dir + "/" + sanitize_filename(name) + ".yaml";
}

std::string ProfileManager::metadata_path() const {
    std::string dir = profiles_dir();
    if (dir.empty()) return "";
    return dir + "/profiles.yaml";
}

std::string ProfileManager::now_timestamp() {
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::vector<ProfileInfo> ProfileManager::load_metadata() const {
    std::vector<ProfileInfo> profiles;
    std::string path = metadata_path();
    if (path.empty() || !fs::exists(path)) return profiles;

    try {
        YAML::Node root = YAML::LoadFile(path);
        if (!root.IsSequence()) return profiles;

        for (const auto& node : root) {
            ProfileInfo info;
            info.name = node["name"].as<std::string>("");
            info.filename = node["filename"].as<std::string>("");
            info.source_url = node["source_url"].as<std::string>("");
            info.last_updated = node["last_updated"].as<std::string>("");
            info.auto_update = node["auto_update"].as<bool>(true);
            info.update_interval_hours = node["update_interval_hours"].as<int>(24);
            info.is_active = (info.name == config_.data().active_profile);
            profiles.push_back(std::move(info));
        }
    } catch (...) {}

    return profiles;
}

bool ProfileManager::save_metadata(const std::vector<ProfileInfo>& profiles) const {
    std::string dir = profiles_dir();
    std::string path = metadata_path();
    if (dir.empty() || path.empty()) return false;

    try {
        fs::create_directories(dir);

        YAML::Emitter out;
        out << YAML::BeginSeq;
        for (const auto& p : profiles) {
            out << YAML::BeginMap;
            out << YAML::Key << "name" << YAML::Value << p.name;
            out << YAML::Key << "filename" << YAML::Value << p.filename;
            out << YAML::Key << "source_url" << YAML::Value << p.source_url;
            out << YAML::Key << "last_updated" << YAML::Value << p.last_updated;
            out << YAML::Key << "auto_update" << YAML::Value << p.auto_update;
            out << YAML::Key << "update_interval_hours" << YAML::Value << p.update_interval_hours;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        // Atomic write: write to temp file, then rename
        std::string tmp = path + ".tmp";
        std::ofstream fout(tmp);
        if (!fout.is_open()) return false;
        fout << out.c_str();
        fout.close();
        if (fout.fail()) return false;
        fs::rename(tmp, path);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<ProfileInfo> ProfileManager::list_profiles() const {
    return load_metadata();
}

ProfileManager::AddResult ProfileManager::add_profile(const std::string& name, const std::string& url) {
    AddResult result;

    if (name.empty()) {
        result.success = false;
        result.error = "Profile name cannot be empty";
        return result;
    }

    if (url.empty()) {
        result.success = false;
        result.error = "URL cannot be empty";
        return result;
    }

    // Check for duplicate name
    auto existing = load_metadata();
    for (const auto& p : existing) {
        if (p.name == name) {
            result.success = false;
            result.error = "Profile already exists: " + name;
            return result;
        }
    }

    // Download subscription content
    auto dl = Subscription::download(url);
    if (!dl.success) {
        result.success = false;
        result.error = dl.error;
        return result;
    }

    // Ensure profiles directory exists
    std::string dir = profiles_dir();
    if (dir.empty()) {
        result.success = false;
        result.error = "Cannot determine profiles directory";
        return result;
    }

    try {
        fs::create_directories(dir);
    } catch (...) {
        result.success = false;
        result.error = "Cannot create profiles directory";
        return result;
    }

    // Save YAML file
    std::string filename = sanitize_filename(name) + ".yaml";
    std::string filepath = dir + "/" + filename;
    if (!Subscription::save_to_file(dl.content, filepath)) {
        result.success = false;
        result.error = "Failed to save profile file";
        return result;
    }

    // Add to metadata
    ProfileInfo info;
    info.name = name;
    info.filename = filename;
    info.source_url = url;
    info.last_updated = now_timestamp();
    info.auto_update = true;
    info.update_interval_hours = 24;
    existing.push_back(info);

    if (!save_metadata(existing)) {
        result.success = false;
        result.error = "Failed to save profile metadata";
        return result;
    }

    result.success = true;
    return result;
}

ProfileManager::UpdateResult ProfileManager::update_profile(const std::string& name) {
    UpdateResult result;
    result.was_active = (name == config_.data().active_profile);

    auto profiles = load_metadata();
    auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const ProfileInfo& p) { return p.name == name; });

    if (it == profiles.end()) {
        result.success = false;
        result.error = "Profile not found: " + name;
        return result;
    }

    // Re-download
    auto dl = Subscription::download(it->source_url);
    if (!dl.success) {
        result.success = false;
        result.error = dl.error;
        return result;
    }

    // Overwrite file
    std::string filepath = profiles_dir() + "/" + it->filename;
    if (!Subscription::save_to_file(dl.content, filepath)) {
        result.success = false;
        result.error = "Failed to save profile file";
        return result;
    }

    // Update metadata
    it->last_updated = now_timestamp();
    if (!save_metadata(profiles)) {
        result.success = false;
        result.error = "Failed to save metadata";
        return result;
    }

    result.success = true;
    return result;
}

bool ProfileManager::delete_profile(const std::string& name) {
    auto profiles = load_metadata();
    auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const ProfileInfo& p) { return p.name == name; });

    if (it == profiles.end()) return false;

    // Delete the YAML file
    std::string filepath = profiles_dir() + "/" + it->filename;
    try {
        fs::remove(filepath);
    } catch (...) {}

    // Remove from metadata
    profiles.erase(it);
    save_metadata(profiles);

    // Clear active if deleted
    if (config_.data().active_profile == name) {
        config_.data().active_profile.clear();
        config_.save();
    }

    return true;
}

bool ProfileManager::set_update_interval(const std::string& name, int hours) {
    auto profiles = load_metadata();
    auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const ProfileInfo& p) { return p.name == name; });

    if (it == profiles.end()) return false;

    it->auto_update = (hours > 0);
    it->update_interval_hours = hours > 0 ? hours : 0;
    return save_metadata(profiles);
}

bool ProfileManager::switch_active(const std::string& name) {
    // Verify profile exists
    auto profiles = load_metadata();
    auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const ProfileInfo& p) { return p.name == name; });

    if (it == profiles.end()) return false;

    // Verify file exists
    std::string filepath = profiles_dir() + "/" + it->filename;
    if (!fs::exists(filepath)) return false;

    config_.data().active_profile = name;
    return config_.save();
}

std::string ProfileManager::active_profile_path() const {
    std::string name = config_.data().active_profile;
    if (name.empty()) return "";

    auto profiles = load_metadata();
    auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const ProfileInfo& p) { return p.name == name; });

    if (it == profiles.end()) return "";
    return profiles_dir() + "/" + it->filename;
}

std::string ProfileManager::active_profile_name() const {
    return config_.data().active_profile;
}

std::string ProfileManager::deploy_active_to_mihomo() const {
    std::string src = active_profile_path();
    if (src.empty() || !fs::exists(src)) return "";

    std::string mihomo_cfg = Config::expand_home(config_.data().mihomo_config_path);
    if (mihomo_cfg.empty()) return "";

    try {
        // Ensure mihomo config directory exists
        fs::create_directories(fs::path(mihomo_cfg).parent_path());
        // Atomic deploy: write to temp file, then rename
        std::string tmp = mihomo_cfg + ".tmp";
        fs::copy_file(src, tmp, fs::copy_options::overwrite_existing);
        fs::rename(tmp, mihomo_cfg);
        return mihomo_cfg;
    } catch (...) {
        return "";
    }
}

std::vector<std::string> ProfileManager::profiles_due_for_update() const {
    std::vector<std::string> due;
    auto profiles = load_metadata();

    auto now = std::time(nullptr);

    for (const auto& p : profiles) {
        if (!p.auto_update || p.source_url.empty()) continue;

        // Parse last_updated timestamp
        std::tm tm = {};
        std::istringstream ss(p.last_updated);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (ss.fail()) {
            // Can't parse timestamp, consider it due
            due.push_back(p.name);
            continue;
        }

        auto last = std::mktime(&tm);
        auto diff_hours = std::difftime(now, last) / 3600.0;
        if (diff_hours >= p.update_interval_hours) {
            due.push_back(p.name);
        }
    }

    return due;
}
