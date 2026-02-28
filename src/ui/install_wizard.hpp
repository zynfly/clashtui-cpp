#pragma once

#include <ftxui/component/component.hpp>
#include <functional>
#include <memory>
#include <string>

class InstallWizard {
public:
    struct Callbacks {
        std::function<bool()> is_installed;
        std::function<std::string()> get_version;
        std::function<std::string()> get_binary_path;
        std::function<std::string()> get_config_path;
        std::function<std::string()> get_service_name;
        std::function<void(const std::string&)> set_binary_path;
        std::function<void()> save_config;
        std::function<void()> post_refresh;
    };

    InstallWizard();
    ~InstallWizard();

    void set_callbacks(Callbacks cb);

    ftxui::Component component();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
