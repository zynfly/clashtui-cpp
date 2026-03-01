#include "core/updater.hpp"
#include "core/installer.hpp"
#include "core/config.hpp"

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
#include <filesystem>
#include <fstream>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

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

// ════════════════════════════════════════════════════════════════
// get_self_path — locate the currently running binary
// ════════════════════════════════════════════════════════════════

std::string Updater::get_self_path() {
    try {
#ifdef __APPLE__
        // macOS: use _NSGetExecutablePath + realpath
        char raw_path[4096];
        uint32_t size = sizeof(raw_path);
        if (_NSGetExecutablePath(raw_path, &size) == 0) {
            char resolved[PATH_MAX];
            if (realpath(raw_path, resolved)) {
                return std::string(resolved);
            }
            return std::string(raw_path);
        }
#else
        // Linux: /proc/self/exe is a symlink to the running binary
        return fs::canonical("/proc/self/exe").string();
#endif
    } catch (...) {}
    return "";
}

// ════════════════════════════════════════════════════════════════
// Shell-escape helper (local to this TU)
// ════════════════════════════════════════════════════════════════

static std::string updater_shell_quote(const std::string& s) {
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

// ════════════════════════════════════════════════════════════════
// apply_self_update — download and replace the clashtui-cpp binary
// ════════════════════════════════════════════════════════════════

UpdateResult Updater::apply_self_update() const {
    UpdateResult result;

    try {
        // Step 1: Check for update
        UpdateInfo info = check_for_update();
        if (!info.available) {
            result.success = true;
            result.message = "Already up to date (v" + info.current_version + ")";
            return result;
        }

        if (info.download_url.empty()) {
            result.message = "No matching binary asset found for this architecture";
            return result;
        }

        // Step 2: Download .tar.gz to temp location
        std::string tmp_archive = "/tmp/clashtui-cpp-update.tar.gz";
        std::string tmp_extract_dir = "/tmp/clashtui-cpp-update-extract";

        // Clean up any previous temp files
        try { fs::remove(tmp_archive); } catch (...) {}
        try { fs::remove_all(tmp_extract_dir); } catch (...) {}

        if (!Installer::download_with_fallback(info.download_url, tmp_archive, nullptr, nullptr)) {
            result.message = "Failed to download update from " + info.download_url;
            try { fs::remove(tmp_archive); } catch (...) {}
            return result;
        }

        // Step 3: Try to download and verify SHA256
        // Construct sha256 URL by appending .sha256 to the download URL
        std::string sha256_url = info.download_url + ".sha256";
        std::string tmp_sha256 = "/tmp/clashtui-cpp-update.tar.gz.sha256";

        if (Installer::download_with_fallback(sha256_url, tmp_sha256, nullptr, nullptr)) {
            // Read expected hash from the sha256 file
            std::ifstream sha_file(tmp_sha256);
            std::string expected_hash;
            if (sha_file.is_open()) {
                sha_file >> expected_hash;
                sha_file.close();
            }

            if (!expected_hash.empty()) {
                if (!Installer::verify_sha256(tmp_archive, expected_hash)) {
                    result.message = "SHA256 checksum verification failed";
                    try { fs::remove(tmp_archive); } catch (...) {}
                    try { fs::remove(tmp_sha256); } catch (...) {}
                    return result;
                }
            }
            try { fs::remove(tmp_sha256); } catch (...) {}
        }
        // SHA256 file not available is non-fatal — proceed without verification

        // Step 4: Extract .tar.gz
        fs::create_directories(tmp_extract_dir);

        std::string tar_cmd = "tar xzf " + updater_shell_quote(tmp_archive) +
                              " -C " + updater_shell_quote(tmp_extract_dir);
        if (system(tar_cmd.c_str()) != 0) {
            result.message = "Failed to extract update archive";
            try { fs::remove(tmp_archive); } catch (...) {}
            try { fs::remove_all(tmp_extract_dir); } catch (...) {}
            return result;
        }

        // Step 5: Find the extracted binary
        // Look for 'clashtui-cpp' in the extracted directory (may be top-level or in a subdirectory)
        std::string new_binary;
        for (auto& entry : fs::recursive_directory_iterator(tmp_extract_dir)) {
            if (entry.is_regular_file() && entry.path().filename() == "clashtui-cpp") {
                new_binary = entry.path().string();
                break;
            }
        }

        if (new_binary.empty()) {
            result.message = "Could not find clashtui-cpp binary in extracted archive";
            try { fs::remove(tmp_archive); } catch (...) {}
            try { fs::remove_all(tmp_extract_dir); } catch (...) {}
            return result;
        }

        // Step 6: Replace the current binary
        std::string self_path = get_self_path();
        if (self_path.empty()) {
            result.message = "Could not determine path of current binary";
            try { fs::remove(tmp_archive); } catch (...) {}
            try { fs::remove_all(tmp_extract_dir); } catch (...) {}
            return result;
        }

        if (access(self_path.c_str(), W_OK) == 0) {
            // We have write permission — copy directly
            try {
                fs::copy_file(new_binary, self_path, fs::copy_options::overwrite_existing);
                fs::permissions(self_path,
                                fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                                fs::perm_options::add);
            } catch (const std::exception& e) {
                result.message = std::string("Failed to replace binary: ") + e.what();
                try { fs::remove(tmp_archive); } catch (...) {}
                try { fs::remove_all(tmp_extract_dir); } catch (...) {}
                return result;
            }
        } else {
            // Need sudo to replace
            std::string cmd = "sudo cp " + updater_shell_quote(new_binary) + " " +
                              updater_shell_quote(self_path) + " && sudo chmod +x " +
                              updater_shell_quote(self_path);
            if (system(cmd.c_str()) != 0) {
                result.message = "Failed to replace binary (sudo cp failed)";
                try { fs::remove(tmp_archive); } catch (...) {}
                try { fs::remove_all(tmp_extract_dir); } catch (...) {}
                return result;
            }
        }

        // Step 7: Clean up
        try { fs::remove(tmp_archive); } catch (...) {}
        try { fs::remove_all(tmp_extract_dir); } catch (...) {}

        result.success = true;
        result.message = "Updated from v" + info.current_version + " to " + info.latest_version +
                         ". Please restart clashtui-cpp.";
    } catch (const std::exception& e) {
        result.message = std::string("Self-update failed: ") + e.what();
    } catch (...) {
        result.message = "Self-update failed: unknown error";
    }

    return result;
}

// ════════════════════════════════════════════════════════════════
// update_mihomo — download and replace the mihomo binary
// ════════════════════════════════════════════════════════════════

UpdateResult Updater::update_mihomo() const {
    UpdateResult result;

    try {
        // Step 1: Load config to get binary_path and service_name
        Config cfg;
        cfg.load();
        const auto& data = cfg.data();

        std::string binary_path = data.mihomo_binary_path;
        std::string service_name = data.mihomo_service_name;

        // Step 2: Get current installed version
        std::string local_version = Installer::get_running_version(binary_path);

        // Step 3: Fetch latest release from GitHub
        ReleaseInfo release = Installer::fetch_latest_release();
        if (release.version.empty()) {
            result.message = "Failed to fetch latest mihomo release info";
            return result;
        }

        // Step 4: Compare versions
        if (!local_version.empty() && !Installer::is_newer_version(local_version, release.version)) {
            result.success = true;
            result.message = "Mihomo is already up to date (" + release.version + ")";
            return result;
        }

        // Step 5: Select the correct asset for this platform
        PlatformInfo platform = Installer::detect_platform();
        AssetInfo asset = Installer::select_asset(release, platform);

        if (asset.download_url.empty()) {
            result.message = "No matching mihomo asset found for " + platform.os + "-" + platform.arch;
            return result;
        }

        // Step 6: Check if mihomo service is running and stop it
        // Determine service scope based on binary path
        ServiceScope scope = ServiceScope::System;
        if (binary_path.find("/usr/") != 0 && binary_path.find("/opt/") != 0) {
            scope = ServiceScope::User;
        }

        bool was_running = Installer::has_systemd() &&
                           Installer::is_service_active(service_name, scope);

        if (was_running) {
            Installer::stop_service(service_name, scope);
        }

        // Step 7: Download to temp location
        std::string tmp_gz = "/tmp/mihomo-update.gz";
        try { fs::remove(tmp_gz); } catch (...) {}

        if (!Installer::download_with_fallback(asset.download_url, tmp_gz, nullptr, nullptr)) {
            // Restart service if it was running before download failure
            if (was_running) {
                Installer::start_service(service_name, scope);
            }
            result.message = "Failed to download mihomo from " + asset.download_url;
            return result;
        }

        // Step 8: Verify SHA256 if checksums are available
        if (!release.checksums_url.empty()) {
            std::string expected_hash = Installer::fetch_checksum_for_file(
                release.checksums_url, asset.name);
            if (!expected_hash.empty()) {
                if (!Installer::verify_sha256(tmp_gz, expected_hash)) {
                    try { fs::remove(tmp_gz); } catch (...) {}
                    if (was_running) {
                        Installer::start_service(service_name, scope);
                    }
                    result.message = "SHA256 checksum verification failed for mihomo";
                    return result;
                }
            }
        }

        // Step 9: Install binary (extract + copy + chmod, handles sudo)
        bool needs_sudo = (binary_path.find("/usr/") == 0 || binary_path.find("/opt/") == 0);
        if (!Installer::install_binary(tmp_gz, binary_path, needs_sudo)) {
            try { fs::remove(tmp_gz); } catch (...) {}
            if (was_running) {
                Installer::start_service(service_name, scope);
            }
            result.message = "Failed to install mihomo binary to " + binary_path;
            return result;
        }

        // Step 10: Clean up
        try { fs::remove(tmp_gz); } catch (...) {}

        // Step 11: Restart service if it was running
        if (was_running) {
            Installer::start_service(service_name, scope);
        }

        result.success = true;
        std::string version_info = release.version;
        if (!local_version.empty()) {
            result.message = "Mihomo updated from " + local_version + " to " + version_info;
        } else {
            result.message = "Mihomo updated to " + version_info;
        }
    } catch (const std::exception& e) {
        result.message = std::string("Mihomo update failed: ") + e.what();
    } catch (...) {
        result.message = "Mihomo update failed: unknown error";
    }

    return result;
}
