#pragma once

#include <ftxui/component/component.hpp>
#include <string>
#include <mutex>
#include <atomic>

class StatusBar {
public:
    StatusBar();
    ~StatusBar();

    ftxui::Component component();

    // Thread-safe setters for background updates
    void set_mode(const std::string& mode);
    void set_connections(int count, int64_t upload_speed, int64_t download_speed);
    void set_connected(bool connected);
    void set_update_available(const std::string& version);

private:
    std::mutex mutex_;
    std::string mode_ = "rule";
    int connection_count_ = 0;
    int64_t upload_speed_ = 0;
    int64_t download_speed_ = 0;
    std::atomic<bool> connected_{false};
    std::string update_version_;

    static std::string format_speed(int64_t bytes_per_sec);
};
