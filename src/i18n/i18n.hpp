#pragma once

#include <atomic>

enum class Lang { EN, ZH };

struct Strings {
    // General
    const char* app_title;
    const char* connected;
    const char* disconnected;
    const char* confirm;
    const char* cancel;

    // Mode
    const char* mode_global;
    const char* mode_rule;
    const char* mode_direct;

    // Proxy panel
    const char* panel_groups;
    const char* panel_nodes;
    const char* panel_details;
    const char* switching_node;
    const char* testing_delay;
    const char* test_all;

    // Subscription
    const char* panel_subscription;
    const char* sub_add;
    const char* sub_update;
    const char* sub_delete;
    const char* sub_last_updated;
    const char* sub_downloading;
    const char* sub_success;
    const char* sub_failed;

    // Install wizard
    const char* install_title;
    const char* install_not_found;
    const char* install_downloading;
    const char* install_verifying;
    const char* install_complete;
    const char* install_select_path;

    // Install wizard (extended)
    const char* install_installed;
    const char* install_checking;
    const char* install_fetching_release;
    const char* install_ready;
    const char* install_extracting;
    const char* install_installing;
    const char* install_needs_sudo;
    const char* install_user_only;
    const char* install_check_update;
    const char* install_up_to_date;
    const char* install_upgrade_available;
    const char* install_upgrade;
    const char* install_confirm_download;
    const char* install_trying_direct;
    const char* install_trying_proxy;
    const char* install_checksum_ok;
    const char* install_checksum_fail;
    const char* install_checksum_skip;
    const char* install_platform;
    const char* install_no_asset;

    // Uninstall
    const char* uninstall_title;
    const char* uninstall_confirm;
    const char* uninstall_remove_config;
    const char* uninstall_stopping;
    const char* uninstall_disabling;
    const char* uninstall_removing_svc;
    const char* uninstall_removing_bin;
    const char* uninstall_removing_cfg;
    const char* uninstall_complete;
    const char* uninstall_failed;

    // Systemd service
    const char* service_setup;
    const char* service_create_prompt;
    const char* service_created;
    const char* service_skipped;
    const char* service_system_level;
    const char* service_user_level;
    const char* service_active;
    const char* service_inactive;

    // Log panel
    const char* log_freeze;
    const char* log_unfreeze;
    const char* log_export;

    // Errors
    const char* err_api_failed;
    const char* err_download_failed;
    const char* err_invalid_config;
    const char* err_node_switch_failed;

    // Daemon
    const char* daemon_running;
    const char* daemon_not_running;
    const char* daemon_starting;
    const char* daemon_stopping;

    // Profile
    const char* profile_active;
    const char* profile_switch;
    const char* profile_switch_success;
    const char* profile_no_daemon;
    const char* profile_updating;
    const char* profile_updating_all;
    const char* profile_none;

    // Mihomo process
    const char* mihomo_starting;
    const char* mihomo_stopping;
    const char* mihomo_restarting;
    const char* mihomo_crashed;

    // Daemon service
    const char* daemon_service_desc;

    // Service management (extended)
    const char* service_start;
    const char* service_stop;
    const char* service_install;
    const char* service_remove;
    const char* service_started;
    const char* service_stopped;
    const char* service_removed;
    const char* service_not_installed;

    // Self-uninstall
    const char* uninstall_self_title;
    const char* uninstall_self_confirm;
    const char* uninstall_self_remove_config;
    const char* uninstall_self_complete;

    // Self version / update check in install wizard
    const char* install_self_version;
    const char* install_self_up_to_date;
    const char* install_self_update_available;
    const char* install_checking_updates;
};

#include "i18n/en.hpp"
#include "i18n/zh.hpp"

inline const Strings EN_STRINGS = EN_STRINGS_DEF;
inline const Strings ZH_STRINGS = ZH_STRINGS_DEF;
inline std::atomic<Lang> current_lang{Lang::ZH};

inline const Strings& T() {
    return current_lang.load() == Lang::ZH ? ZH_STRINGS : EN_STRINGS;
}
