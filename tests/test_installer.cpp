#include <gtest/gtest.h>
#include "core/installer.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class InstallerTest : public ::testing::Test {
protected:
    std::string test_dir;

    void SetUp() override {
        test_dir = (fs::temp_directory_path() / "clashtui-test-installer").string();
        fs::create_directories(test_dir);
    }

    void TearDown() override {
        fs::remove_all(test_dir);
    }
};

TEST_F(InstallerTest, IsInstalledReturnsFalseForMissing) {
    EXPECT_FALSE(Installer::is_installed("/nonexistent/path/mihomo"));
}

TEST_F(InstallerTest, IsInstalledReturnsTrueForExisting) {
    std::string fake_binary = test_dir + "/mihomo";
    std::ofstream out(fake_binary);
    out << "fake";
    out.close();

    EXPECT_TRUE(Installer::is_installed(fake_binary));
}

TEST_F(InstallerTest, GetVersionForMissingBinary) {
    std::string version = Installer::get_running_version("/nonexistent/mihomo");
    EXPECT_TRUE(version.empty());
}

TEST_F(InstallerTest, GenerateDefaultConfig) {
    std::string config_path = test_dir + "/mihomo/config.yaml";

    ASSERT_TRUE(Installer::generate_default_config(config_path));
    EXPECT_TRUE(fs::exists(config_path));

    // Read and verify content
    std::ifstream in(config_path);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("mixed-port: 7890"), std::string::npos);
    EXPECT_NE(content.find("mode: rule"), std::string::npos);
    EXPECT_NE(content.find("external-controller: 127.0.0.1:9090"), std::string::npos);
    EXPECT_NE(content.find("dns:"), std::string::npos);
    EXPECT_NE(content.find("MATCH,DIRECT"), std::string::npos);
}

TEST_F(InstallerTest, GenerateDefaultConfigCreatesParentDirs) {
    std::string deep_path = test_dir + "/a/b/c/config.yaml";

    ASSERT_TRUE(Installer::generate_default_config(deep_path));
    EXPECT_TRUE(fs::exists(deep_path));
}

// ========== Phase 8: detect_platform() tests ==========

TEST_F(InstallerTest, DetectPlatformReturnsValidOS) {
    auto platform = Installer::detect_platform();
    EXPECT_FALSE(platform.os.empty());
    // Should be one of: linux, darwin
    EXPECT_TRUE(platform.os == "linux" || platform.os == "darwin");
}

TEST_F(InstallerTest, DetectPlatformReturnsValidArch) {
    auto platform = Installer::detect_platform();
    EXPECT_FALSE(platform.arch.empty());
    // Should be one of known architectures
    EXPECT_TRUE(platform.arch == "amd64" || platform.arch == "arm64" ||
                platform.arch == "armv7" || platform.arch == "386" ||
                platform.arch == "s390x" || platform.arch == "riscv64" ||
                platform.arch == "mips64");
}

// ========== Phase 8: select_asset() tests ==========

TEST_F(InstallerTest, SelectAssetFindsMatch) {
    ReleaseInfo release;
    release.assets = {
        {"mihomo-linux-amd64-v1.19.0.gz", "https://example.com/amd64.gz", 10000},
        {"mihomo-linux-arm64-v1.19.0.gz", "https://example.com/arm64.gz", 12000},
        {"mihomo-darwin-amd64-v1.19.0.gz", "https://example.com/darwin.gz", 11000},
    };

    PlatformInfo p{"linux", "arm64"};
    auto asset = Installer::select_asset(release, p);
    EXPECT_EQ(asset.name, "mihomo-linux-arm64-v1.19.0.gz");
    EXPECT_EQ(asset.download_url, "https://example.com/arm64.gz");
}

TEST_F(InstallerTest, SelectAssetReturnsEmptyForNoMatch) {
    ReleaseInfo release;
    release.assets = {
        {"mihomo-linux-amd64-v1.19.0.gz", "https://example.com/amd64.gz", 10000},
    };

    PlatformInfo p{"linux", "mips64"};
    auto asset = Installer::select_asset(release, p);
    EXPECT_TRUE(asset.name.empty());
}

TEST_F(InstallerTest, SelectAssetPrefersNonAlpha) {
    ReleaseInfo release;
    release.assets = {
        {"mihomo-linux-arm64-alpha-v1.19.0.gz", "https://example.com/alpha.gz", 10000},
        {"mihomo-linux-arm64-v1.19.0.gz", "https://example.com/stable.gz", 12000},
    };

    PlatformInfo p{"linux", "arm64"};
    auto asset = Installer::select_asset(release, p);
    EXPECT_EQ(asset.download_url, "https://example.com/stable.gz");
}

