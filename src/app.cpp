#include "app.hpp"
#include "core/config.hpp"
#include "core/profile_manager.hpp"
#include "daemon/ipc_client.hpp"
#include "api/mihomo_client.hpp"
#include "ui/main_screen.hpp"
#include "ui/proxy_panel.hpp"
#include "ui/subscription_panel.hpp"
#include "ui/log_panel.hpp"
#include "ui/install_wizard.hpp"
#include "ui/config_panel.hpp"
#include "ui/status_bar.hpp"
#include "core/installer.hpp"
#include "core/updater.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <thread>
#include <atomic>

using namespace ftxui;

struct App::Impl {
    Config config;
    std::unique_ptr<MihomoClient> client;
    ProfileManager profile_mgr{config};
    DaemonClient daemon_client;

    MainScreen main_screen;
    StatusBar status_bar;
    ProxyPanel proxy_panel;
    SubscriptionPanel subscription_panel;
    LogPanel log_panel;
    InstallWizard install_wizard;
    ConfigPanel config_panel;

    ScreenInteractive screen = ScreenInteractive::FullscreenAlternateScreen();

    // Panel management
    int current_panel = 0; // 0=proxy, 1=sub, 2=log, 3=install, 4=config
    Component panel_container;

    // Background threads
    std::atomic<bool> stop_flag{false};
    std::thread status_thread;

    // Cached daemon availability
    std::atomic<bool> daemon_available{false};
    std::atomic<bool> was_connected{false};  // track connection state transitions

    void init_client() {
        client = std::make_unique<MihomoClient>(
            config.data().api_host,
            config.data().api_port,
            config.data().api_secret
        );
    }

    void setup_callbacks() {
        MainScreen::Callbacks cb;

        cb.on_mode_change = [this](const std::string& mode) {
            if (client && client->set_mode(mode)) {
                main_screen.set_mode(mode);
                status_bar.set_mode(mode);
            }
        };

        cb.on_toggle_lang = [this]() {
            if (current_lang == Lang::ZH) {
                current_lang = Lang::EN;
                main_screen.set_language_label("EN");
                config.data().language = "en";
            } else {
                current_lang = Lang::ZH;
                main_screen.set_language_label("中");
                config.data().language = "zh";
            }
            config.save();
        };

        cb.on_quit = [this]() {
            screen.Exit();
        };

        cb.on_panel_switch = [this](int panel) {
            current_panel = panel;
            // Refresh profile list when switching to subscription panel
            if (panel == 1) {
                subscription_panel.refresh_profiles();
            }
        };

        main_screen.set_callbacks(std::move(cb));
    }

