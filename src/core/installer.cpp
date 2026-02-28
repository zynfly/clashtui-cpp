#include "core/installer.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <sys/utsname.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <iomanip>
#include <regex>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ════════════════════════════════════════════════════════════════
// Private helpers
// ════════════════════════════════════════════════════════════════

/// Shell-escape a string by wrapping in single quotes and escaping embedded quotes
static std::string shell_quote(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

/// Validate a service name: only allow alphanumeric, dash, underscore, dot
static bool is_valid_service_name(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

Installer::UrlParts Installer::parse_url(const std::string& url) {
    UrlParts parts;
    auto pos = url.find("://");
    if (pos != std::string::npos) {
        parts.scheme = url.substr(0, pos);
        auto rest = url.substr(pos + 3);
        auto path_pos = rest.find('/');
        if (path_pos != std::string::npos) {
            parts.host = rest.substr(0, path_pos);
            parts.path = rest.substr(path_pos);
        } else {
            parts.host = rest;
            parts.path = "/";
        }
    }
    auto colon = parts.host.find(':');
    if (colon != std::string::npos) {
        try {
            parts.port = std::stoi(parts.host.substr(colon + 1));
        } catch (...) {}
        parts.host = parts.host.substr(0, colon);
    } else {
        parts.port = (parts.scheme == "https") ? 443 : 80;
    }
    return parts;
}

int Installer::run_command(const std::string& cmd) {
    return system(cmd.c_str());
}

std::string Installer::run_command_output(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

std::string Installer::systemctl_cmd(ServiceScope scope) {
    if (scope == ServiceScope::System) {
        return "sudo systemctl";
    }
    return "systemctl --user";
}

std::string Installer::get_service_file_path(const std::string& service_name, ServiceScope scope) {
    if (scope == ServiceScope::System) {
        return "/etc/systemd/system/" + service_name + ".service";
    }
    // User scope
    const char* home = getenv("HOME");
    std::string home_dir = home ? home : "/tmp";
    return home_dir + "/.config/systemd/user/" + service_name + ".service";
}

// ════════════════════════════════════════════════════════════════
// Phase 1: Detection & version comparison
// ════════════════════════════════════════════════════════════════

bool Installer::is_installed(const std::string& binary_path) {
    return fs::exists(binary_path);
}

std::string Installer::get_running_version(const std::string& binary_path) {
    if (!fs::exists(binary_path)) return "";

    std::string cmd = shell_quote(binary_path) + " -v 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);

    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

PlatformInfo Installer::detect_platform() {
    PlatformInfo info;

    // Detect OS
#if defined(__linux__)
    info.os = "linux";
#elif defined(__APPLE__)
    info.os = "darwin";
#elif defined(_WIN32)
    info.os = "windows";
#else
    info.os = "unknown";
#endif

    // Detect architecture using uname
    try {
        struct utsname uts;
        if (uname(&uts) == 0) {
            std::string machine(uts.machine);
            if (machine == "x86_64" || machine == "amd64") {
                info.arch = "amd64";
            } else if (machine == "aarch64" || machine == "arm64") {
                info.arch = "arm64";
            } else if (machine == "armv7l" || machine == "armv7") {
                info.arch = "armv7";
            } else if (machine == "i686" || machine == "i386") {
                info.arch = "386";
            } else if (machine == "s390x") {
                info.arch = "s390x";
            } else if (machine == "riscv64") {
                info.arch = "riscv64";
            } else if (machine == "mips64") {
                info.arch = "mips64";
            } else {
                info.arch = machine;
            }
        }
    } catch (...) {
        info.arch = "amd64"; // fallback
    }

    return info;
}

AssetInfo Installer::select_asset(const ReleaseInfo& release, const PlatformInfo& platform) {
    // Build the pattern we're looking for: mihomo-{os}-{arch}
    std::string target = platform.os + "-" + platform.arch;

    // Candidates that match the target pattern and end with .gz
    std::vector<const AssetInfo*> candidates;

    for (const auto& asset : release.assets) {
        // Must end with .gz
        if (asset.name.size() < 3 || asset.name.substr(asset.name.size() - 3) != ".gz") {
            continue;
        }

        // Must contain the os-arch pattern
        if (asset.name.find(target) == std::string::npos) {
            continue;
        }

        candidates.push_back(&asset);
    }

    if (candidates.empty()) {
        return AssetInfo{};
    }

    // Prefer non-alpha, non-compatible versions
    // Sort: prefer names without "alpha", "beta", "compatible", "cgo" substrings
    auto score = [](const std::string& name) -> int {
        int s = 0;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("alpha") != std::string::npos) s += 10;
        if (lower.find("beta") != std::string::npos) s += 10;
        if (lower.find("compatible") != std::string::npos) s += 5;
        // Shorter names are generally the standard release
        s += static_cast<int>(name.size());
        return s;
    };

    const AssetInfo* best = candidates[0];
    int best_score = score(best->name);

    for (size_t i = 1; i < candidates.size(); ++i) {
        int s = score(candidates[i]->name);
        if (s < best_score) {
            best_score = s;
            best = candidates[i];
        }
    }

    return *best;
}

bool Installer::is_newer_version(const std::string& local_version, const std::string& remote_version) {
    // Parse version strings like "v1.18.0" or "Mihomo Meta v1.18.0" into (major, minor, patch)
    auto parse = [](const std::string& ver) -> std::vector<int> {
        std::vector<int> parts;
        // Find the version pattern: vX.Y.Z or X.Y.Z
        std::regex re(R"(v?(\d+)\.(\d+)\.(\d+))");
        std::smatch match;
        if (std::regex_search(ver, match, re)) {
            for (size_t i = 1; i < match.size(); ++i) {
                try {
                    parts.push_back(std::stoi(match[i].str()));
                } catch (...) {
                    parts.push_back(0);
                }
            }
        }
        return parts;
    };

    auto local_parts = parse(local_version);
    auto remote_parts = parse(remote_version);

    // If we can't parse either, assume not newer
    if (local_parts.empty() || remote_parts.empty()) {
        return false;
    }

    // Pad to same length
    while (local_parts.size() < 3) local_parts.push_back(0);
    while (remote_parts.size() < 3) remote_parts.push_back(0);

    // Compare numerically: major, minor, patch
    for (size_t i = 0; i < 3; ++i) {
        if (remote_parts[i] > local_parts[i]) return true;
        if (remote_parts[i] < local_parts[i]) return false;
    }

    return false; // equal
}

ReleaseInfo Installer::fetch_latest_release() {
    ReleaseInfo info;
    try {
        httplib::SSLClient cli("api.github.com", 443);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(15, 0);

        httplib::Headers headers = {
            {"User-Agent", "clashtui-cpp"},
            {"Accept", "application/vnd.github.v3+json"},
        };

        auto res = cli.Get("/repos/MetaCubeX/mihomo/releases/latest", headers);
        if (res && res->status == 200) {
            auto j = json::parse(res->body);
            info.version = j.value("tag_name", "");
            info.changelog = j.value("body", "");
            if (j.contains("assets") && j["assets"].is_array()) {
                for (auto& asset : j["assets"]) {
                    AssetInfo ai;
                    ai.name = asset.value("name", "");
                    ai.download_url = asset.value("browser_download_url", "");
                    ai.size = asset.value("size", (int64_t)0);

                    // Detect checksums file before moving ai
                    // Typically named something like "checksums.txt" or containing "sha256"
                    std::string lower_name = ai.name;
                    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                    if (lower_name.find("checksum") != std::string::npos ||
                        lower_name.find("sha256") != std::string::npos) {
                        info.checksums_url = ai.download_url;
                    }

                    info.assets.push_back(std::move(ai));
                }
            }
        }
    } catch (...) {}
    return info;
}

// ════════════════════════════════════════════════════════════════
// Phase 2: Download pipeline + SHA256
// ════════════════════════════════════════════════════════════════

bool Installer::verify_sha256(const std::string& file_path, const std::string& expected_hash) {
    try {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) return false;

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) return false;

        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            return false;
        }

        char buffer[8192];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            if (EVP_DigestUpdate(ctx, buffer, static_cast<size_t>(file.gcount())) != 1) {
                EVP_MD_CTX_free(ctx);
                return false;
            }
            if (file.gcount() < static_cast<std::streamsize>(sizeof(buffer))) break;
        }

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;
        if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
            EVP_MD_CTX_free(ctx);
            return false;
        }
        EVP_MD_CTX_free(ctx);

        // Convert to hex string
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (unsigned int i = 0; i < hash_len; ++i) {
            oss << std::setw(2) << static_cast<int>(hash[i]);
        }
        std::string computed = oss.str();

        // Case-insensitive comparison
        std::string expected_lower = expected_hash;
        std::transform(expected_lower.begin(), expected_lower.end(), expected_lower.begin(), ::tolower);
        std::transform(computed.begin(), computed.end(), computed.begin(), ::tolower);

        return computed == expected_lower;
    } catch (...) {
        return false;
    }
}

