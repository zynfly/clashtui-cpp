#include "ui/log_panel.hpp"
#include "i18n/i18n.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <deque>
#include <mutex>
#include <thread>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace ftxui;

static const int MAX_LOG_LINES = 1000;

struct LogPanel::Impl {
    Callbacks callbacks;

    std::deque<LogEntry> logs;
    std::mutex log_mutex;

    int filter_level = 0; // 0=all, 1=info, 2=warning, 3=error
    bool frozen = false;
    int scroll_offset = 0;

    std::atomic<bool> stream_stop{true};
    std::thread stream_thread;

    void start_streaming() {
        if (!callbacks.start_stream) return;
        stream_stop.store(false);
        stream_thread = std::thread([this]() {
            callbacks.start_stream("debug",
                [this](LogEntry entry) {
                    push(std::move(entry));
                    if (callbacks.post_refresh) {
                        callbacks.post_refresh();
                    }
                },
                stream_stop);
        });
    }

    void stop_streaming() {
        stream_stop.store(true);
        if (stream_thread.joinable()) {
            stream_thread.join();
        }
    }

    void push(LogEntry entry) {
        std::lock_guard<std::mutex> lock(log_mutex);
        logs.push_back(std::move(entry));
        while ((int)logs.size() > MAX_LOG_LINES) {
            logs.pop_front();
        }
    }

    bool matches_filter(const LogEntry& entry) const {
        if (filter_level == 0) return true;
        if (filter_level == 1 && entry.type == "info") return true;
        if (filter_level == 2 && entry.type == "warning") return true;
        if (filter_level == 3 && entry.type == "error") return true;
        return false;
    }

    Color log_color(const std::string& type) const {
        if (type == "info") return Color::White;
        if (type == "warning") return Color::Yellow;
        if (type == "error") return Color::Red;
        if (type == "debug") return Color::GrayDark;
        return Color::White;
    }

    void export_logs() {
        std::lock_guard<std::mutex> lock(log_mutex);
        auto t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        std::ostringstream oss;
        oss << "clashtui-logs-" << std::put_time(&tm, "%Y%m%d-%H%M%S") << ".log";

        std::ofstream out(oss.str());
        if (!out.is_open()) return;
        for (const auto& entry : logs) {
            if (matches_filter(entry)) {
                out << "[" << entry.type << "] " << entry.payload << "\n";
            }
        }
    }
};

LogPanel::LogPanel() : impl_(std::make_unique<Impl>()) {}
LogPanel::~LogPanel() {
    impl_->stop_streaming();
}

void LogPanel::set_callbacks(Callbacks cb) { impl_->callbacks = std::move(cb); }

void LogPanel::on_activate() { impl_->start_streaming(); }
void LogPanel::on_deactivate() { impl_->stop_streaming(); }

void LogPanel::push_log(LogEntry entry) { impl_->push(std::move(entry)); }

Component LogPanel::component() {
    auto self = impl_.get();

    return Renderer([self](bool /*focused*/) -> Element {
        std::lock_guard<std::mutex> lock(self->log_mutex);

        // Header with filter info
        std::string filter_labels[] = {"ALL", "INFO", "WARNING", "ERROR"};
        Elements header_items;
        for (int i = 0; i < 4; i++) {
            auto el = text(" " + std::to_string(i + 1) + ":" + filter_labels[i] + " ");
            if (i == self->filter_level) {
                el = el | bold | inverted;
            } else {
                el = el | dim;
            }
            header_items.push_back(el);
        }
        header_items.push_back(filler());
        header_items.push_back(
            self->frozen
                ? text(" [F] " + std::string(T().log_freeze) + " ") | color(Color::Yellow)
                : text(" [F] " + std::string(T().log_unfreeze) + " ") | dim
        );
        header_items.push_back(text(" [X] " + std::string(T().log_export) + " ") | dim);

        auto header = hbox(std::move(header_items));

        // Log entries
        Elements lines;
        for (const auto& entry : self->logs) {
            if (!self->matches_filter(entry)) continue;
            std::string prefix = "[" + entry.type + "] ";
            lines.push_back(
                hbox({
                    text(prefix) | bold | color(self->log_color(entry.type)),
                    text(entry.payload),
                })
            );
        }

        if (lines.empty()) {
            lines.push_back(text("  (no logs)") | dim);
        }

        auto log_view = vbox(std::move(lines));
        if (!self->frozen) {
            log_view = log_view | focusPositionRelative(0, 1); // auto-scroll to bottom
        }

        return vbox({
            header,
            separator(),
            log_view | vscroll_indicator | frame | flex,
        }) | border;
    }) | CatchEvent([self](Event event) -> bool {
        // 1-4: filter level
        if (event.is_character()) {
            if (event.character() == "1") { self->filter_level = 0; return true; }
            if (event.character() == "2") { self->filter_level = 1; return true; }
            if (event.character() == "3") { self->filter_level = 2; return true; }
            if (event.character() == "4") { self->filter_level = 3; return true; }

            // F: toggle freeze
            if (event.character() == "f" || event.character() == "F") {
                self->frozen = !self->frozen;
                return true;
            }

            // X: export
            if (event.character() == "x" || event.character() == "X") {
                self->export_logs();
                return true;
            }
        }

        return false;
    });
}
