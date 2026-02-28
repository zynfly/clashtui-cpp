#include "ui/config_panel.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>

using namespace ftxui;

struct ConfigPanel::Impl {
    Callbacks callbacks;

    // Editable fields (string copies for input components)
    std::string host;
    std::string port_str;
    std::string secret;
    std::string timeout_str;
    bool synced = false;

    void sync_from_config() {
        if (synced || !callbacks.get_config) return;
        auto& cfg = callbacks.get_config();
        host = cfg.api_host;
        port_str = std::to_string(cfg.api_port);
        secret = cfg.api_secret;
        timeout_str = std::to_string(cfg.api_timeout_ms);
        synced = true;
    }

    void apply_to_config() {
        if (!callbacks.get_config) return;
        auto& cfg = callbacks.get_config();
        cfg.api_host = host;
        try { cfg.api_port = std::stoi(port_str); } catch (...) {}
        cfg.api_secret = secret;
        try { cfg.api_timeout_ms = std::stoi(timeout_str); } catch (...) {}
        if (callbacks.save_config) callbacks.save_config();
        if (callbacks.on_config_changed) callbacks.on_config_changed();
    }
};

ConfigPanel::ConfigPanel() : impl_(std::make_unique<Impl>()) {}
ConfigPanel::~ConfigPanel() = default;

void ConfigPanel::set_callbacks(Callbacks cb) { impl_->callbacks = std::move(cb); }

Component ConfigPanel::component() {
    auto self = impl_.get();

    auto host_input = Input(&self->host, "127.0.0.1");
    auto port_input = Input(&self->port_str, "9090");
    auto secret_input = Input(&self->secret, "secret");
    auto timeout_input = Input(&self->timeout_str, "5000");

    auto container = Container::Vertical({
        host_input,
        port_input,
        secret_input,
        timeout_input,
    });

    return Renderer(container, [self, container] {
        self->sync_from_config();

        auto make_row = [](const std::string& label, Element input) {
            return hbox({
                text(" " + label + ": ") | size(WIDTH, EQUAL, 16),
                input | flex,
            });
        };

        auto children = container->ChildAt(0);

        return vbox({
            text(" Config") | bold,
            separator(),
            text(" API Connection") | bold | dim,
            make_row("Host", container->ChildAt(0)->Render()),
            make_row("Port", container->ChildAt(1)->Render()),
            make_row("Secret", container->ChildAt(2)->Render()),
            make_row("Timeout(ms)", container->ChildAt(3)->Render()),
            separator(),
            text(" Display") | bold | dim,
            hbox({
                text(" Language: ") | size(WIDTH, EQUAL, 16),
                text(current_lang == Lang::ZH ? "中文" : "English"),
                text("  (Ctrl+L to toggle)") | dim,
            }),
            separator(),
            text(" Press Ctrl+S to save") | dim,
        }) | border;
    }) | CatchEvent([self](Event event) -> bool {
        // Ctrl+S: save
        if (event == Event::Special("\x13")) {
            self->apply_to_config();
            return true;
        }
        return false;
    });
}
