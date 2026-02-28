#include "ui/subscription_panel.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <thread>
#include <mutex>
#include <chrono>

using namespace ftxui;

struct SubscriptionPanel::Impl {
    Callbacks callbacks;

    int selected = 0;

    // Dialog state
    bool show_add_dialog = false;
    bool show_delete_confirm = false;
    bool show_edit_dialog = false;
    std::string input_name;
    std::string input_url;

    // Cached profile list
    std::vector<ProfileInfo> profiles;
    std::mutex profiles_mutex;

    // Notification
    std::string notification;
    std::chrono::steady_clock::time_point notification_time;
    std::mutex notify_mutex;

    void set_notification(const std::string& msg) {
        std::lock_guard<std::mutex> lock(notify_mutex);
        notification = msg;
        notification_time = std::chrono::steady_clock::now();
    }

    std::string get_notification() {
        std::lock_guard<std::mutex> lock(notify_mutex);
        if (notification.empty()) return "";
        auto elapsed = std::chrono::steady_clock::now() - notification_time;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 3) {
            notification.clear();
            return "";
        }
        return notification;
    }

    void refresh_profiles() {
        if (callbacks.list_profiles) {
            std::lock_guard<std::mutex> lock(profiles_mutex);
            profiles = callbacks.list_profiles();
        }
    }
};

SubscriptionPanel::SubscriptionPanel() : impl_(std::make_unique<Impl>()) {}
SubscriptionPanel::~SubscriptionPanel() = default;

void SubscriptionPanel::set_callbacks(Callbacks cb) { impl_->callbacks = std::move(cb); }

