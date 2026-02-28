#include <gtest/gtest.h>
#include "core/updater.hpp"

TEST(UpdaterTest, CurrentVersionNotEmpty) {
    EXPECT_FALSE(Updater::current_version().empty());
}

TEST(UpdaterTest, CurrentVersionFormat) {
    auto v = Updater::current_version();
    int major = -1, minor = -1, patch = -1;
    int parsed = sscanf(v.c_str(), "%d.%d.%d", &major, &minor, &patch);
    EXPECT_EQ(parsed, 3);
    EXPECT_GE(major, 0);
    EXPECT_GE(minor, 0);
    EXPECT_GE(patch, 0);
}

TEST(UpdaterTest, CheckForUpdateInvalidRepo) {
    Updater updater("nonexistent/nonexistent-repo-xyz-12345");
    auto info = updater.check_for_update();
    EXPECT_FALSE(info.available);
    EXPECT_FALSE(info.current_version.empty());
    EXPECT_TRUE(info.download_url.empty());
}

TEST(UpdaterTest, ConstructionDefault) {
    Updater updater;
    // Should not crash
    EXPECT_FALSE(Updater::current_version().empty());
}

TEST(UpdaterTest, ConstructionCustomRepo) {
    Updater updater("some-owner/some-repo");
    // Should not crash
    EXPECT_FALSE(Updater::current_version().empty());
}
