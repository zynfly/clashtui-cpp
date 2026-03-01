#pragma once

#include <string>

class CLI {
public:
    /// Parse argv and dispatch to subcommand.
    /// Returns exit code, or -1 if no subcommand (caller should launch TUI).
    static int run(int argc, char* argv[]);

    struct ProxyPorts {
        int http = 7890;
        int socks = 7891;
        std::string host = "127.0.0.1";
    };

    /// Resolve proxy ports: API → config YAML → defaults
    static ProxyPorts resolve_ports();
    /// Fast port resolution: config YAML → defaults (no API call, for shell startup)
    static ProxyPorts resolve_ports_fast();

private:
    static int cmd_help();
    static int cmd_version();
    static int cmd_status();
    static int cmd_init(int argc, char* argv[]);
    static int cmd_proxy(int argc, char* argv[]);

    static int proxy_on();
    static int proxy_off();
    static int proxy_env();
    static int proxy_status();
    static int proxy_is_enabled();

    static void print_export_lines(const ProxyPorts& ports);
    static void print_unset_lines();

    static void save_proxy_state(bool enabled);
    static bool check_shell_init_installed();
};