bool Installer::download_single(const std::string& url,
                                const std::string& dest_path,
                                std::function<void(int64_t, int64_t)> on_progress,
                                std::atomic<bool>* cancel_flag) {
    try {
        auto parts = parse_url(url);
        if (parts.host.empty()) return false;

        // Ensure parent directory exists
        auto parent = fs::path(dest_path).parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent);
        }

        std::ofstream out(dest_path, std::ios::binary);
        if (!out.is_open()) return false;

        int64_t total_bytes = 0;
        int64_t received_bytes = 0;
        bool success = false;

        httplib::Headers headers = {
            {"User-Agent", "clashtui-cpp"},
        };

        auto response_handler = [&](const httplib::Response& response) -> bool {
            if (cancel_flag && cancel_flag->load()) return false;

            // Extract content-length from response
            if (response.has_header("Content-Length")) {
                try {
                    total_bytes = std::stoll(response.get_header_value("Content-Length"));
                } catch (...) {}
            }
            // Check for successful status
            return response.status == 200;
        };

        auto content_receiver = [&](const char* data, size_t data_length) -> bool {
            if (cancel_flag && cancel_flag->load()) return false;

            out.write(data, static_cast<std::streamsize>(data_length));
            if (!out.good()) return false;  // Stop on write error
            received_bytes += static_cast<int64_t>(data_length);

            if (on_progress) {
                on_progress(received_bytes, total_bytes);
            }
            return true;
        };

        if (parts.scheme == "https") {
            httplib::SSLClient cli(parts.host, parts.port);
            cli.set_connection_timeout(15, 0);
            cli.set_read_timeout(120, 0);
            cli.set_follow_location(true);

            auto res = cli.Get(parts.path, headers, response_handler, content_receiver);
            success = (res && res->status == 200);
        } else {
            httplib::Client cli(parts.host, parts.port);
            cli.set_connection_timeout(15, 0);
            cli.set_read_timeout(120, 0);
            cli.set_follow_location(true);

            auto res = cli.Get(parts.path, headers, response_handler, content_receiver);
            success = (res && res->status == 200);
        }

        out.close();

        if (!success) {
            // Clean up partial download
            try { fs::remove(dest_path); } catch (...) {}
        }

        return success;
    } catch (...) {
        try { fs::remove(dest_path); } catch (...) {}
        return false;
    }
}

