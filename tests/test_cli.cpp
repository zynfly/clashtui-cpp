#include <gtest/gtest.h>

#include "core/cli.hpp"

// ── Subcommand dispatch tests ───────────────────────────────

TEST(CLIDispatch, NoArgs_ReturnsTUI) {
    char* argv[] = { (char*)"clashtui-cpp" };
    EXPECT_EQ(CLI::run(1, argv), -1);
}

TEST(CLIDispatch, Help_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"help" };
    EXPECT_EQ(CLI::run(2, argv), 0);
}

TEST(CLIDispatch, HelpFlag_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"--help" };
    EXPECT_EQ(CLI::run(2, argv), 0);
}

TEST(CLIDispatch, HelpShort_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"-h" };
    EXPECT_EQ(CLI::run(2, argv), 0);
}

TEST(CLIDispatch, Version_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"version" };
    EXPECT_EQ(CLI::run(2, argv), 0);
}

TEST(CLIDispatch, VersionFlag_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"--version" };
    EXPECT_EQ(CLI::run(2, argv), 0);
}

TEST(CLIDispatch, Daemon_ReturnsDaemonCode) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"daemon" };
    EXPECT_EQ(CLI::run(2, argv), -2);
}

TEST(CLIDispatch, UnknownCommand_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"foobar" };
    EXPECT_EQ(CLI::run(2, argv), 1);
}

TEST(CLIDispatch, ProxyNoSubcommand_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy" };
    EXPECT_EQ(CLI::run(2, argv), 1);
}

TEST(CLIDispatch, ProxyUnknown_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy", (char*)"foobar" };
    EXPECT_EQ(CLI::run(3, argv), 1);
}

TEST(CLIDispatch, InitNoShell_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"init" };
    EXPECT_EQ(CLI::run(2, argv), 1);
}

TEST(CLIDispatch, InitBash_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"init", (char*)"bash" };
    EXPECT_EQ(CLI::run(3, argv), 0);
}

TEST(CLIDispatch, InitZsh_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"init", (char*)"zsh" };
    EXPECT_EQ(CLI::run(3, argv), 0);
}

TEST(CLIDispatch, InitUnsupportedShell_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"init", (char*)"fish" };
    EXPECT_EQ(CLI::run(3, argv), 1);
}

// ── proxy on/off/env tests ──────────────────────────────────

TEST(CLIProxy, ProxyOn_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy", (char*)"on" };
    EXPECT_EQ(CLI::run(3, argv), 0);
}

TEST(CLIProxy, ProxyOff_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy", (char*)"off" };
    EXPECT_EQ(CLI::run(3, argv), 0);
}

TEST(CLIProxy, ProxyEnv_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy", (char*)"env" };
    EXPECT_EQ(CLI::run(3, argv), 0);
}

TEST(CLIProxy, ProxyStatus_ReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy", (char*)"status" };
    EXPECT_EQ(CLI::run(3, argv), 0);
}

TEST(CLIProxy, ProxyIsEnabled_ReturnsZeroOrOne) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy", (char*)"is-enabled" };
    int rc = CLI::run(3, argv);
    EXPECT_TRUE(rc == 0 || rc == 1);
}

// ── Output format tests ─────────────────────────────────────

TEST(CLIProxy, ProxyEnvOutputContainsExports) {
    testing::internal::CaptureStdout();
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy", (char*)"env" };
    CLI::run(3, argv);
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("export http_proxy="), std::string::npos);
    EXPECT_NE(output.find("export https_proxy="), std::string::npos);
    EXPECT_NE(output.find("export all_proxy="), std::string::npos);
    EXPECT_NE(output.find("export no_proxy="), std::string::npos);
}