Component SubscriptionPanel::component() {
    auto self = impl_.get();

    // Input components for add/edit dialog
    auto name_input = Input(&self->input_name, "Name");
    auto url_input = Input(&self->input_url, "URL");

    auto dialog_container = Container::Vertical({name_input, url_input});

    return Renderer(dialog_container, [self, dialog_container] {
        // Notification overlay
        std::string notify = self->get_notification();

        // Get profiles
        std::vector<ProfileInfo> profiles;
        {
            std::lock_guard<std::mutex> lock(self->profiles_mutex);
            profiles = self->profiles;
        }

        std::string active_name;
        if (self->callbacks.get_active_profile) {
            active_name = self->callbacks.get_active_profile();
        }

        // Check daemon status
        bool daemon_ok = false;
        if (self->callbacks.is_daemon_available) {
            daemon_ok = self->callbacks.is_daemon_available();
        }

        // Main list
        Elements rows;
        rows.push_back(hbox({
            text(" ") | size(WIDTH, EQUAL, 3),
            text(" Name") | bold | size(WIDTH, EQUAL, 20),
            text(" URL") | bold | flex,
            text(" Last Updated") | bold | size(WIDTH, EQUAL, 22),
        }) | inverted);
        rows.push_back(separator());

        for (int i = 0; i < (int)profiles.size(); ++i) {
            auto& p = profiles[i];
            bool is_active = (p.name == active_name);

            // Truncate URL
            std::string url_display = p.source_url;
            if (url_display.size() > 40) {
                url_display = url_display.substr(0, 37) + "...";
            }

            std::string active_mark = is_active ? "[*]" : "   ";

            auto row = hbox({
                text(active_mark) | size(WIDTH, EQUAL, 3),
                text(" " + p.name) | size(WIDTH, EQUAL, 20),
                text(" " + url_display) | flex,
                text(" " + p.last_updated) | size(WIDTH, EQUAL, 22),
            });

            if (i == self->selected) {
                row = row | inverted | bold;
            }
            if (is_active) {
                row = row | color(Color::Green);
            }
            rows.push_back(row);
        }

        if (profiles.empty()) {
            rows.push_back(text("  " + std::string(T().profile_none)) | dim);
        }

        // Footer with keybindings
        auto footer = hbox({
            text(" [A]") | bold, text(T().sub_add),
            text("  [U]") | bold, text(T().sub_update),
            text("  [D]") | bold, text(T().sub_delete),
            text("  [Enter]") | bold, text(T().profile_switch),
            text("  [Esc]") | bold, text("Back"),
        }) | dim;

        Elements content;
        content.push_back(text(" " + std::string(T().panel_subscription)) | bold);

        // Daemon status indicator
        if (!daemon_ok) {
            content.push_back(
                text(" " + std::string(T().profile_no_daemon)) | dim | color(Color::Yellow)
            );
        }

        content.push_back(separator());
        content.push_back(vbox(std::move(rows)) | flex);
        content.push_back(separator());
        content.push_back(footer);

        // Add dialog overlay
        if (self->show_add_dialog) {
            auto dialog = vbox({
                text(std::string(" ") + T().sub_add) | bold,
                separator(),
                hbox({text(" Name: "), dialog_container->Render()}),
                separator(),
                text(" Enter: " + std::string(T().confirm) + "  Esc: " + T().cancel) | dim,
            }) | border | size(WIDTH, LESS_THAN, 60) | center;

            content.push_back(dialog);
        }

        // Delete confirm dialog
        if (self->show_delete_confirm) {
            std::string del_name;
            if (self->selected >= 0 && self->selected < (int)profiles.size()) {
                del_name = profiles[self->selected].name;
            }
            auto dialog = vbox({
                text(" " + std::string(T().sub_delete) + ": " + del_name + "?") | bold,
                separator(),
                text(" Enter: " + std::string(T().confirm) + "  Esc: " + T().cancel) | dim,
            }) | border | center;

            content.push_back(dialog);
        }

        // Notification toast
        if (!notify.empty()) {
            content.push_back(
                text(" " + notify + " ") | bold | inverted | center
            );
        }

        return vbox(std::move(content)) | border;
    }) | CatchEvent([self](Event event) -> bool {
        // Handle dialog modes
        if (self->show_add_dialog) {
            if (event == Event::Return) {
                if (!self->input_name.empty() && !self->input_url.empty()) {
                    std::string name = self->input_name;
                    std::string url = self->input_url;
                    self->show_add_dialog = false;
                    self->input_name.clear();
                    self->input_url.clear();

                    // Run in background thread
                    std::thread([self, name, url]() {
                        self->set_notification(std::string(T().sub_downloading));
                        if (self->callbacks.post_refresh) self->callbacks.post_refresh();

                        std::string err;
                        bool ok = false;
                        if (self->callbacks.add_profile) {
                            ok = self->callbacks.add_profile(name, url, err);
                        }

                        if (ok) {
                            self->set_notification(std::string(T().sub_success));
                            self->refresh_profiles();
                        } else {
                            self->set_notification(std::string(T().sub_failed) + ": " + err);
                        }
                        if (self->callbacks.post_refresh) self->callbacks.post_refresh();
                    }).detach();
                } else {
                    self->show_add_dialog = false;
                    self->input_name.clear();
                    self->input_url.clear();
                }
                return true;
            }
            if (event == Event::Escape) {
                self->show_add_dialog = false;
                self->input_name.clear();
                self->input_url.clear();
                return true;
            }
            return false;
        }

        if (self->show_delete_confirm) {
            if (event == Event::Return) {
                std::vector<ProfileInfo> profiles;
                {
                    std::lock_guard<std::mutex> lock(self->profiles_mutex);
                    profiles = self->profiles;
                }
                if (self->selected >= 0 && self->selected < (int)profiles.size()) {
                    std::string name = profiles[self->selected].name;
                    std::string err;
                    if (self->callbacks.delete_profile) {
                        self->callbacks.delete_profile(name, err);
                    }
                    self->refresh_profiles();
                    {
                        std::lock_guard<std::mutex> lock(self->profiles_mutex);
                        if (self->selected >= (int)self->profiles.size() && self->selected > 0) {
                            self->selected--;
                        }
                    }
                }
                self->show_delete_confirm = false;
                if (self->callbacks.post_refresh) self->callbacks.post_refresh();
                return true;
            }
            if (event == Event::Escape) {
                self->show_delete_confirm = false;
                return true;
            }
            return true;
        }

        // Normal mode keybindings
        if (event == Event::ArrowUp || (event.is_character() && event.character() == "k")) {
            if (self->selected > 0) self->selected--;
            return true;
        }
        if (event == Event::ArrowDown || (event.is_character() && event.character() == "j")) {
            std::lock_guard<std::mutex> lock(self->profiles_mutex);
            int max = (int)self->profiles.size() - 1;
            if (self->selected < max) self->selected++;
            return true;
        }

        // Enter: switch active profile
        if (event == Event::Return) {
            std::vector<ProfileInfo> profiles;
            {
                std::lock_guard<std::mutex> lock(self->profiles_mutex);
                profiles = self->profiles;
            }
            if (self->selected >= 0 && self->selected < (int)profiles.size()) {
                std::string name = profiles[self->selected].name;
                std::thread([self, name]() {
                    std::string err;
                    bool ok = false;
                    if (self->callbacks.switch_profile) {
                        ok = self->callbacks.switch_profile(name, err);
                    }
                    if (ok) {
                        self->set_notification(std::string(T().profile_switch_success));
                        self->refresh_profiles();
                    } else {
                        self->set_notification(std::string(T().sub_failed) + ": " + err);
                    }
                    if (self->callbacks.post_refresh) self->callbacks.post_refresh();
                }).detach();
            }
            return true;
        }

        if (event.is_character()) {
            // A: add profile
            if (event.character() == "a" || event.character() == "A") {
                self->show_add_dialog = true;
                return true;
            }
            // D: delete profile
            if (event.character() == "d" || event.character() == "D") {
                std::lock_guard<std::mutex> lock(self->profiles_mutex);
                if (!self->profiles.empty()) {
                    self->show_delete_confirm = true;
                }
                return true;
            }
            // U: update selected profile
            if (event.character() == "u") {
                std::vector<ProfileInfo> profiles;
                {
                    std::lock_guard<std::mutex> lock(self->profiles_mutex);
                    profiles = self->profiles;
                }
                if (self->selected >= 0 && self->selected < (int)profiles.size()) {
                    std::string name = profiles[self->selected].name;
                    std::thread([self, name]() {
                        self->set_notification(std::string(T().profile_updating));
                        if (self->callbacks.post_refresh) self->callbacks.post_refresh();

                        std::string err;
                        bool ok = false;
                        if (self->callbacks.update_profile) {
                            ok = self->callbacks.update_profile(name, err);
                        }

                        if (ok) {
                            self->set_notification(std::string(T().sub_success));
                            self->refresh_profiles();
                        } else {
                            self->set_notification(std::string(T().sub_failed) + ": " + err);
                        }
                        if (self->callbacks.post_refresh) self->callbacks.post_refresh();
                    }).detach();
                }
                return true;
            }
            // Shift+U: update all profiles
            if (event.character() == "U") {
                std::thread([self]() {
                    self->set_notification(std::string(T().profile_updating_all));
                    if (self->callbacks.post_refresh) self->callbacks.post_refresh();

                    std::vector<ProfileInfo> profiles;
                    {
                        std::lock_guard<std::mutex> lock(self->profiles_mutex);
                        profiles = self->profiles;
                    }

                    bool all_ok = true;
                    for (const auto& p : profiles) {
                        std::string err;
                        if (self->callbacks.update_profile) {
                            if (!self->callbacks.update_profile(p.name, err)) {
                                all_ok = false;
                            }
                        }
                    }

                    if (all_ok) {
                        self->set_notification(std::string(T().sub_success));
                    } else {
                        self->set_notification(std::string(T().sub_failed));
                    }
                    self->refresh_profiles();
                    if (self->callbacks.post_refresh) self->callbacks.post_refresh();
                }).detach();
                return true;
            }
            // R: refresh profile list
            if (event.character() == "r" || event.character() == "R") {
                self->refresh_profiles();
                return true;
            }
        }

        return false;
    });
}
