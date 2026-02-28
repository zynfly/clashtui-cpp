#pragma once

#include "api/mihomo_client.hpp"

#include <ftxui/component/component.hpp>
#include <memory>
#include <functional>

class ProxyPanel {
public:
    struct Callbacks {
        std::function<std::map<std::string, ProxyGroup>()> get_groups;
        std::function<std::map<std::string, ProxyNode>()> get_nodes;
        std::function<bool(const std::string& group, const std::string& proxy)> select_proxy;
        std::function<DelayResult(const std::string& name)> test_delay;
    };

    ProxyPanel();
    ~ProxyPanel();

    void set_callbacks(Callbacks cb);
    void refresh_data();

    ftxui::Component component();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
