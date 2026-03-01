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

    /// Atomically replace a binary file using rename().
    /// Writes new_binary to a temp file in the same dir as target_path,
    /// then renames (atomic inode swap). The old inode remains valid for
    /// any process that has the file open/mapped.
    /// Returns empty string on success, or error message on failure.
    static std::string atomic_replace_binary(const std::string& new_binary,
                                             const std::string& target_path);

private:
    std::string repo_;
    static std::string detect_arch_tag();

    /// Get the absolute path of the currently running binary
    static std::string get_self_path();
};
