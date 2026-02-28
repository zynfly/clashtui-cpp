#include "core/subscription.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

Subscription::DownloadResult Subscription::download(const std::string& url) {
    DownloadResult result;

    // Parse URL to extract host and path
    std::string scheme, host, path;
    int port = 443;

    auto pos = url.find("://");
    if (pos != std::string::npos) {
        scheme = url.substr(0, pos);
        auto rest = url.substr(pos + 3);
        auto path_pos = rest.find('/');
        if (path_pos != std::string::npos) {
            host = rest.substr(0, path_pos);
            path = rest.substr(path_pos);
        } else {
            host = rest;
            path = "/";
        }
    } else {
        result.error = "Invalid URL";
        return result;
    }

    // Check for port in host
    auto colon = host.find(':');
    if (colon != std::string::npos) {
        try {
            port = std::stoi(host.substr(colon + 1));
        } catch (...) {}
        host = host.substr(0, colon);
    } else {
        port = (scheme == "https") ? 443 : 80;
    }

    try {
        httplib::Headers headers = {
            {"User-Agent", "clash"}
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
