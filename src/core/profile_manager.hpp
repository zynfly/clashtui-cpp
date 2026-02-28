#pragma once

#include <string>
#include <vector>

class Config;

struct ProfileInfo {
    std::string name;               // "my-sub"
    std::string filename;           // "my-sub.yaml"
    std::string source_url;         // subscription URL
    std::string last_updated;       // ISO timestamp
    bool auto_update = true;
    int update_interval_hours = 24;
    bool is_active = false;
};

class ProfileManager {
public:
    explicit ProfileManager(Config& config);

    /// Directory where profile YAML files are stored
    std::string profiles_dir() const;

    /// List all profiles (scanned from profiles dir + config metadata)
    std::vector<ProfileInfo> list_profiles() const;

    struct AddResult { bool success; std::string error; };
    /// Add a new profile: download subscription and save as YAML
    AddResult add_profile(const std::string& name, const std::string& url);

    struct UpdateResult { bool success; std::string error; bool was_active; };
    /// Re-download and update an existing profile
    UpdateResult update_profile(const std::string& name);

    /// Delete a profile (file + config entry)
    bool delete_profile(const std::string& name);

    /// Switch the active profile
    bool switch_active(const std::string& name);

    /// Get the full path to the active profile YAML
    std::string active_profile_path() const;

    /// Get the name of the active profile
    std::string active_profile_name() const;

    /// Deploy the active profile to the mihomo config directory.
    /// Copies the profile YAML to mihomo's config path so mihomo can load it.
    /// Returns the deployed path (mihomo config path) on success, empty on failure.
    std::string deploy_active_to_mihomo() const;

    /// Set auto-update interval for a profile (0 = disabled)
    bool set_update_interval(const std::string& name, int hours);

    /// Get profiles that are due for automatic update
    std::vector<std::string> profiles_due_for_update() const;

private:
    Config& config_;

    /// Sanitize a profile name to a safe filename
    static std::string sanitize_filename(const std::string& name);

    /// Get the full path for a profile by name
    std::string profile_path(const std::string& name) const;

    /// Get the metadata file path (profiles.yaml in profiles dir)
    std::string metadata_path() const;

    /// Load profile metadata from profiles.yaml
    std::vector<ProfileInfo> load_metadata() const;

    /// Save profile metadata to profiles.yaml
    bool save_metadata(const std::vector<ProfileInfo>& profiles) const;

    /// Get current ISO timestamp
    static std::string now_timestamp();
};
