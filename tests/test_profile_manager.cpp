#include <gtest/gtest.h>
#include "core/profile_manager.hpp"
#include "core/config.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;

class ProfileManagerTest : public ::testing::Test {
protected:
    std::string original_home_;
    std::string temp_dir_;

    void SetUp() override {
        // Save original HOME
        const char* home = std::getenv("HOME");
        if (home) original_home_ = home;

        // Create temp directory for test
        temp_dir_ = fs::temp_directory_path().string() + "/clashtui_pm_test_" +
                     std::to_string(std::time(nullptr));
        fs::create_directories(temp_dir_);
        setenv("HOME", temp_dir_.c_str(), 1);
    }

    void TearDown() override {
        // Restore HOME
        if (!original_home_.empty()) {
            setenv("HOME", original_home_.c_str(), 1);
        } else {
            unsetenv("HOME");
        }
        // Clean up temp dir
        try {
            fs::remove_all(temp_dir_);
        } catch (...) {}
    }
};

TEST_F(ProfileManagerTest, ProfilesDir) {
    if (geteuid() == 0) GTEST_SKIP() << "Skipped: runs as root, config_dir ignores HOME";
    Config config;
    ProfileManager pm(config);
    std::string expected = temp_dir_ + "/.config/clashtui-cpp/profiles";
    EXPECT_EQ(pm.profiles_dir(), expected);
}

TEST_F(ProfileManagerTest, EmptyListInitially) {
    Config config;
    ProfileManager pm(config);
    auto profiles = pm.list_profiles();
    EXPECT_TRUE(profiles.empty());
}

TEST_F(ProfileManagerTest, ActiveProfilePathEmptyByDefault) {
    Config config;
    ProfileManager pm(config);
    EXPECT_TRUE(pm.active_profile_path().empty());
    EXPECT_TRUE(pm.active_profile_name().empty());
}

TEST_F(ProfileManagerTest, AddProfileEmptyName) {
    Config config;
    ProfileManager pm(config);
    auto result = pm.add_profile("", "http://example.com/sub");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(ProfileManagerTest, AddProfileEmptyUrl) {
    Config config;
    ProfileManager pm(config);
    auto result = pm.add_profile("test", "");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(ProfileManagerTest, DeleteNonexistentProfile) {
    Config config;
    ProfileManager pm(config);
    EXPECT_FALSE(pm.delete_profile("nonexistent"));
}

TEST_F(ProfileManagerTest, SwitchToNonexistentProfile) {
    Config config;
    ProfileManager pm(config);
    EXPECT_FALSE(pm.switch_active("nonexistent"));
}

TEST_F(ProfileManagerTest, ProfilesDueForUpdateEmpty) {
    Config config;
    ProfileManager pm(config);
    auto due = pm.profiles_due_for_update();
    EXPECT_TRUE(due.empty());
}

TEST_F(ProfileManagerTest, UpdateNonexistentProfile) {
    Config config;
    ProfileManager pm(config);
    auto result = pm.update_profile("nonexistent");
    EXPECT_FALSE(result.success);
}

// Test ProfileInfo defaults
TEST(ProfileInfoTest, Defaults) {
    ProfileInfo info;
    EXPECT_TRUE(info.name.empty());
    EXPECT_TRUE(info.filename.empty());
    EXPECT_TRUE(info.source_url.empty());
    EXPECT_TRUE(info.last_updated.empty());
    EXPECT_TRUE(info.auto_update);
    EXPECT_EQ(info.update_interval_hours, 24);
    EXPECT_FALSE(info.is_active);
}