std::vector<std::string> Installer::get_proxy_mirrors() {
    return {
        "",                          // direct (no mirror)
        "https://ghfast.top/",
        "https://gh-proxy.com/",
        "https://ghproxy.cc/"
    };
}

bool Installer::download_with_fallback(const std::string& url,
                                       const std::string& dest_path,
                                       std::function<void(int64_t, int64_t)> on_progress,
                                       std::atomic<bool>* cancel_flag) {
    auto mirrors = get_proxy_mirrors();

    for (const auto& mirror : mirrors) {
        if (cancel_flag && cancel_flag->load()) return false;

        std::string full_url;
        if (mirror.empty()) {
            full_url = url;
        } else {
            // Mirror prefix: mirror + original URL
            full_url = mirror + url;
        }

        if (download_single(full_url, dest_path, on_progress, cancel_flag)) {
            return true;
        }
    }

    return false;
}

std::string Installer::fetch_checksum_for_file(const std::string& checksums_url,
                                               const std::string& filename) {
    try {
        auto parts = parse_url(checksums_url);
        if (parts.host.empty()) return "";

        httplib::Headers headers = {
            {"User-Agent", "clashtui-cpp"},
        };

        std::string body;

        if (parts.scheme == "https") {
            httplib::SSLClient cli(parts.host, parts.port);
            cli.set_connection_timeout(10, 0);
            cli.set_read_timeout(30, 0);
            cli.set_follow_location(true);

            auto res = cli.Get(parts.path, headers);
            if (res && res->status == 200) {
                body = res->body;
            }
        } else {
            httplib::Client cli(parts.host, parts.port);
            cli.set_connection_timeout(10, 0);
            cli.set_read_timeout(30, 0);
            cli.set_follow_location(true);

            auto res = cli.Get(parts.path, headers);
            if (res && res->status == 200) {
                body = res->body;
            }
        }

        if (body.empty()) return "";

        // Parse checksums.txt format: "<hash>  <filename>" or "<hash> <filename>"
        std::istringstream stream(body);
        std::string line;
        while (std::getline(stream, line)) {
            // Trim trailing \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Find the filename in this line (exact match after whitespace)
            auto space_pos = line.find_first_of(" \t");
            if (space_pos != std::string::npos) {
                // Skip whitespace (checksums.txt can use "  " or " *")
                auto name_start = line.find_first_not_of(" \t*", space_pos);
                if (name_start != std::string::npos) {
                    std::string file_in_line = line.substr(name_start);
                    if (file_in_line == filename) {
                        return line.substr(0, space_pos);
                    }
                }
            }
        }
    } catch (...) {}

    return "";
}

