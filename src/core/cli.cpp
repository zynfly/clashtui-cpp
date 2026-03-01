#include "core/cli.hpp"
#include "core/config.hpp"
#include "api/mihomo_client.hpp"
#include "daemon/ipc_client.hpp"
#include "daemon/daemon.hpp"

#include <yaml-cpp/yaml.h>

#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <signal.h>

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

// ── Subcommand dispatch ─────────────────────────────────────

int CLI::run(int argc, char* argv[]) {
    if (argc < 2) return -1;  // no subcommand → launch TUI

    const char* cmd = argv[1];

    if (std::strcmp(cmd, "help") == 0 || std::strcmp(cmd, "--help") == 0 || std::strcmp(cmd, "-h") == 0) {
        return cmd_help();
    }
    if (std::strcmp(cmd, "version") == 0 || std::strcmp(cmd, "--version") == 0 || std::strcmp(cmd, "-v") == 0) {
        return cmd_version();
    }
    if (std::strcmp(cmd, "status") == 0) {
        return cmd_status();
    }
    if (std::strcmp(cmd, "daemon") == 0 || std::strcmp(cmd, "--daemon") == 0) {
        return -2;  // special: caller handles daemon mode
    }
    if (std::strcmp(cmd, "init") == 0) {
        return cmd_init(argc, argv);
    }
    if (std::strcmp(cmd, "proxy") == 0) {
        return cmd_proxy(argc, argv);
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    std::cerr << "Run 'clashtui-cpp help' for usage.\n";
    return 1;
}

// ── help ────────────────────────────────────────────────────

int CLI::cmd_help() {
    std::cout <<
        "clashtui-cpp — TUI manager for Clash/Mihomo proxy\n"
        "\n"
        "Usage:\n"
        "  clashtui-cpp                Launch TUI (default)\n"
        "  clashtui-cpp daemon         Run as background daemon\n"
        "  clashtui-cpp proxy on       Enable proxy (sets env vars + remembers)\n"
        "  clashtui-cpp proxy off      Disable proxy (unsets env vars + remembers)\n"
        "  clashtui-cpp proxy env      Print export commands (no state change)\n"
        "  clashtui-cpp proxy status   Show proxy ports and env var status\n"
        "  clashtui-cpp status         Show daemon and mihomo status\n"
        "  clashtui-cpp init <shell>   Print shell init function (bash/zsh)\n"
        "  clashtui-cpp version        Show version\n"
        "  clashtui-cpp help           Show this help\n"
        "\n"
        "Setup (add to ~/.bashrc or ~/.zshrc, one-time):\n"
        "  eval \"$(clashtui-cpp init bash)\"   # for bash\n"
        "  eval \"$(clashtui-cpp init zsh)\"    # for zsh\n"
        "\n"
        "After setup:\n"
        "  clashtui-cpp proxy on    # enables proxy, new shells auto-enable too\n"
        "  clashtui-cpp proxy off   # disables proxy, new shells stay clean\n"
        "\n"
        "Without init, use eval manually:\n"
        "  eval \"$(clashtui-cpp proxy env)\"\n"
        "\n"
        "Keyboard shortcuts (TUI mode):\n"
        "  Alt+1/2/3   Switch Global/Rule/Direct mode\n"
        "  S           Subscription panel\n"
        "  I           Install wizard\n"
        "  L           Log panel\n"
        "  C           Config panel\n"
        "  Ctrl+L      Toggle EN/ZH language\n"
        "  Q           Quit\n";
    return 0;
}

// ── version ─────────────────────────────────────────────────

int CLI::cmd_version() {
    std::cout << "clashtui-cpp " << APP_VERSION << "\n";
    return 0;
}

// ── init ────────────────────────────────────────────────────

int CLI::cmd_init(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: clashtui-cpp init <bash|zsh>\n";
        return 1;
    }

    const char* shell = argv[2];
    if (std::strcmp(shell, "bash") != 0 && std::strcmp(shell, "zsh") != 0) {
        std::cerr << "Unsupported shell: " << shell << "\n";
        std::cerr << "Supported: bash, zsh\n";
        return 1;
    }

    // Output a shell function that wraps proxy on/off with eval,
    // and auto-enables proxy on shell startup if previously enabled
    std::cout <<
        "clashtui-cpp() {\n"
        "  case \"$1\" in\n"
        "    proxy)\n"
        "      case \"$2\" in\n"
        "        on|off)\n"
        "          eval \"$(command clashtui-cpp \"$@\")\"\n"
        "          ;;\n"
        "        *)\n"
        "          command clashtui-cpp \"$@\"\n"
        "          ;;\n"
        "      esac\n"
        "      ;;\n"
        "    *)\n"
        "      command clashtui-cpp \"$@\"\n"
        "      ;;\n"
        "  esac\n"
        "}\n"
        "\n"
        "# Auto-enable proxy if previously set to on\n"
        "if command clashtui-cpp proxy is-enabled >/dev/null 2>&1; then\n"
        "  eval \"$(command clashtui-cpp proxy env)\"\n"
        "fi\n";

    return 0;
}

