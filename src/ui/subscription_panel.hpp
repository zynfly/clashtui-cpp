#pragma once

#include "core/profile_manager.hpp"

#include <ftxui/component/component.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SubscriptionPanel {
public:
    struct Callbacks {
        // Daemon-backed (preferred)
        std::function<bool()> is_daemon_available;
        std::function<std::vector<ProfileInfo>()> list_profiles;
        std::function<bool(const std::string&, const std::string&, std::string&)> add_profile;
        std::function<bool(const std::string&, std::string&)> update_profile;
        std::function<bool(const std::string&, std::string&)> delete_profile;
        std::function<bool(const std::string&, std::string&)> switch_profile;
        std::function<std::string()> get_active_profile;
        std::function<bool(const std::string&, int)> set_update_interval;

        std::function<void()> post_refresh;
    };

    SubscriptionPanel();
    ~SubscriptionPanel();

    void set_callbacks(Callbacks cb);
    void refresh_profiles();

    ftxui::Component component();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