TEST(CLIProxy, ProxyOffOutputContainsUnset) {
    testing::internal::CaptureStdout();
    char* argv[] = { (char*)"clashtui-cpp", (char*)"proxy", (char*)"off" };
    CLI::run(3, argv);
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("unset http_proxy"), std::string::npos);
    EXPECT_NE(output.find("unset https_proxy"), std::string::npos);
    EXPECT_NE(output.find("unset all_proxy"), std::string::npos);
    EXPECT_NE(output.find("unset no_proxy"), std::string::npos);
    EXPECT_NE(output.find("unset HTTP_PROXY"), std::string::npos);
    EXPECT_NE(output.find("unset HTTPS_PROXY"), std::string::npos);
    EXPECT_NE(output.find("unset ALL_PROXY"), std::string::npos);
    EXPECT_NE(output.find("unset NO_PROXY"), std::string::npos);
}

TEST(CLIDispatch, DaemonFlag_BackwardsCompat) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"--daemon" };
    EXPECT_EQ(CLI::run(2, argv), -2);
}

// ── Port resolution tests ───────────────────────────────────

TEST(CLIResolve, DefaultPorts) {
    auto ports = CLI::resolve_ports();
    EXPECT_EQ(ports.http, 7890);
    EXPECT_EQ(ports.socks, 7891);
    EXPECT_FALSE(ports.host.empty());
}

// ── update subcommand tests ──────────────────────────────────

TEST(CLIUpdate, UpdateNoSubcommand_DispatchesAll) {
    // "update" with no subcommand dispatches to "all" — returns 0 or 1 depending on network
    char* argv[] = { (char*)"clashtui-cpp", (char*)"update" };
    int rc = CLI::run(2, argv);
    EXPECT_TRUE(rc == 0 || rc == 1);
}

TEST(CLIUpdate, UnknownSubcommand_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"update", (char*)"foobar" };
    EXPECT_EQ(CLI::run(3, argv), 1);
}

TEST(CLIUpdate, CheckOutputFormat) {
    testing::internal::CaptureStdout();
    char* argv[] = { (char*)"clashtui-cpp", (char*)"update", (char*)"check" };
    int rc = CLI::run(3, argv);
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    // Should mention clashtui-cpp version info
    EXPECT_NE(output.find("clashtui-cpp:"), std::string::npos);
    // Should mention mihomo
    EXPECT_NE(output.find("mihomo:"), std::string::npos);
}

// ── profile subcommand tests ─────────────────────────────────

TEST(CLIProfile, NoSubcommand_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"profile" };
    EXPECT_EQ(CLI::run(2, argv), 1);
}

TEST(CLIProfile, UnknownSubcommand_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"profile", (char*)"foobar" };
    EXPECT_EQ(CLI::run(3, argv), 1);
}

TEST(CLIProfile, ListReturnsZero) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"profile", (char*)"list" };
    EXPECT_EQ(CLI::run(3, argv), 0);
}

TEST(CLIProfile, AddMissingArgs_ReturnsError) {
    // Missing both name and url
    char* argv1[] = { (char*)"clashtui-cpp", (char*)"profile", (char*)"add" };
    EXPECT_EQ(CLI::run(3, argv1), 1);

    // Missing url
    char* argv2[] = { (char*)"clashtui-cpp", (char*)"profile", (char*)"add", (char*)"test" };
    EXPECT_EQ(CLI::run(4, argv2), 1);
}

TEST(CLIProfile, RmMissingArgs_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"profile", (char*)"rm" };
    EXPECT_EQ(CLI::run(3, argv), 1);
}

TEST(CLIProfile, SwitchMissingArgs_ReturnsError) {
    char* argv[] = { (char*)"clashtui-cpp", (char*)"profile", (char*)"switch" };
    EXPECT_EQ(CLI::run(3, argv), 1);
}

TEST(CLIProfile, UpdateAllReturnsZero) {
    // update with no name updates all — should succeed even with no profiles
    char* argv[] = { (char*)"clashtui-cpp", (char*)"profile", (char*)"update" };
    EXPECT_EQ(CLI::run(3, argv), 0);
}

TEST(CLIProfile, HelpContainsUpdateAndProfile) {
    testing::internal::CaptureStdout();
    char* argv[] = { (char*)"clashtui-cpp", (char*)"help" };
    CLI::run(2, argv);
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("update"), std::string::npos);
    EXPECT_NE(output.find("profile"), std::string::npos);
}
