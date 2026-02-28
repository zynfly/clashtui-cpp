#include "app.hpp"
#include "core/config.hpp"
#include "daemon/daemon.hpp"

#include <cstring>
#include <signal.h>

static Daemon* g_daemon = nullptr;

static void signal_handler(int /*sig*/) {
    if (g_daemon) {
        g_daemon->request_stop();
    }
}

int main(int argc, char* argv[]) {
    bool daemon_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--daemon") == 0) {
            daemon_mode = true;
        }
    }

    if (daemon_mode) {
        Config config;
        config.load();

        Daemon daemon(config);
        g_daemon = &daemon;

        // Register signal handlers for graceful shutdown
        struct sigaction sa;
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);

        int ret = daemon.run();
        g_daemon = nullptr;
        return ret;
    } else {
        App app;
        app.run();
        return 0;
    }
}
