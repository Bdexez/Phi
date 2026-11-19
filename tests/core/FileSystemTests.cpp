#include <bdex/core/FileSystem.hpp>

#include <gtest/gtest.h>

using namespace bdex;

TEST(VirtualFileSystem, ResolvesMountedPaths) {
    VirtualFileSystem vfs;
    vfs.mount("app", "/data/game");
    EXPECT_TRUE(vfs.isMounted("app"));
    EXPECT_FALSE(vfs.isMounted("engine"));

    const auto path = vfs.resolve("app:/textures/crate.tga");
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, std::filesystem::path("/data/game/textures/crate.tga"));

    // ".." that stays inside the mount is fine.
    const auto normalized = vfs.resolve("app:/textures/../meshes/./cube.obj");
    ASSERT_TRUE(normalized.has_value());
    EXPECT_EQ(*normalized, std::filesystem::path("/data/game/meshes/cube.obj"));

    // Plain paths are used as-is.
    EXPECT_EQ(*vfs.resolve("relative/file.txt"), std::filesystem::path("relative/file.txt"));
}

TEST(VirtualFileSystem, RejectsUnknownMountsAndEscapes) {
    VirtualFileSystem vfs;
    vfs.mount("app", "/data/game");

    EXPECT_FALSE(vfs.resolve("mods:/a.txt").has_value());
    EXPECT_FALSE(vfs.resolve("app:/../secret.txt").has_value());
    EXPECT_FALSE(vfs.resolve("app:/textures/../../secret.txt").has_value());
    EXPECT_FALSE(vfs.resolve("app://etc/passwd").has_value());  // absolute after the root
    EXPECT_FALSE(vfs.readBinary("app:/../secret.txt").has_value());
}