    void start_status_thread() {
        status_thread = std::thread([this]() {
            while (!stop_flag.load()) {
                if (client) {
                    bool ok = client->test_connection();
                    status_bar.set_connected(ok);
                    main_screen.set_connected(ok);

                    // Auto-refresh proxy data when connection is restored
                    if (ok && !was_connected.load()) {
                        proxy_panel.refresh_data();
                    }
                    was_connected.store(ok);

                    if (ok) {
                        auto stats = client->get_connections();
                        status_bar.set_connections(
                            stats.active_connections,
                            stats.upload_speed,
                            stats.download_speed
                        );

                        auto cfg = client->get_config();
                        status_bar.set_mode(cfg.mode);
                        main_screen.set_mode(cfg.mode);
                    }
                }

                // Check daemon availability periodically
                daemon_available.store(daemon_client.is_daemon_running());

                // Post a custom event to trigger UI refresh
                screen.Post(Event::Custom);

                // Sleep 2 seconds, checking stop_flag every 100ms
                for (int i = 0; i < 20 && !stop_flag.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }

    void stop_threads() {
        stop_flag.store(true);
        if (status_thread.joinable()) {
            status_thread.join();
        }
    }
};

App::App() : impl_(std::make_unique<Impl>()) {
    // Load config (use defaults if file doesn't exist)
    impl_->config.load();

    // Set language from config
    if (impl_->config.data().language == "en") {
        current_lang = Lang::EN;
        impl_->main_screen.set_language_label("EN");
    } else {
        current_lang = Lang::ZH;
        impl_->main_screen.set_language_label("中");
    }

    // Init API client
    impl_->init_client();

    // Setup UI
    impl_->setup_callbacks();

    // Setup ProxyPanel callbacks
    {
        ProxyPanel::Callbacks pcb;
        pcb.get_groups = [this]() { return impl_->client->get_proxy_groups(); };
        pcb.get_nodes = [this]() { return impl_->client->get_proxy_nodes(); };
        pcb.select_proxy = [this](const std::string& g, const std::string& p) {
            return impl_->client->select_proxy(g, p);
        };
        pcb.test_delay = [this](const std::string& name) {
            return impl_->client->test_delay(name);
        };
        impl_->proxy_panel.set_callbacks(std::move(pcb));
    }

    // Initial data load
    impl_->proxy_panel.refresh_data();

    // Setup LogPanel callbacks
    {
        LogPanel::Callbacks lcb;
        lcb.start_stream = [this](const std::string& level,
                                   std::function<void(LogEntry)> callback,
                                   std::atomic<bool>& stop_flag) {
            impl_->client->stream_logs(level, std::move(callback), stop_flag);
        };
        lcb.post_refresh = [this]() {
            impl_->screen.Post(Event::Custom);
        };
        impl_->log_panel.set_callbacks(std::move(lcb));
    }

    // Setup SubscriptionPanel callbacks (profile-based)
    {
        SubscriptionPanel::Callbacks scb;

        scb.is_daemon_available = [this]() -> bool {
            return impl_->daemon_available.load();
        };

        scb.list_profiles = [this]() -> std::vector<ProfileInfo> {
            if (impl_->daemon_available.load()) {
                return impl_->daemon_client.list_profiles();
            }
            return impl_->profile_mgr.list_profiles();
        };

        scb.add_profile = [this](const std::string& name, const std::string& url, std::string& err) -> bool {
            if (impl_->daemon_available.load()) {
                return impl_->daemon_client.add_profile(name, url, err);
            }
            auto result = impl_->profile_mgr.add_profile(name, url);
            err = result.error;
            return result.success;
        };

        scb.update_profile = [this](const std::string& name, std::string& err) -> bool {
            if (impl_->daemon_available.load()) {
                return impl_->daemon_client.update_profile(name, err);
            }
            auto result = impl_->profile_mgr.update_profile(name);
            err = result.error;
            if (result.success && result.was_active && impl_->client) {
                std::string deployed = impl_->profile_mgr.deploy_active_to_mihomo();
                if (deployed.empty()) {
                    err = "Failed to deploy profile to mihomo";
                    return false;
                }
                impl_->client->reload_config_and_wait(deployed);
                impl_->proxy_panel.refresh_data();
            }
            return result.success;
        };

        scb.delete_profile = [this](const std::string& name, std::string& err) -> bool {
            if (impl_->daemon_available.load()) {
                return impl_->daemon_client.delete_profile(name, err);
            }
            if (impl_->profile_mgr.delete_profile(name)) return true;
            err = "Failed to delete profile";
            return false;
        };

        scb.switch_profile = [this](const std::string& name, std::string& err) -> bool {
            bool ok = false;
            if (impl_->daemon_available.load()) {
                ok = impl_->daemon_client.switch_profile(name, err);
            } else {
                // Degraded mode: switch locally and reload mihomo
                if (impl_->profile_mgr.switch_active(name)) {
                    std::string deployed = impl_->profile_mgr.deploy_active_to_mihomo();
                    if (deployed.empty()) {
                        err = "Failed to deploy profile to mihomo";
                    } else {
                        if (impl_->client) {
                            impl_->client->reload_config_and_wait(deployed);
                        }
                        ok = true;
                    }
                } else {
                    err = "Failed to switch profile";
                }
            }
            // Refresh proxy data after profile switch
            if (ok) {
                impl_->proxy_panel.refresh_data();
            }
            return ok;
        };

        scb.get_active_profile = [this]() -> std::string {
            if (impl_->daemon_available.load()) {
                return impl_->daemon_client.get_active_profile();
            }
            return impl_->profile_mgr.active_profile_name();
        };

        scb.set_update_interval = [this](const std::string& name, int hours) -> bool {
            return impl_->profile_mgr.set_update_interval(name, hours);
        };

        scb.post_refresh = [this]() { impl_->screen.Post(Event::Custom); };

        impl_->subscription_panel.set_callbacks(std::move(scb));
    }

    // Initial profile data load
    impl_->subscription_panel.refresh_profiles();

    // Setup InstallWizard callbacks
    {
        InstallWizard::Callbacks icb;
        icb.is_installed = [this]() {
            return Installer::is_installed(
                Config::expand_home(impl_->config.data().mihomo_binary_path));
        };
        icb.get_version = [this]() {
            return Installer::get_running_version(
                Config::expand_home(impl_->config.data().mihomo_binary_path));
        };
        icb.get_binary_path = [this]() {
            return Config::expand_home(impl_->config.data().mihomo_binary_path);
        };
        icb.get_config_path = [this]() {
            return impl_->config.data().mihomo_config_path;
        };
        icb.get_service_name = [this]() {
            return impl_->config.data().mihomo_service_name;
        };
        icb.set_binary_path = [this](const std::string& path) {
            impl_->config.data().mihomo_binary_path = path;
        };
        icb.save_config = [this]() { impl_->config.save(); };
        icb.post_refresh = [this]() { impl_->screen.Post(Event::Custom); };
        impl_->install_wizard.set_callbacks(std::move(icb));
    }

    // Setup ConfigPanel callbacks
    {
        ConfigPanel::Callbacks ccb;
        ccb.get_config = [this]() -> AppConfig& { return impl_->config.data(); };
        ccb.save_config = [this]() { impl_->config.save(); };
        ccb.on_config_changed = [this]() { impl_->init_client(); };
        impl_->config_panel.set_callbacks(std::move(ccb));
    }

    // Panel container (Tab-based switching)
    impl_->panel_container = Container::Tab({
        impl_->proxy_panel.component(),
        impl_->subscription_panel.component(),
        impl_->log_panel.component(),
        impl_->install_wizard.component(),
        impl_->config_panel.component(),
    }, &impl_->current_panel);

    impl_->main_screen.set_content(impl_->panel_container);
    impl_->main_screen.set_status_bar(impl_->status_bar.component());
}

App::~App() {
    impl_->stop_threads();
}

void App::run() {
    // Start background threads
    impl_->start_status_thread();

    // Check for updates in background
    std::thread([this] {
        Updater updater;
        auto info = updater.check_for_update();
        if (info.available) {
            impl_->status_bar.set_update_available(info.latest_version);
            impl_->screen.Post(Event::Custom);
        }
    }).detach();

    // Run the TUI
    impl_->screen.Loop(impl_->main_screen.component());

    // Cleanup
    impl_->stop_threads();
}
