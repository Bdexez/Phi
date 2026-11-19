#include <bdex/core/Config.hpp>
#include <bdex/core/Log.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace bdex;

TEST(Config, ParsesSectionsAndTypes) {
    const auto config = Config::parse(R"(
# top-level comment
name = demo

[window]
title = My Game
width = 1280
scale = 1.5
fullscreen = yes
; another comment
[engine]
modules = window_glfw, rhi_vulkan ,, renderer_forward
)");
    ASSERT_TRUE(config.has_value()) << config.error().message;

    EXPECT_EQ(config->getString("name"), "demo");
    EXPECT_EQ(config->getString("window.title"), "My Game");
    EXPECT_EQ(config->getInt("window.width"), 1280);
    EXPECT_DOUBLE_EQ(config->getFloat("window.scale"), 1.5);
    EXPECT_TRUE(config->getBool("window.fullscreen"));
    EXPECT_EQ(config->getList("engine.modules"),
              (std::vector<std::string>{"window_glfw", "rhi_vulkan", "renderer_forward"}));
}

TEST(Config, FallbacksApplyToMissingOrMalformedValues) {
    std::vector<std::string> warnings;
    const u32 sink = log::addSink([&](const log::Record& record) {
        if (record.level == log::Level::Warn) {
            warnings.emplace_back(record.message);
        }
    });

    Config config;
    config.set("a.number", "12abc");
    config.set("a.flag", "ture");
    EXPECT_EQ(config.getInt("a.missing", 9), 9);
    EXPECT_EQ(config.getString("a.missing", "x"), "x");
    EXPECT_FALSE(config.getBool("a.missing"));
    EXPECT_TRUE(config.getList("a.missing").empty());
    EXPECT_TRUE(warnings.empty());  // missing keys are not an error

    // Malformed values fall back too, but say so.
    EXPECT_EQ(config.getInt("a.number", 7), 7);
    EXPECT_DOUBLE_EQ(config.getFloat("a.number", 2.5), 2.5);
    EXPECT_TRUE(config.getBool("a.flag", true));
    log::removeSink(sink);
    ASSERT_EQ(warnings.size(), 3u);
    EXPECT_NE(warnings[0].find("a.number"), std::string::npos);
    EXPECT_NE(warnings[2].find("ture"), std::string::npos);
}

TEST(Config, BooleansIgnoreLetterCase) {
    Config config;
    config.set("a", "True");
    config.set("b", "OFF");
    config.set("c", "Yes");
    EXPECT_TRUE(config.getBool("a"));
    EXPECT_FALSE(config.getBool("b", true));
    EXPECT_TRUE(config.getBool("c"));
}

TEST(Config, DefaultsMergeAndOverrides) {
    Config config;
    config.set("window.width", "800");
    config.setDefault("window.width", "1");
    config.setDefault("window.height", "600");
    EXPECT_EQ(config.getInt("window.width"), 800);
    EXPECT_EQ(config.getInt("window.height"), 600);

    Config overrides;
    overrides.set("window.height", "720");
    config.merge(overrides);
    EXPECT_EQ(config.getInt("window.height"), 720);
}

TEST(Config, ReportsSyntaxErrorsWithLineNumbers) {
    const auto missingEquals = Config::parse("[a]\nnot a pair\n");
    ASSERT_FALSE(missingEquals.has_value());
    EXPECT_NE(missingEquals.error().message.find("line 2"), std::string::npos);

    EXPECT_FALSE(Config::parse("[broken\n").has_value());
    EXPECT_FALSE(Config::parse("= value\n").has_value());
}

TEST(Config, WritesTextThatReadsBack) {
    Config config;
    config.set("name", "demo");
    config.set("window.width", "1280");
    config.set("window.title", "My Game = fun # really");
    config.set("input.move_forward", "Z, Up");
    config.set("entity.0.Transform.position", "0 1 0");  // several dots
    config.set("empty.value", "");

    const std::string text = config.toText();
    EXPECT_TRUE(text.starts_with("name = demo\n"));
    EXPECT_NE(text.find("[window]\n"), std::string::npos);
    const auto reread = Config::parse(text);
    ASSERT_TRUE(reread.has_value()) << reread.error().message << "\n" << text;
    EXPECT_EQ(reread->entries(), config.entries());

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "bdex_tests_config.ini";
    ASSERT_TRUE(config.saveFile(path).has_value());
    const auto loaded = Config::loadFile(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->getString("window.title"), "My Game = fun # really");
}