// ========== Phase 8: is_newer_version() tests ==========

TEST_F(InstallerTest, IsNewerVersionBasic) {
    EXPECT_TRUE(Installer::is_newer_version("v1.18.0", "v1.19.0"));
    EXPECT_FALSE(Installer::is_newer_version("v1.19.0", "v1.18.0"));
    EXPECT_FALSE(Installer::is_newer_version("v1.19.0", "v1.19.0"));
}

TEST_F(InstallerTest, IsNewerVersionMajor) {
    EXPECT_TRUE(Installer::is_newer_version("v1.19.0", "v2.0.0"));
    EXPECT_FALSE(Installer::is_newer_version("v2.0.0", "v1.99.99"));
}

TEST_F(InstallerTest, IsNewerVersionPatch) {
    EXPECT_TRUE(Installer::is_newer_version("v1.19.0", "v1.19.1"));
    EXPECT_FALSE(Installer::is_newer_version("v1.19.1", "v1.19.0"));
}

TEST_F(InstallerTest, IsNewerVersionInvalidStrings) {
    EXPECT_FALSE(Installer::is_newer_version("", "v1.0.0"));
    EXPECT_FALSE(Installer::is_newer_version("v1.0.0", ""));
    EXPECT_FALSE(Installer::is_newer_version("abc", "def"));
}

// ========== Phase 8: verify_sha256() tests ==========

TEST_F(InstallerTest, VerifySha256CorrectHash) {
    std::string file_path = test_dir + "/testfile.txt";
    std::ofstream out(file_path, std::ios::binary);
    out << "hello world";
    out.close();
    // SHA256 of "hello world" = b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
    EXPECT_TRUE(Installer::verify_sha256(file_path, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9"));
}

TEST_F(InstallerTest, VerifySha256WrongHash) {
    std::string file_path = test_dir + "/testfile.txt";
    std::ofstream out(file_path, std::ios::binary);
    out << "hello world";
    out.close();
    EXPECT_FALSE(Installer::verify_sha256(file_path, "0000000000000000000000000000000000000000000000000000000000000000"));
}

TEST_F(InstallerTest, VerifySha256MissingFile) {
    EXPECT_FALSE(Installer::verify_sha256("/nonexistent/file", "abc123"));
}

TEST_F(InstallerTest, VerifySha256EmptyHash) {
    std::string file_path = test_dir + "/testfile.txt";
    std::ofstream out(file_path, std::ios::binary);
    out << "test";
    out.close();
    // Empty expected hash should never match a computed hash
    EXPECT_FALSE(Installer::verify_sha256(file_path, ""));
}

// ========== Phase 8: generate_service_content() tests ==========

TEST_F(InstallerTest, GenerateServiceContentSystem) {
    auto content = Installer::generate_service_content("/usr/local/bin/mihomo", "/etc/mihomo", ServiceScope::System);
    // ExecStart paths should be quoted
    EXPECT_NE(content.find("\"/usr/local/bin/mihomo\""), std::string::npos);
    EXPECT_NE(content.find("\"/etc/mihomo\""), std::string::npos);
    EXPECT_NE(content.find("multi-user.target"), std::string::npos);
    EXPECT_NE(content.find("[Service]"), std::string::npos);
}

TEST_F(InstallerTest, GenerateServiceContentUser) {
    auto content = Installer::generate_service_content("/home/user/.local/bin/mihomo", "/home/user/.config/clashtui-cpp/mihomo", ServiceScope::User);
    EXPECT_NE(content.find("\"/home/user/.local/bin/mihomo\""), std::string::npos);
    EXPECT_NE(content.find("\"/home/user/.config/clashtui-cpp/mihomo\""), std::string::npos);
    EXPECT_NE(content.find("default.target"), std::string::npos);
}

// ========== Phase 8: get_proxy_mirrors() tests ==========

TEST_F(InstallerTest, GetProxyMirrorsNonEmpty) {
    auto mirrors = Installer::get_proxy_mirrors();
    EXPECT_GE(mirrors.size(), 2u);  // at least direct + one mirror
    EXPECT_TRUE(mirrors[0].empty());  // first should be empty (direct)
}

TEST_F(InstallerTest, GetProxyMirrorsContainExpectedMirrors) {
    auto mirrors = Installer::get_proxy_mirrors();
    bool has_ghfast = false;
    for (const auto& m : mirrors) {
        if (m.find("ghfast") != std::string::npos) has_ghfast = true;
    }
    EXPECT_TRUE(has_ghfast);
}

// ========== Phase 8: extract_gz() test ==========

TEST_F(InstallerTest, ExtractGzInvalidFile) {
    EXPECT_FALSE(Installer::extract_gz("/nonexistent/file.gz", test_dir + "/output"));
}
