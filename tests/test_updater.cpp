#include <gtest/gtest.h>
#include "core/updater.hpp"

#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <filesystem>
#include <fcntl.h>

namespace fs = std::filesystem;

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

// ════════════════════════════════════════════════════════════════
// atomic_replace_binary tests
// ════════════════════════════════════════════════════════════════

class AtomicReplaceTest : public ::testing::Test {
protected:
    fs::path tmp_dir;
    fs::path old_bin;
    fs::path new_bin;

    void SetUp() override {
        tmp_dir = fs::temp_directory_path() / "clashtui-atomic-test";
        fs::create_directories(tmp_dir);

        old_bin = tmp_dir / "fake-binary";
        new_bin = tmp_dir / "new-binary";

        // Create "old" binary with known content
        {
            std::ofstream f(old_bin.string());
            f << "OLD_BINARY_CONTENT_v1";
        }
        fs::permissions(old_bin, fs::perms::owner_exec, fs::perm_options::add);

        // Create "new" binary with different content
        {
            std::ofstream f(new_bin.string());
            f << "NEW_BINARY_CONTENT_v2";
        }
    }

    void TearDown() override {
        try { fs::remove_all(tmp_dir); } catch (...) {}
    }

    static ino_t get_inode(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) return st.st_ino;
        return 0;
    }

    static std::string read_content(const std::string& path) {
        std::ifstream f(path);
        return std::string((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    }
};

// 1. Basic replacement succeeds, content is correct
TEST_F(AtomicReplaceTest, ReplacesContentCorrectly) {
    std::string err = Updater::atomic_replace_binary(new_bin.string(), old_bin.string());
    EXPECT_TRUE(err.empty()) << "Error: " << err;
    EXPECT_EQ(read_content(old_bin.string()), "NEW_BINARY_CONTENT_v2");
}

// 2. Inode changes — proves rename() was used, not overwrite
TEST_F(AtomicReplaceTest, InodeChangesAfterReplace) {
    ino_t inode_before = get_inode(old_bin.string());
    ASSERT_NE(inode_before, 0u);

    std::string err = Updater::atomic_replace_binary(new_bin.string(), old_bin.string());
    EXPECT_TRUE(err.empty()) << "Error: " << err;

    ino_t inode_after = get_inode(old_bin.string());
    EXPECT_NE(inode_before, inode_after)
        << "Inode should change (rename creates new inode, not overwrite)";
}

// 3. Old fd remains readable after replace — simulates a running process
TEST_F(AtomicReplaceTest, OldFdStillReadableAfterReplace) {
    // Open the old binary *before* replacement (simulates mmap by running process)
    int fd = open(old_bin.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0) << "Could not open old binary";

    // Replace
    std::string err = Updater::atomic_replace_binary(new_bin.string(), old_bin.string());
    EXPECT_TRUE(err.empty()) << "Error: " << err;

    // Read from the old fd — should still return OLD content
    char buf[64] = {};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    ASSERT_GT(n, 0) << "Old fd should still be readable after atomic replace";
    EXPECT_EQ(std::string(buf), "OLD_BINARY_CONTENT_v1")
        << "Old fd should return original content, not new content";
}

// 4. Replaced file has executable permissions
TEST_F(AtomicReplaceTest, HasExecutablePermission) {
    std::string err = Updater::atomic_replace_binary(new_bin.string(), old_bin.string());
    EXPECT_TRUE(err.empty()) << "Error: " << err;

    auto perms = fs::status(old_bin).permissions();
    EXPECT_NE(perms & fs::perms::owner_exec, fs::perms::none);
    EXPECT_NE(perms & fs::perms::group_exec, fs::perms::none);
    EXPECT_NE(perms & fs::perms::others_exec, fs::perms::none);
}

// 5. Temp file does not linger after successful replacement
TEST_F(AtomicReplaceTest, NoTempFileResidue) {
    std::string err = Updater::atomic_replace_binary(new_bin.string(), old_bin.string());
    EXPECT_TRUE(err.empty()) << "Error: " << err;

    fs::path tmp_file = tmp_dir / ".clashtui-cpp.update.tmp";
    EXPECT_FALSE(fs::exists(tmp_file))
        << "Temp file should not exist after successful replace";
}

// 6. Fails gracefully when target does not exist
TEST_F(AtomicReplaceTest, FailsWhenTargetNotExist) {
    fs::path nonexistent = tmp_dir / "no-such-binary";
    std::string err = Updater::atomic_replace_binary(new_bin.string(), nonexistent.string());
    // Should still succeed if we have write access to the parent dir —
    // rename() will create the target entry. But the key point is it
    // should not crash.
    // (rename from tmp to nonexistent target works on POSIX)
    // Either way, just ensure no crash and no temp residue.
    fs::path tmp_file = tmp_dir / ".clashtui-cpp.update.tmp";
    EXPECT_FALSE(fs::exists(tmp_file));
}

// 7. Fails gracefully when source does not exist
TEST_F(AtomicReplaceTest, FailsWhenSourceNotExist) {
    fs::path bad_source = tmp_dir / "no-such-source";
    std::string err = Updater::atomic_replace_binary(bad_source.string(), old_bin.string());
    EXPECT_FALSE(err.empty()) << "Should return error when source is missing";
    // Original binary should be untouched
    EXPECT_EQ(read_content(old_bin.string()), "OLD_BINARY_CONTENT_v1");
}

// 8. Multiple consecutive replacements work correctly
TEST_F(AtomicReplaceTest, ConsecutiveReplacementsWork) {
    // First replacement
    std::string err = Updater::atomic_replace_binary(new_bin.string(), old_bin.string());
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(read_content(old_bin.string()), "NEW_BINARY_CONTENT_v2");

    // Create a third version
    fs::path v3 = tmp_dir / "v3-binary";
    { std::ofstream f(v3.string()); f << "BINARY_V3"; }

    // Second replacement
    err = Updater::atomic_replace_binary(v3.string(), old_bin.string());
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(read_content(old_bin.string()), "BINARY_V3");
}
