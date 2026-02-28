#include "ui/main_screen.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>

using namespace ftxui;

struct MainScreen::Impl {
    Callbacks callbacks;
    std::string mode = "rule";
    bool connected = false;
    std::string lang_label = "中";

    Component content = Renderer([] { return text("Loading..."); });
    Component status_bar = Renderer([] { return text(""); });
    Component container;
};

MainScreen::MainScreen() : impl_(std::make_unique<Impl>()) {}
MainScreen::~MainScreen() = default;

void MainScreen::set_callbacks(Callbacks cb) { impl_->callbacks = std::move(cb); }
void MainScreen::set_mode(const std::string& mode) { impl_->mode = mode; }
void MainScreen::set_connected(bool connected) { impl_->connected = connected; }
void MainScreen::set_language_label(const std::string& label) { impl_->lang_label = label; }
void MainScreen::set_content(Component content) { impl_->content = std::move(content); }
void MainScreen::set_status_bar(Component status_bar) { impl_->status_bar = std::move(status_bar); }

Component MainScreen::component() {
    auto self = impl_.get();

    // Wrap content and status_bar in a container so they can receive focus
    auto layout = Container::Vertical({
        self->content,
        self->status_bar,
    });

    return Renderer(layout, [self, layout] {
        // ── Header ──
        auto mode_btn = [&](const std::string& m, const std::string& label) -> Element {
            bool active = (self->mode == m);
            auto el = text(" " + label + " ");
            if (active) {
                return el | bold | inverted;
            }
            return el | dim;
        };

        auto header = hbox({
            text(" clashtui-cpp ") | bold | color(Color::Cyan),
            separator(),
            mode_btn("global", T().mode_global),
            text(" "),
            mode_btn("rule", T().mode_rule),
            text(" "),
            mode_btn("direct", T().mode_direct),
            filler(),
            text(" " + self->lang_label + " ") | border,
            text(" "),
            self->connected
                ? text("● " + std::string(T().connected)) | color(Color::Green)
                : text("○ " + std::string(T().disconnected)) | color(Color::Red),
            text(" "),
        });

        // ── Footer ──
        auto footer = hbox({
            text(" [S]") | bold,
            text(T().panel_subscription),
            text("  [I]") | bold,
            text(T().install_title),
            text("  [L]") | bold,
            text("Log"),
            text("  [C]") | bold,
            text("Config"),
            text("  [F1-F3]") | bold,
            text("Mode"),
            text("  [Q]") | bold,
            text("Quit"),
            text("  "),
        }) | dim;

        // ── Compose ──
        return vbox({
            header,
            separator(),
            self->content->Render() | flex,
            separator(),
            self->status_bar->Render(),
            footer,
        });
    }) | CatchEvent([self](Event event) -> bool {
        // Global key events
        if (event == Event::F1) {
            if (self->callbacks.on_mode_change) self->callbacks.on_mode_change("global");
            return true;
        }
        if (event == Event::F2) {
            if (self->callbacks.on_mode_change) self->callbacks.on_mode_change("rule");
            return true;
        }
        if (event == Event::F3) {
            if (self->callbacks.on_mode_change) self->callbacks.on_mode_change("direct");
            return true;
        }
        // Ctrl+L: toggle language
        if (event == Event::Special("\x0C")) {
            if (self->callbacks.on_toggle_lang) self->callbacks.on_toggle_lang();
            return true;
        }
        // Q: quit
        if (event.is_character() && event.character() == "q") {
            if (self->callbacks.on_quit) self->callbacks.on_quit();
            return true;
        }
        if (event.is_character() && event.character() == "Q") {
            if (self->callbacks.on_quit) self->callbacks.on_quit();
            return true;
        }
        // S: subscription panel
        if (event.is_character() && (event.character() == "s" || event.character() == "S")) {
            if (self->callbacks.on_panel_switch) self->callbacks.on_panel_switch(1);
            return true;
        }
        // I: install wizard
        if (event.is_character() && (event.character() == "i" || event.character() == "I")) {
            if (self->callbacks.on_panel_switch) self->callbacks.on_panel_switch(3);
            return true;
        }
        // L: log panel
        if (event.is_character() && (event.character() == "l" || event.character() == "L")) {
            if (self->callbacks.on_panel_switch) self->callbacks.on_panel_switch(2);
            return true;
        }
        // C: config panel
        if (event.is_character() && (event.character() == "c" || event.character() == "C")) {
            if (self->callbacks.on_panel_switch) self->callbacks.on_panel_switch(4);
            return true;
        }
        // Esc: back to proxy panel
        if (event == Event::Escape) {
            if (self->callbacks.on_panel_switch) self->callbacks.on_panel_switch(0);
            return true;
        }
        return false;
    });
}
