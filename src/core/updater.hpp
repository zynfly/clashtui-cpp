#pragma once

#include <string>

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

class Updater {
public:
    explicit Updater(const std::string& repo = "zynfly/clashtui-cpp");

    /// Check GitHub for latest release, compare against compiled-in version
    UpdateInfo check_for_update() const;

    /// Get compiled-in version
    static std::string current_version();

    /// Download and apply self-update (replace current binary)
    UpdateResult apply_self_update() const;

    /// Download and apply mihomo update (using config for paths)
    UpdateResult update_mihomo() const;

private:
    std::string repo_;
    static std::string detect_arch_tag();

    /// Get the absolute path of the currently running binary
    static std::string get_self_path();
};