bool Installer::extract_gz(const std::string& gz_path, const std::string& dest_path) {
    try {
        // Ensure parent directory exists
        auto parent = fs::path(dest_path).parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent);
        }

        std::string cmd = "gunzip -c " + shell_quote(gz_path) + " > " + shell_quote(dest_path) + " && chmod +x " + shell_quote(dest_path);
        int ret = run_command(cmd);
        return ret == 0;
    } catch (...) {
        return false;
    }
}

bool Installer::install_binary(const std::string& gz_path,
                               const std::string& install_path,
                               bool needs_sudo) {
    try {
        // Create a temporary extraction target
        std::string temp_path = gz_path + ".extracted";

        // Extract the .gz file
        if (!extract_gz(gz_path, temp_path)) {
            return false;
        }

        // Move to final location
        std::string cmd;
        if (needs_sudo) {
            cmd = "sudo cp " + shell_quote(temp_path) + " " + shell_quote(install_path) + " && "
                  "sudo chmod +x " + shell_quote(install_path);
        } else {
            // Ensure parent directory exists
            auto parent = fs::path(install_path).parent_path();
            if (!parent.empty()) {
                fs::create_directories(parent);
            }
            cmd = "cp " + shell_quote(temp_path) + " " + shell_quote(install_path) + " && "
                  "chmod +x " + shell_quote(install_path);
        }

        int ret = run_command(cmd);

        // Clean up temp file
        try { fs::remove(temp_path); } catch (...) {}

        return ret == 0;
    } catch (...) {
        return false;
    }
}

