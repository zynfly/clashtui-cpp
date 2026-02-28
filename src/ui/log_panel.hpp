#pragma once

#include "api/mihomo_client.hpp"

#include <ftxui/component/component.hpp>
#include <functional>
#include <memory>

class LogPanel {
public:
    struct Callbacks {
        // Start streaming logs. Called with level filter. Should run stream_logs in a thread.
        std::function<void(const std::string& level,
                           std::function<void(LogEntry)> callback,
                           std::atomic<bool>& stop_flag)> start_stream;
        // Post event to refresh UI
        std::function<void()> post_refresh;
    };

    LogPanel();
    ~LogPanel();

    void set_callbacks(Callbacks cb);

    // Called when panel becomes active/inactive
    void on_activate();
    void on_deactivate();

    // Push a log entry (thread-safe)
    void push_log(LogEntry entry);

    ftxui::Component component();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
