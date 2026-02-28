#include "core/updater.hpp"

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sys/utsname.h>

#include <string>
#include <tuple>
#include <regex>

using json = nlohmann::json;

// ════════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════════

/// Parse a version string like "vX.Y.Z" or "X.Y.Z" into a (major, minor, patch) tuple.
/// Returns (0,0,0) if parsing fails.
static std::tuple<int, int, int> parse_version(const std::string& ver) {
    std::regex re(R"(v?(\d+)\.(\d+)\.(\d+))");
    std::smatch match;
    if (std::regex_search(ver, match, re) && match.size() >= 4) {
        try {
            return {std::stoi(match[1].str()),
                    std::stoi(match[2].str()),
                    std::stoi(match[3].str())};
        } catch (...) {}
    }
    return {0, 0, 0};
}

// ════════════════════════════════════════════════════════════════
// Updater implementation
// ════════════════════════════════════════════════════════════════

Updater::Updater(const std::string& repo) : repo_(repo) {}

std::string Updater::current_version() {
    return APP_VERSION;
}

std::string Updater::detect_arch_tag() {
    try {
        struct utsname uts;
        if (uname(&uts) == 0) {
            std::string machine(uts.machine);
            if (machine == "x86_64" || machine == "amd64") {
                return "x86_64";
            }
            if (machine == "aarch64" || machine == "arm64") {
                return "aarch64";
            }
            // Fallback: return raw machine string
            return machine;
        }
    } catch (...) {}
    return "x86_64"; // safe default
}

UpdateInfo Updater::check_for_update() const {
    UpdateInfo info;
    info.current_version = current_version();

    try {
        httplib::SSLClient cli("api.github.com", 443);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(10, 0);

        std::string ua = std::string("clashtui-cpp/") + current_version();
        httplib::Headers headers = {
            {"User-Agent", ua},
            {"Accept", "application/vnd.github.v3+json"},
        };

        std::string path = "/repos/" + repo_ + "/releases/latest";
        auto res = cli.Get(path.c_str(), headers);

        if (!res || res->status != 200) {
            return info;
        }

        auto j = json::parse(res->body);

        info.latest_version = j.value("tag_name", "");
        info.changelog = j.value("body", "");

        // Compare versions
        auto local = parse_version(info.current_version);
        auto remote = parse_version(info.latest_version);

        if (remote > local) {
            info.available = true;
        }

        // Find matching asset for current architecture
        std::string arch = detect_arch_tag();

        if (j.contains("assets") && j["assets"].is_array()) {
            for (const auto& asset : j["assets"]) {
                std::string name = asset.value("name", "");

                // Must contain the arch string
                if (name.find(arch) == std::string::npos) {
                    continue;
                }

                // Must contain ".tar.gz"
                if (name.find(".tar.gz") == std::string::npos) {
                    continue;
                }

                // Must NOT contain ".sha256"
                if (name.find(".sha256") != std::string::npos) {
                    continue;
                }

                info.download_url = asset.value("browser_download_url", "");
                break;
            }
        }
    } catch (...) {
        // Never throw — return default UpdateInfo on any failure
    }

    return info;
}
