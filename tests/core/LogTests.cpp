#include <phi/core/FileSystem.hpp>
#include <phi/core/Log.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace phi;

TEST(Log, FileSinksKeepEveryEnabledRecord) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "phi_tests_log" / "engine.log";
    std::filesystem::remove_all(path.parent_path());

    const log::Level previous = log::level();
    log::setLevel(log::Level::Info);
    log::setConsoleEnabled(false);
    const auto sink = log::addFileSink(path);  // creates the directory
    ASSERT_TRUE(sink.has_value()) << sink.error().message;
    log::info("test", "hello {}", 42);
    log::debug("test", "below the level: dropped");
    log::warn("other", "careful");

    // Flushed as written: readable before the sink closes.
    const auto open = fs::readTextFile(path);
    ASSERT_TRUE(open.has_value());
    EXPECT_NE(open->find("info  test: hello 42\n"), std::string::npos);
    EXPECT_NE(open->find("warn  other: careful\n"), std::string::npos);
    EXPECT_EQ(open->find("dropped"), std::string::npos);

    log::removeSink(*sink);
    log::info("test", "after removal");
    log::setConsoleEnabled(true);
    log::setLevel(previous);
    EXPECT_EQ(fs::readTextFile(path)->find("after removal"), std::string::npos);
    std::filesystem::remove_all(path.parent_path());

    EXPECT_FALSE(log::addFileSink("/proc/nope/engine.log").has_value());
}
