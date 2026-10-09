#pragma once

#include <bdex/core/Types.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace bdex {

/// Notices files changing on disk, for hot reloading: edit a model or
/// rebuild a shader, and the running program picks it up. Polls
/// modification times when asked, which works everywhere and costs a few
/// file system queries per poll; watch what matters, and poll a few times
/// a second rather than every frame.
class FileWatcher {
public:
    /// Watches a file, or every file under a directory (recursively).
    /// What exists now is the baseline: only later changes are reported.
    void watch(const std::filesystem::path& path);
    void unwatch(const std::filesystem::path& path);

    /// Files modified, created or removed since the last poll (or since
    /// they were first watched), in path order.
    [[nodiscard]] std::vector<std::filesystem::path> poll();

private:
    using Stamp = std::optional<std::filesystem::file_time_type>;  ///< None: missing.
    /// Current stamps of every file the watched path covers.
    [[nodiscard]] static std::map<std::filesystem::path, Stamp> scan(const std::filesystem::path& path);

    std::map<std::filesystem::path, std::map<std::filesystem::path, Stamp>> watched_;
};

}  // namespace bdex
