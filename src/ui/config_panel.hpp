#pragma once

#include "core/config.hpp"

#include <ftxui/component/component.hpp>
#include <functional>
#include <memory>

class ConfigPanel {
public:
    struct Callbacks {
        std::function<AppConfig&()> get_config;
        std::function<void()> save_config;
        std::function<void()> on_config_changed; // called when API settings change
    };

    ConfigPanel();
    ~ConfigPanel();

    void set_callbacks(Callbacks cb);

    ftxui::Component component();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