// ── status ──────────────────────────────────────────────────

int CLI::cmd_status() {
    Config config;
    config.load();

    // Daemon status
    DaemonClient dc;
    bool daemon_running = dc.is_daemon_running();
    std::cout << "Daemon:  " << (daemon_running ? "running" : "stopped") << "\n";

    if (daemon_running) {
        auto st = dc.get_status();
        std::cout << "Mihomo:  " << (st.mihomo_running ? "running (pid " + std::to_string(st.mihomo_pid) + ")" : "stopped") << "\n";
        if (!st.active_profile.empty()) {
            std::cout << "Profile: " << st.active_profile << "\n";
        }
    }

    // Mihomo API status
    auto& d = config.data();
    MihomoClient client(d.api_host, d.api_port, d.api_secret);
    if (client.test_connection()) {
        auto ver = client.get_version();
        std::cout << "API:     connected (mihomo " << ver.version << ")\n";
        auto cfg = client.get_config();
        std::cout << "Mode:    " << cfg.mode << "\n";
        if (cfg.mixed_port > 0)
            std::cout << "HTTP:    " << d.api_host << ":" << cfg.mixed_port << "\n";
        if (cfg.socks_port > 0)
            std::cout << "SOCKS:   " << d.api_host << ":" << cfg.socks_port << "\n";
        auto stats = client.get_connections();
        std::cout << "Conns:   " << stats.active_connections << " active\n";
    } else {
        std::cout << "API:     not connected\n";
    }

    return 0;
}

// ── proxy ───────────────────────────────────────────────────

int CLI::cmd_proxy(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: clashtui-cpp proxy <on|off|env|status|is-enabled>\n";
        return 1;
    }

    const char* sub = argv[2];
    if (std::strcmp(sub, "on") == 0) return proxy_on();
    if (std::strcmp(sub, "off") == 0) return proxy_off();
    if (std::strcmp(sub, "env") == 0) return proxy_env();
    if (std::strcmp(sub, "status") == 0) return proxy_status();
    if (std::strcmp(sub, "is-enabled") == 0) return proxy_is_enabled();

    std::cerr << "Unknown proxy command: " << sub << "\n";
    std::cerr << "Usage: clashtui-cpp proxy <on|off|env|status|is-enabled>\n";
    return 1;
}

int CLI::proxy_on() {
    auto ports = resolve_ports();
    print_export_lines(ports);
    save_proxy_state(true);
    if (!check_shell_init_installed()) {
        // Output to stderr so it doesn't interfere with eval
        std::cerr << "\n"
            "NOTE: Shell init not detected. To make proxy on/off work directly,\n"
            "add this to your shell config:\n"
            "\n"
            "  # For bash (~/.bashrc):\n"
            "  eval \"$(clashtui-cpp init bash)\"\n"
            "\n"
            "  # For zsh (~/.zshrc):\n"
            "  eval \"$(clashtui-cpp init zsh)\"\n"
            "\n"
            "Without it, use:  eval \"$(clashtui-cpp proxy env)\"\n";
    }
    return 0;
}

int CLI::proxy_off() {
    print_unset_lines();
    save_proxy_state(false);
    return 0;
}

int CLI::proxy_status() {
    auto ports = resolve_ports();

    std::cout << "Resolved ports:\n";
    std::cout << "  HTTP/Mixed: " << ports.host << ":" << ports.http << "\n";
    std::cout << "  SOCKS:      " << ports.host << ":" << ports.socks << "\n";
    std::cout << "\n";

    // Check current env vars
    const char* hp  = std::getenv("http_proxy");
    const char* hsp = std::getenv("https_proxy");
    const char* ap  = std::getenv("all_proxy");
    const char* np  = std::getenv("no_proxy");
    std::cout << "Current environment:\n";
    std::cout << "  http_proxy:  " << (hp  ? hp  : "(not set)") << "\n";
    std::cout << "  https_proxy: " << (hsp ? hsp : "(not set)") << "\n";
    std::cout << "  all_proxy:   " << (ap  ? ap  : "(not set)") << "\n";
    std::cout << "  no_proxy:    " << (np  ? np  : "(not set)") << "\n";

    bool active = (hp != nullptr && hp[0] != '\0');
    std::cout << "\nProxy: " << (active ? "ACTIVE" : "INACTIVE") << "\n";

    Config config;
    config.load();
    std::cout << "Remembered: " << (config.data().proxy_enabled ? "on" : "off")
              << " (new shells will " << (config.data().proxy_enabled ? "auto-enable" : "not enable") << " proxy)\n";
    return 0;
}

// ── Port resolution (API → YAML → defaults) ────────────────