bool Installer::generate_default_config(const std::string& config_path) {
    try {
        auto parent = fs::path(config_path).parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent);
        }

        std::ofstream out(config_path);
        if (!out.is_open()) return false;

        out << "# Minimal Mihomo configuration\n"
            << "mixed-port: 7890\n"
            << "allow-lan: false\n"
            << "mode: rule\n"
            << "log-level: info\n"
            << "external-controller: 127.0.0.1:9090\n"
            << "\n"
            << "dns:\n"
            << "  enable: true\n"
            << "  nameserver:\n"
            << "    - 8.8.8.8\n"
            << "    - 1.1.1.1\n"
            << "\n"
            << "proxies: []\n"
            << "\n"
            << "rules:\n"
            << "  - MATCH,DIRECT\n";

        return true;
    } catch (...) {
        return false;
    }
}

bool Installer::download_binary(const std::string& url,
                                const std::string& dest_path,
                                std::function<void(float)> on_progress) {
    // Legacy wrapper around download_single
    if (on_progress) {
        return download_single(url, dest_path,
            [&on_progress](int64_t received, int64_t total) {
                if (total > 0) {
                    on_progress(static_cast<float>(received) / static_cast<float>(total));
                }
            });
    }
    return download_single(url, dest_path);
}

// ════════════════════════════════════════════════════════════════
// Phase 3: Systemd service management
// ════════════════════════════════════════════════════════════════

bool Installer::has_systemd() {
    try {
        std::string result = run_command_output("which systemctl 2>/dev/null");
        return !result.empty();
    } catch (...) {
        return false;
    }
}

std::string Installer::generate_service_content(const std::string& binary_path,
                                                const std::string& config_dir,
                                                ServiceScope scope) {
    std::ostringstream ss;
    ss << "[Unit]\n"
       << "Description=Mihomo Proxy Service\n"
       << "After=network-online.target\n"
       << "Wants=network-online.target\n"
       << "\n"
       << "[Service]\n"
       << "Type=simple\n"
       << "ExecStart=\"" << binary_path << "\" -d \"" << config_dir << "\"\n"
       << "Restart=on-failure\n"
       << "RestartSec=5\n"
       << "LimitNOFILE=65536\n"
       << "\n"
       << "[Install]\n";

    if (scope == ServiceScope::System) {
        ss << "WantedBy=multi-user.target\n";
    } else {
        ss << "WantedBy=default.target\n";
    }

    return ss.str();
}

// ── Daemon service ──────────────────────────────────────────

std::string Installer::generate_daemon_service_content(
    const std::string& clashtui_binary_path, ServiceScope scope) {
    std::ostringstream ss;
    ss << "[Unit]\n"
       << "Description=clashtui-cpp Daemon (Mihomo Manager)\n"
       << "After=network-online.target\n"
       << "Wants=network-online.target\n"
       << "\n"
       << "[Service]\n"
       << "Type=simple\n"
       << "ExecStart=\"" << clashtui_binary_path << "\" --daemon\n"
       << "Restart=on-failure\n"
       << "RestartSec=5\n"
       << "\n"
       << "[Install]\n";

    if (scope == ServiceScope::System) {
        ss << "WantedBy=multi-user.target\n";
    } else {
        ss << "WantedBy=default.target\n";
    }

    return ss.str();
}

