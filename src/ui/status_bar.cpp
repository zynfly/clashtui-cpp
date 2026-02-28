#include "ui/status_bar.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <iomanip>

using namespace ftxui;

StatusBar::StatusBar() = default;
StatusBar::~StatusBar() = default;

void StatusBar::set_mode(const std::string& mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = mode;
}

void StatusBar::set_connections(int count, int64_t upload_speed, int64_t download_speed) {
    std::lock_guard<std::mutex> lock(mutex_);
    connection_count_ = count;
    upload_speed_ = upload_speed;
    download_speed_ = download_speed;
}

void StatusBar::set_connected(bool connected) {
    connected_.store(connected);
}

void StatusBar::set_update_available(const std::string& version) {
    std::lock_guard<std::mutex> lock(mutex_);
    update_version_ = version;
}

std::string StatusBar::format_speed(int64_t bytes_per_sec) {
    std::ostringstream oss;
    if (bytes_per_sec < 1024) {
        oss << bytes_per_sec << " B/s";
    } else if (bytes_per_sec < 1024 * 1024) {
        oss << std::fixed << std::setprecision(1)
            << (double)bytes_per_sec / 1024.0 << " KB/s";
    } else {
        oss << std::fixed << std::setprecision(1)
            << (double)bytes_per_sec / (1024.0 * 1024.0) << " MB/s";
    }
    return oss.str();
}

Component StatusBar::component() {
    return Renderer([this] {
        std::string mode;
        int conn_count;
        int64_t up_speed, down_speed;
        std::string update_ver;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            mode = mode_;
            conn_count = connection_count_;
            up_speed = upload_speed_;
            down_speed = download_speed_;
            update_ver = update_version_;
        }

        bool is_connected = connected_.load();

        // Left: mode
        auto mode_text = text(" " + mode + " ") | bold;

        // Center: connections + speed
        std::string stats = std::to_string(conn_count) + " conn  "
                          + "↑ " + format_speed(up_speed) + "  "
                          + "↓ " + format_speed(down_speed);
        auto center_text = text(stats);

        // Right: update indicator + connection status
        Elements right_elements;
        if (!update_ver.empty()) {
            right_elements.push_back(
                text(" ↑ " + update_ver + " ") | color(Color::Yellow)
            );
        }
        auto status_text = is_connected
            ? text(" ● " + std::string(T().connected) + " ") | color(Color::Green)
            : text(" ○ " + std::string(T().disconnected) + " ") | color(Color::Red);
        right_elements.push_back(status_text);

        return hbox({
            mode_text,
            filler(),
            center_text,
            filler(),
            hbox(right_elements),
        }) | inverted;
    });
}
