#pragma once

// English string table - included by i18n.hpp after Strings is defined

inline constexpr Strings EN_STRINGS_DEF = {
    // General
    "clashtui-cpp",
    "Connected",
    "Disconnected",
    "Confirm",
    "Cancel",

    // Mode
    "Global",
    "Rule",
    "Direct",

    // Proxy panel
    "Groups",
    "Nodes",
    "Details",
    "Switching node...",
    "Testing delay...",
    "Test All",

    // Subscription
    "Subscriptions",
    "Add",
    "Update",
    "Delete",
    "Last updated",
    "Downloading...",
    "Success",
    "Failed",

    // Install wizard
    "Install Wizard",
    "Mihomo not found",
    "Downloading...",
    "Verifying...",
    "Installation complete",
    "Select install path",

    // Install wizard (extended)
    "Mihomo is installed",
    "Checking installation...",
    "Fetching latest release...",
    "Ready to install",
    "Extracting...",
    "Installing...",
    "Requires sudo",
    "User install (no sudo)",
    "Check for updates",
    "Already up to date",
    "New version available",
    "Upgrade",
    "Press Enter to download",
    "Trying direct download...",
    "Trying mirror...",
    "Checksum verified",
    "Checksum verification failed!",
    "Checksum not available",
    "Platform",
    "No compatible binary found",

    // Uninstall
    "Uninstall Mihomo",
    "Are you sure?",
    "Also remove config files?",
    "Stopping service...",
    "Disabling service...",
    "Removing service file...",
    "Removing binary...",
    "Removing configuration...",
    "Uninstall complete",
    "Uninstall failed",

    // Systemd service
    "Systemd Service Setup",
    "Create systemd service?",
    "Service created and started",
    "Service setup skipped",
    "System service",
    "User service",
    "active",
    "inactive",

    // Log panel
    "Freeze",
    "Unfreeze",
    "Export",

    // Errors
    "API request failed",
    "Download failed",
    "Invalid configuration",
    "Node switch failed",

    // Daemon
    "Daemon running",
    "Daemon not running",
    "Starting daemon...",
    "Stopping daemon...",

    // Profile
    "(active)",
    "Switch profile",
    "Profile switched",
    "Start daemon for auto-update",
    "Updating profile...",
    "Updating all profiles...",
    "No profiles",

    // Mihomo process
    "Starting mihomo...",
    "Stopping mihomo...",
    "Restarting mihomo...",
    "Mihomo crashed",

    // Daemon service
    "clashtui-cpp Daemon",
};
