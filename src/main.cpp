#include "core/cli.hpp"
#include "core/config.hpp"
#include "daemon/daemon.hpp"
#include "app.hpp"

#include <signal.h>

static Daemon* g_daemon = nullptr;

static void signal_handler(int /*sig*/) {
    if (g_daemon) {
        g_daemon->request_stop();
    }
}

static int run_daemon() {
    Config config;
    config.load();

    Daemon daemon(config);
    g_daemon = &daemon;

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    int ret = daemon.run();
    g_daemon = nullptr;
    return ret;
}

int main(int argc, char* argv[]) {
    int cli_result = CLI::run(argc, argv);

    if (cli_result == -2) {
        // daemon subcommand
        return run_daemon();
    }
    if (cli_result != -1) {
        // handled by CLI (help, version, status, proxy, or error)
        return cli_result;
    }

    // No subcommand → launch TUI
    App app;
    app.run();
    return 0;
}