bool Installer::install_daemon_service(
    const std::string& clashtui_binary_path,
    const std::string& service_name,
    ServiceScope scope) {
    try {
        if (!is_valid_service_name(service_name)) return false;

        std::string content = generate_daemon_service_content(clashtui_binary_path, scope);
        std::string path = get_service_file_path(service_name, scope);

        if (scope == ServiceScope::System) {
            std::string tmp = "/tmp/clashtui-daemon-service-" + service_name + ".tmp";
            {
                std::ofstream out(tmp);
                if (!out.is_open()) return false;
                out << content;
            }
            std::string cmd = "sudo cp " + shell_quote(tmp) + " " + shell_quote(path);
            int ret = run_command(cmd);
            try { fs::remove(tmp); } catch (...) {}
            if (ret != 0) return false;
        } else {
            auto parent = fs::path(path).parent_path();
            if (!parent.empty()) {
                fs::create_directories(parent);
            }
            std::ofstream out(path);
            if (!out.is_open()) return false;
            out << content;
        }

        // daemon-reload + enable + start
        std::string ctl = systemctl_cmd(scope);
        run_command(ctl + " daemon-reload");
        run_command(ctl + " enable " + shell_quote(service_name));
        run_command(ctl + " start " + shell_quote(service_name));
        return true;
    } catch (...) {
        return false;
    }
}

bool Installer::install_service(const std::string& binary_path,
                                const std::string& config_dir,
                                const std::string& service_name,
                                ServiceScope scope) {
    try {
        if (!is_valid_service_name(service_name)) return false;

        std::string content = generate_service_content(binary_path, config_dir, scope);
        std::string path = get_service_file_path(service_name, scope);

        if (scope == ServiceScope::System) {
            // Write via temp file + sudo cp (avoids shell interpretation of content)
            std::string tmp = "/tmp/clashtui-service-" + service_name + ".tmp";
            {
                std::ofstream out(tmp);
                if (!out.is_open()) return false;
                out << content;
            }
            std::string cmd = "sudo cp " + shell_quote(tmp) + " " + shell_quote(path);
            int ret = run_command(cmd);
            try { fs::remove(tmp); } catch (...) {}
            if (ret != 0) return false;
        } else {
            // User scope: ensure directory exists and write directly
            auto parent = fs::path(path).parent_path();
            if (!parent.empty()) {
                fs::create_directories(parent);
            }
            std::ofstream out(path);
            if (!out.is_open()) return false;
            out << content;
            out.close();
        }

        // Reload daemon
        std::string ctl = systemctl_cmd(scope);
        if (run_command(ctl + " daemon-reload") != 0) return false;

        // Enable
        if (run_command(ctl + " enable " + service_name + ".service") != 0) return false;

        // Start
        if (run_command(ctl + " start " + service_name + ".service") != 0) return false;

        return true;
    } catch (...) {
        return false;
    }
}

bool Installer::start_service(const std::string& service_name, ServiceScope scope) {
    try {
        if (!is_valid_service_name(service_name)) return false;
        std::string cmd = systemctl_cmd(scope) + " start " + service_name + ".service";
        return run_command(cmd) == 0;
    } catch (...) {
        return false;
    }
}

bool Installer::stop_service(const std::string& service_name, ServiceScope scope) {
    try {
        if (!is_valid_service_name(service_name)) return false;
        std::string cmd = systemctl_cmd(scope) + " stop " + service_name + ".service";
        return run_command(cmd) == 0;
    } catch (...) {
        return false;
    }
}

bool Installer::enable_service(const std::string& service_name, ServiceScope scope) {
    try {
        if (!is_valid_service_name(service_name)) return false;
        std::string cmd = systemctl_cmd(scope) + " enable " + service_name + ".service";
        return run_command(cmd) == 0;
    } catch (...) {
        return false;
    }
}

bool Installer::disable_service(const std::string& service_name, ServiceScope scope) {
    try {
        if (!is_valid_service_name(service_name)) return false;
        std::string cmd = systemctl_cmd(scope) + " disable " + service_name + ".service";
        return run_command(cmd) == 0;
    } catch (...) {
        return false;
    }
}

bool Installer::is_service_active(const std::string& service_name, ServiceScope scope) {
    try {
        if (!is_valid_service_name(service_name)) return false;
        std::string cmd = systemctl_cmd(scope) + " is-active " + service_name + ".service 2>/dev/null";
        std::string output = run_command_output(cmd);
        return output == "active";
    } catch (...) {
        return false;
    }
}

