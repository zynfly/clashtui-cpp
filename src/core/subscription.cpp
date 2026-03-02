#include "core/subscription.hpp"
#include "core/utils.hpp"

#include <httplib.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

Subscription::DownloadResult Subscription::download(const std::string& url) {
    DownloadResult result;

    auto parts = parse_url(url);
    if (parts.scheme.empty()) {
        result.error = "Invalid URL";
        return result;
    }
    const auto& scheme = parts.scheme;
    const auto& host = parts.host;
    const auto& path = parts.path;
    int port = parts.port;

    try {
        httplib::Headers headers = {
            {"User-Agent", "clash.meta"}
        };

        if (scheme == "https") {
            httplib::SSLClient cli(host, port);
            cli.set_connection_timeout(10, 0);
            cli.set_read_timeout(30, 0);
            cli.set_follow_location(true);

            auto res = cli.Get(path, headers);
            if (res && res->status == 200) {
                result.success = true;
                result.content = res->body;
            } else {
                result.error = res ? "HTTP " + std::to_string(res->status) : "Connection failed";
            }
        } else {
            httplib::Client cli(host, port);
            cli.set_connection_timeout(10, 0);
            cli.set_read_timeout(30, 0);
            cli.set_follow_location(true);

            auto res = cli.Get(path, headers);
            if (res && res->status == 200) {
                result.success = true;
                result.content = res->body;
            } else {
                result.error = res ? "HTTP " + std::to_string(res->status) : "Connection failed";
            }
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    }

    return result;
}

bool Subscription::save_to_file(const std::string& content, const std::string& path) {
    try {
        auto parent = fs::path(path).parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent);
        }
        std::ofstream out(path);
        if (!out.is_open()) return false;
        out << content;
        return true;
    } catch (...) {
        return false;
    }
}
