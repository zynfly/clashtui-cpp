#include "ui/proxy_panel.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>
#include <sstream>
#include <thread>
#include <mutex>

using namespace ftxui;

// ── Delay color helper ──────────────────────────────────────

static Color delay_color(int delay) {
    if (delay <= 0) return Color::GrayDark;
    if (delay < 100) return Color::Green;
    if (delay <= 300) return Color::Yellow;
    return Color::Red;
}

static std::string delay_badge(int delay) {
    if (delay == -1) return "[?]";
    if (delay == 0) return "[✗]";
    return "[" + std::to_string(delay) + "ms]";
}

// ── Mini sparkline from delay history ───────────────────────

static std::string sparkline(const std::vector<int>& history, int count = 5) {
    static const char* blocks[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

    if (history.empty()) return "";

    // Take last `count` entries
    auto start = history.size() > (size_t)count
                     ? history.end() - count
                     : history.begin();
    std::vector<int> recent(start, history.end());

    int max_val = *std::max_element(recent.begin(), recent.end());
    if (max_val <= 0) max_val = 1;

    std::string result;
    for (int v : recent) {
        if (v <= 0) {
            result += "▁";
        } else {
            int idx = std::min(7, (int)((double)v / max_val * 7));
            result += blocks[idx];
        }
    }
    return result;
}

// ── ProxyPanel::Impl ────────────────────────────────────────

struct ProxyPanel::Impl {
    Callbacks callbacks;

    // Data
    std::vector<std::string> group_names;
    std::map<std::string, ProxyGroup> groups;
    std::map<std::string, ProxyNode> nodes;
    std::mutex data_mutex;

    // Selection state
    int selected_group = 0;
    int selected_node = 0;
    int focus_column = 0; // 0=groups, 1=nodes, 2=details

    // Get current group
    const ProxyGroup* current_group() {
        if (group_names.empty() || selected_group < 0 ||
            selected_group >= (int)group_names.size()) {
            return nullptr;
        }
        auto it = groups.find(group_names[selected_group]);
        if (it == groups.end()) return nullptr;
        return &it->second;
    }

    // Get nodes of current group
    std::vector<std::string> current_node_names() {
        auto* g = current_group();
        if (!g) return {};
        return g->all;
    }

    // Get selected node info
    const ProxyNode* current_node() {
        auto names = current_node_names();
        if (names.empty() || selected_node < 0 ||
            selected_node >= (int)names.size()) {
            return nullptr;
        }
        auto it = nodes.find(names[selected_node]);
        if (it == nodes.end()) return nullptr;
        return &it->second;
    }

    // ── Left column: group list ─────────────────────────────
    Element render_groups() {
        Elements items;
        for (int i = 0; i < (int)group_names.size(); ++i) {
            auto it = groups.find(group_names[i]);
            if (it == groups.end()) continue;
            auto& g = it->second;

            // Type badge
            std::string badge;
            if (g.type == "Selector") badge = "[SELECT]";
            else if (g.type == "URLTest") badge = "[URL-TEST]";
            else if (g.type == "Fallback") badge = "[FALLBACK]";
            else if (g.type == "LoadBalance") badge = "[LB]";
            else badge = "[" + g.type + "]";

            auto line = hbox({
                text(g.name) | flex,
                text(" " + badge) | dim,
            });

            if (i == selected_group) {
                if (focus_column == 0) {
                    line = line | inverted | bold;
                } else {
                    line = line | bold;
                }
            }

            items.push_back(line);
        }

        if (items.empty()) {
            items.push_back(text("  (no groups)") | dim);
        }

        return vbox(std::move(items)) | vscroll_indicator | frame |
               border | size(WIDTH, GREATER_THAN, 20);
    }

    // ── Center column: node list ────────────────────────────
    Element render_nodes() {
        auto* g = current_group();
        if (!g) {
            return vbox({text("  (no group selected)") | dim}) | border | flex;
        }

        Elements items;
        auto& node_names = g->all;

        for (int i = 0; i < (int)node_names.size(); ++i) {
            auto nit = nodes.find(node_names[i]);

            // Active indicator
            std::string prefix = (node_names[i] == g->now) ? "▶ " : "  ";

            // Delay badge
            int d = -1;
            if (nit != nodes.end()) d = nit->second.delay;
            std::string badge = delay_badge(d);

            auto line = hbox({
                text(prefix),
                text(node_names[i]) | flex,
                text(" " + badge) | color(delay_color(d)),
            });

            if (i == selected_node) {
                if (focus_column == 1) {
                    line = line | inverted | bold;
                } else {
                    line = line | bold;
                }
            }

            items.push_back(line);
        }

        if (items.empty()) {
            items.push_back(text("  (empty group)") | dim);
        }

        return vbox(std::move(items)) | vscroll_indicator | frame |
               border | flex;
    }

    // ── Right column: node details ──────────────────────────
    Element render_details() {
        auto* node = current_node();
        if (!node) {
            return vbox({text("  (no node selected)") | dim}) | border |
                   size(WIDTH, GREATER_THAN, 25);
        }

        Elements items;
        items.push_back(text(" " + node->name) | bold);
        items.push_back(separator());
        items.push_back(hbox({text(" Type: ") | dim, text(node->type)}));

        if (!node->server.empty()) {
            items.push_back(hbox({text(" Server: ") | dim, text(node->server)}));
        }
        if (node->port > 0) {
            items.push_back(hbox({text(" Port: ") | dim, text(std::to_string(node->port))}));
        }

        // Delay
        std::string d_str = delay_badge(node->delay);
        items.push_back(hbox({
            text(" Delay: ") | dim,
            text(d_str) | color(delay_color(node->delay)),
        }));

        // Alive
        items.push_back(hbox({
            text(" Alive: ") | dim,
            node->alive ? text("yes") | color(Color::Green)
                        : text("no") | color(Color::Red),
        }));

        // Delay history sparkline
        if (!node->delay_history.empty()) {
            items.push_back(separator());
            items.push_back(text(" Delay History:") | dim);
            items.push_back(text(" " + sparkline(node->delay_history)));
        }

        return vbox(std::move(items)) | border |
               size(WIDTH, GREATER_THAN, 25);
    }
};

ProxyPanel::ProxyPanel() : impl_(std::make_unique<Impl>()) {}
ProxyPanel::~ProxyPanel() = default;

void ProxyPanel::set_callbacks(Callbacks cb) { impl_->callbacks = std::move(cb); }

void ProxyPanel::refresh_data() {
    if (!impl_->callbacks.get_groups || !impl_->callbacks.get_nodes) return;

    auto groups = impl_->callbacks.get_groups();
    auto nodes = impl_->callbacks.get_nodes();

    std::lock_guard<std::mutex> lock(impl_->data_mutex);
    impl_->groups = std::move(groups);
    impl_->nodes = std::move(nodes);

    // Rebuild sorted group names
    impl_->group_names.clear();
    for (auto& [name, _] : impl_->groups) {
        impl_->group_names.push_back(name);
    }
    std::sort(impl_->group_names.begin(), impl_->group_names.end());

    // Clamp selections
    if (impl_->selected_group >= (int)impl_->group_names.size()) {
        impl_->selected_group = std::max(0, (int)impl_->group_names.size() - 1);
    }
    auto node_names = impl_->current_node_names();
    if (impl_->selected_node >= (int)node_names.size()) {
        impl_->selected_node = std::max(0, (int)node_names.size() - 1);
    }
}

Component ProxyPanel::component() {
    auto self = impl_.get();

    return Renderer([self] {
        std::lock_guard<std::mutex> lock(self->data_mutex);
        return hbox({
            self->render_groups(),
            self->render_nodes(),
            self->render_details(),
        });
    }) | CatchEvent([self](Event event) -> bool {
        // Tab / Left / Right: switch focus column
        if (event == Event::Tab) {
            self->focus_column = (self->focus_column + 1) % 3;
            return true;
        }
        if (event == Event::ArrowLeft) {
            if (self->focus_column > 0) self->focus_column--;
            return true;
        }
        if (event == Event::ArrowRight) {
            if (self->focus_column < 2) self->focus_column++;
            return true;
        }

        // Up/Down or j/k: navigate within current column
        auto navigate = [&](int delta) {
            std::lock_guard<std::mutex> lock(self->data_mutex);
            if (self->focus_column == 0) {
                int max = (int)self->group_names.size() - 1;
                self->selected_group = std::clamp(self->selected_group + delta, 0, std::max(0, max));
                self->selected_node = 0; // reset node selection on group change
            } else if (self->focus_column == 1) {
                auto names = self->current_node_names();
                int max = (int)names.size() - 1;
                self->selected_node = std::clamp(self->selected_node + delta, 0, std::max(0, max));
            }
        };

        if (event == Event::ArrowUp || (event.is_character() && event.character() == "k")) {
            navigate(-1);
            return true;
        }
        if (event == Event::ArrowDown || (event.is_character() && event.character() == "j")) {
            navigate(1);
            return true;
        }

        // Enter: select proxy
        if (event == Event::Return) {
            if (self->focus_column == 1 && self->callbacks.select_proxy) {
                std::lock_guard<std::mutex> lock(self->data_mutex);
                auto* g = self->current_group();
                auto names = self->current_node_names();
                if (g && self->selected_node >= 0 && self->selected_node < (int)names.size()) {
                    std::string group = g->name;
                    std::string proxy = names[self->selected_node];
                    // Run in background to avoid blocking UI
                    std::thread([self, group, proxy]() {
                        self->callbacks.select_proxy(group, proxy);
                        // Update the group's now field locally
                        std::lock_guard<std::mutex> lock(self->data_mutex);
                        auto it = self->groups.find(group);
                        if (it != self->groups.end()) {
                            it->second.now = proxy;
                        }
                    }).detach();
                }
            }
            return true;
        }

        // T: test selected node delay
        if (event.is_character() && (event.character() == "t" || event.character() == "T")) {
            if (self->callbacks.test_delay) {
                std::lock_guard<std::mutex> lock(self->data_mutex);
                auto names = self->current_node_names();
                if (self->selected_node >= 0 && self->selected_node < (int)names.size()) {
                    std::string name = names[self->selected_node];
                    std::thread([self, name]() {
                        auto result = self->callbacks.test_delay(name);
                        std::lock_guard<std::mutex> lock(self->data_mutex);
                        auto it = self->nodes.find(name);
                        if (it != self->nodes.end()) {
                            it->second.delay = result.success ? result.delay : 0;
                            it->second.delay_history.push_back(it->second.delay);
                        }
                    }).detach();
                }
            }
            return true;
        }

        // A: test all nodes in current group
        if (event.is_character() && (event.character() == "a" || event.character() == "A")) {
            if (self->callbacks.test_delay) {
                std::lock_guard<std::mutex> lock(self->data_mutex);
                auto names = self->current_node_names();
                for (const auto& name : names) {
                    std::thread([self, name]() {
                        auto result = self->callbacks.test_delay(name);
                        std::lock_guard<std::mutex> lock(self->data_mutex);
                        auto it = self->nodes.find(name);
                        if (it != self->nodes.end()) {
                            it->second.delay = result.success ? result.delay : 0;
                            it->second.delay_history.push_back(it->second.delay);
                        }
                    }).detach();
                }
            }
            return true;
        }

        // R: refresh data
        if (event.is_character() && (event.character() == "r" || event.character() == "R")) {
            if (self->callbacks.get_groups && self->callbacks.get_nodes) {
                std::thread([self]() {
                    auto groups = self->callbacks.get_groups();
                    auto nodes = self->callbacks.get_nodes();
                    std::lock_guard<std::mutex> lock(self->data_mutex);
                    self->groups = std::move(groups);
                    self->nodes = std::move(nodes);
                    self->group_names.clear();
                    for (auto& [name, _] : self->groups) {
                        self->group_names.push_back(name);
                    }
                    std::sort(self->group_names.begin(), self->group_names.end());
                }).detach();
            }
            return true;
        }

        return false;
    });
}