bool Installer::remove_service(const std::string& service_name, ServiceScope scope) {
    try {
        if (!is_valid_service_name(service_name)) return false;

        std::string ctl = systemctl_cmd(scope);

        // Stop (ignore failure if not running)
        run_command(ctl + " stop " + service_name + ".service 2>/dev/null");

        // Disable (ignore failure if not enabled)
        run_command(ctl + " disable " + service_name + ".service 2>/dev/null");

        // Remove the service file
        std::string path = get_service_file_path(service_name, scope);
        if (scope == ServiceScope::System) {
            run_command("sudo rm -f " + shell_quote(path));
        } else {
            try { fs::remove(path); } catch (...) {}
        }

        // Reload daemon
        run_command(ctl + " daemon-reload");

        return true;
    } catch (...) {
        return false;
    }
}

// ════════════════════════════════════════════════════════════════
// Phase 4: Uninstall
// ════════════════════════════════════════════════════════════════

bool Installer::uninstall(const std::string& binary_path,
                          const std::string& service_name,
                          ServiceScope scope,
                          bool remove_config,
                          const std::string& config_dir,
                          std::function<void(UninstallProgress)> on_progress) {
    auto report = [&on_progress](UninstallProgress::Phase phase, const std::string& msg) {
        if (on_progress) {
            UninstallProgress p;
            p.phase = phase;
            p.message = msg;
            on_progress(p);
        }
    };

    try {
        // Step 1: Stop service if applicable
        if (scope != ServiceScope::None) {
            report(UninstallProgress::Phase::StoppingService, "Stopping service...");
            stop_service(service_name, scope);

            // Step 2: Disable service
            report(UninstallProgress::Phase::DisablingService, "Disabling service...");
            disable_service(service_name, scope);

            // Step 3: Remove service file
            report(UninstallProgress::Phase::RemovingService, "Removing service files...");
            std::string svc_path = get_service_file_path(service_name, scope);
            if (scope == ServiceScope::System) {
                run_command("sudo rm -f " + shell_quote(svc_path));
            } else {
                try { fs::remove(svc_path); } catch (...) {}
            }
            // Daemon reload
            run_command(systemctl_cmd(scope) + " daemon-reload");
        }

        // Step 4: Remove binary
        report(UninstallProgress::Phase::RemovingBinary, "Removing binary...");
        if (fs::exists(binary_path)) {
            // Check if binary is in a system path (needs sudo)
            bool needs_sudo = (binary_path.find("/usr/") == 0 ||
                               binary_path.find("/opt/") == 0);
            if (needs_sudo) {
                if (run_command("sudo rm -f " + shell_quote(binary_path)) != 0) {
                    report(UninstallProgress::Phase::Failed, "Failed to remove binary");
                    return false;
                }
            } else {
                try {
                    fs::remove(binary_path);
                } catch (const std::exception& e) {
                    report(UninstallProgress::Phase::Failed,
                           std::string("Failed to remove binary: ") + e.what());
                    return false;
                }
            }
        }

        // Step 5: Optionally remove config directory
        if (remove_config && !config_dir.empty()) {
            report(UninstallProgress::Phase::RemovingConfig, "Removing configuration...");
            try {
                if (fs::exists(config_dir)) {
                    fs::remove_all(config_dir);
                }
            } catch (const std::exception& e) {
                // Non-fatal: config removal failure shouldn't fail the entire uninstall
                report(UninstallProgress::Phase::RemovingConfig,
                       std::string("Warning: failed to remove config: ") + e.what());
            }
        }

        report(UninstallProgress::Phase::Complete, "Uninstall complete");
        return true;
    } catch (const std::exception& e) {
        report(UninstallProgress::Phase::Failed, std::string("Uninstall failed: ") + e.what());
        return false;
    } catch (...) {
        report(UninstallProgress::Phase::Failed, "Uninstall failed: unknown error");
        return false;
    }
}
