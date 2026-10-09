#include <bdex/core/FileWatcher.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace bdex;

namespace {

void write(const std::filesystem::path& path, const char* text) {
    std::ofstream(path) << text;
}

/// Moves a file's modification time forward, as a later save would, without
/// waiting for the file system's clock resolution.
void touch(const std::filesystem::path& path, int seconds) {
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(seconds));
}

}  // namespace

TEST(FileWatcher, ReportsFilesModifiedCreatedAndRemoved) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "bdex_tests_watcher";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "shaders");
    write(root / "model.gltf", "{}");
    write(root / "shaders" / "a.spv", "a");
    write(root / "shaders" / "b.spv", "b");

    FileWatcher watcher;
    watcher.watch(root / "model.gltf");
    watcher.watch(root / "shaders");
    watcher.watch(root / "later.txt");  // missing for now
    EXPECT_TRUE(watcher.poll().empty());  // what existed is the baseline

    touch(root / "model.gltf", 5);
    touch(root / "shaders" / "b.spv", 5);
    EXPECT_EQ(watcher.poll(), (std::vector<std::filesystem::path>{root / "model.gltf", root / "shaders" / "b.spv"}));
    EXPECT_TRUE(watcher.poll().empty());  // reported once

    write(root / "shaders" / "c.spv", "c");
    std::filesystem::remove(root / "shaders" / "a.spv");
    write(root / "later.txt", "here now");
    EXPECT_EQ(watcher.poll(), (std::vector<std::filesystem::path>{root / "later.txt", root / "shaders" / "a.spv",
                                                                  root / "shaders" / "c.spv"}));

    std::filesystem::remove(root / "model.gltf");
    EXPECT_EQ(watcher.poll(), (std::vector<std::filesystem::path>{root / "model.gltf"}));
    watcher.unwatch(root / "shaders");
    touch(root / "shaders" / "b.spv", 10);
    EXPECT_TRUE(watcher.poll().empty());
    std::filesystem::remove_all(root);
}
