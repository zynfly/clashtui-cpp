#pragma once

#include <ftxui/component/component.hpp>
#include <string>
#include <functional>
#include <memory>

class MainScreen {
public:
    struct Callbacks {
        std::function<void(const std::string&)> on_mode_change; // "global","rule","direct"
        std::function<void()> on_toggle_lang;
        std::function<void()> on_quit;
        std::function<void(int)> on_panel_switch; // 0=proxy, 1=sub, 2=log, 3=install, 4=config
    };

    MainScreen();
    ~MainScreen();

    void set_callbacks(Callbacks cb);
    void set_mode(const std::string& mode);
    void set_connected(bool connected);
    void set_language_label(const std::string& label);

    // Set the main content component (e.g., ProxyPanel, LogPanel, etc.)
    void set_content(ftxui::Component content);

    // Set the status bar component
    void set_status_bar(ftxui::Component status_bar);

    ftxui::Component component();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
