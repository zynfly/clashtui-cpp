#include "ui/main_screen.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/component_base.hpp>

using namespace ftxui;

struct MainScreen::Impl {
    Callbacks callbacks;
    std::string mode = "rule";
    bool connected = false;
    std::string lang_label = "中";

    Component content = Renderer([] { return text("Loading..."); });
    Component status_bar = Renderer([] { return text(""); });

    // Custom component: handles global shortcuts AFTER child gets first chance.
    class ScreenComponent : public ComponentBase {
    public:
        explicit ScreenComponent(Impl* impl) : impl_(impl) {}

        bool Focusable() const override {
            for (auto& child : children_) {
                if (child->Focusable()) return true;
            }
            return false;
        }

        Element OnRender() override {
            auto mode_btn = [&](const std::string& m, const std::string& label) -> Element {
                bool active = (impl_->mode == m);
                auto el = text(" " + label + " ");
                if (active) return el | bold | inverted;
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
                text(" " + impl_->lang_label + " ") | border,
                text(" "),
                impl_->connected
                    ? text("● " + std::string(T().connected)) | color(Color::Green)
                    : text("○ " + std::string(T().disconnected)) | color(Color::Red),
                text(" "),
            });

            auto footer = hbox({
                text(" [S]") | bold,
                text(T().panel_subscription),
                text("  [I]") | bold,
                text(T().install_title),
                text("  [L]") | bold,
                text("Log"),
                text("  [C]") | bold,
                text("Config"),
                text("  [Alt+1-3]") | bold,
                text("Mode"),
                text("  [Q]") | bold,
                text("Quit"),
                text("  "),
            }) | dim;

            return vbox({
                header,
                separator(),
                impl_->content->Render() | flex,
                separator(),
                impl_->status_bar->Render(),
                footer,
            });
        }

        bool OnEvent(Event event) override {
            // Phase 1: Truly global keys (always intercept first)
            // Alt+1/2/3 for mode switching (terminal sends ESC + digit)
            if (event == Event::Special("\x1b""1")) {
                if (impl_->callbacks.on_mode_change) impl_->callbacks.on_mode_change("global");
                return true;
            }
            if (event == Event::Special("\x1b""2")) {
                if (impl_->callbacks.on_mode_change) impl_->callbacks.on_mode_change("rule");
                return true;
            }
            if (event == Event::Special("\x1b""3")) {
                if (impl_->callbacks.on_mode_change) impl_->callbacks.on_mode_change("direct");
                return true;
            }
            if (event == Event::Special("\x0C")) {
                if (impl_->callbacks.on_toggle_lang) impl_->callbacks.on_toggle_lang();
                return true;
            }

            // Phase 2: Let child components (content/Tab → active panel) handle first
            if (ComponentBase::OnEvent(event)) {
                return true;
            }

            // Phase 3: Fallback global shortcuts (sub-panel didn't handle)
            if (event.is_character()) {
                auto ch = event.character();
                if (ch == "q" || ch == "Q") {
                    if (impl_->callbacks.on_quit) impl_->callbacks.on_quit();
                    return true;
                }
                if (ch == "s" || ch == "S") {
                    if (impl_->callbacks.on_panel_switch) impl_->callbacks.on_panel_switch(1);
                    return true;
                }
                if (ch == "i" || ch == "I") {
                    if (impl_->callbacks.on_panel_switch) impl_->callbacks.on_panel_switch(3);
                    return true;
                }
                if (ch == "l" || ch == "L") {
                    if (impl_->callbacks.on_panel_switch) impl_->callbacks.on_panel_switch(2);
                    return true;
                }
                if (ch == "c" || ch == "C") {
                    if (impl_->callbacks.on_panel_switch) impl_->callbacks.on_panel_switch(4);
                    return true;
                }
            }
            if (event == Event::Escape) {
                if (impl_->callbacks.on_panel_switch) impl_->callbacks.on_panel_switch(0);
                return true;
            }
            return false;
        }

    private:
        Impl* impl_;
    };
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
    auto comp = Make<Impl::ScreenComponent>(impl_.get());
    comp->Add(impl_->content);
    return comp;
}