CLI::ProxyPorts CLI::resolve_ports() {
    ProxyPorts ports;

    Config config;
    config.load();
    auto& d = config.data();

    ports.host = d.api_host;

    // Tier 1: Try mihomo REST API
    try {
        MihomoClient client(d.api_host, d.api_port, d.api_secret);
        if (client.test_connection()) {
            auto cfg = client.get_config();
            if (cfg.mixed_port > 0) ports.http = cfg.mixed_port;
            if (cfg.socks_port > 0) ports.socks = cfg.socks_port;
            if (cfg.port > 0 && ports.http == 7890) ports.http = cfg.port;
            return ports;
        }
    } catch (...) {}

    // Tier 2: Parse mihomo config YAML
    try {
        std::string yaml_path = d.mihomo_config_path;
        if (!yaml_path.empty()) {
            yaml_path = Config::expand_home(yaml_path);
            std::ifstream fin(yaml_path);
            if (fin.is_open()) {
                auto yaml = YAML::Load(fin);
                if (yaml["mixed-port"]) ports.http = yaml["mixed-port"].as<int>();
                if (yaml["socks-port"]) ports.socks = yaml["socks-port"].as<int>();
                if (yaml["port"] && ports.http == 7890) ports.http = yaml["port"].as<int>();
            }
        }
    } catch (...) {}

    // Tier 3: defaults (already set in ProxyPorts initializer)
    return ports;
}

CLI::ProxyPorts CLI::resolve_ports_fast() {
    ProxyPorts ports;

    Config config;
    config.load();
    auto& d = config.data();

    ports.host = d.api_host;

    // Only YAML + defaults (no API call — fast for shell startup)
    try {
        std::string yaml_path = d.mihomo_config_path;
        if (!yaml_path.empty()) {
            yaml_path = Config::expand_home(yaml_path);
            std::ifstream fin(yaml_path);
            if (fin.is_open()) {
                auto yaml = YAML::Load(fin);
                if (yaml["mixed-port"]) ports.http = yaml["mixed-port"].as<int>();
                if (yaml["socks-port"]) ports.socks = yaml["socks-port"].as<int>();
                if (yaml["port"] && ports.http == 7890) ports.http = yaml["port"].as<int>();
            }
        }
    } catch (...) {}

    return ports;
}

int CLI::proxy_env() {
    // Fast path: no API call, used by init auto-enable on shell startup
    auto ports = resolve_ports_fast();
    print_export_lines(ports);
    return 0;
}

int CLI::proxy_is_enabled() {
    Config config;
    config.load();
    return config.data().proxy_enabled ? 0 : 1;
}

// ── State persistence ───────────────────────────────────────

void CLI::save_proxy_state(bool enabled) {
    try {
        Config config;
        config.load();
        config.data().proxy_enabled = enabled;
        if (!config.save()) {
            std::cerr << "Warning: could not save proxy state to config file\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: could not save proxy state: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "Warning: could not save proxy state to config file\n";
    }
}

bool CLI::check_shell_init_installed() {
    // Check common RC files for our init line
    const char* home = std::getenv("HOME");
    if (!home) return false;

    std::string home_str(home);
    std::vector<std::string> rc_files = {
        home_str + "/.bashrc",
        home_str + "/.zshrc",
        home_str + "/.bash_profile",
        home_str + "/.zprofile",
        home_str + "/.profile",
    };

    for (const auto& rc : rc_files) {
        std::ifstream fin(rc);
        if (!fin.is_open()) continue;
        std::string line;
        while (std::getline(fin, line)) {
            if (line.find("clashtui-cpp init") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// ── Output helpers ──────────────────────────────────────────

void CLI::print_export_lines(const ProxyPorts& ports) {
    std::string http_url = "http://" + ports.host + ":" + std::to_string(ports.http);
    std::string socks_url = "socks5://" + ports.host + ":" + std::to_string(ports.socks);

    std::cout << "export http_proxy=\"" << http_url << "\"\n";
    std::cout << "export https_proxy=\"" << http_url << "\"\n";
    std::cout << "export all_proxy=\"" << socks_url << "\"\n";
    std::cout << "export no_proxy=\"localhost,127.0.0.1,::1\"\n";
    std::cout << "export HTTP_PROXY=\"" << http_url << "\"\n";
    std::cout << "export HTTPS_PROXY=\"" << http_url << "\"\n";
    std::cout << "export ALL_PROXY=\"" << socks_url << "\"\n";
    std::cout << "export NO_PROXY=\"localhost,127.0.0.1,::1\"\n";
}

void CLI::print_unset_lines() {
    std::cout << "unset http_proxy\n";
    std::cout << "unset https_proxy\n";
    std::cout << "unset all_proxy\n";
    std::cout << "unset no_proxy\n";
    std::cout << "unset HTTP_PROXY\n";
    std::cout << "unset HTTPS_PROXY\n";
    std::cout << "unset ALL_PROXY\n";
    std::cout << "unset NO_PROXY\n";
}
