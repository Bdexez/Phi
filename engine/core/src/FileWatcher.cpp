#include <bdex/core/FileWatcher.hpp>

#include <algorithm>
#include <system_error>

namespace bdex {

std::map<std::filesystem::path, FileWatcher::Stamp> FileWatcher::scan(const std::filesystem::path& path) {
    std::map<std::filesystem::path, Stamp> stamps;
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        for (auto it = std::filesystem::recursive_directory_iterator(path, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            std::error_code fileError;
            if (it->is_regular_file(fileError)) {
                const auto time = it->last_write_time(fileError);
                stamps[it->path()] = fileError ? Stamp{} : Stamp{time};
            }
        }
        return stamps;
    }
    // A single file, possibly missing for now: it is reported once it
    // appears.
    const auto time = std::filesystem::last_write_time(path, error);
    stamps[path] = error ? Stamp{} : Stamp{time};
    return stamps;
}

void FileWatcher::watch(const std::filesystem::path& path) {
    watched_[path] = scan(path);
}

void FileWatcher::unwatch(const std::filesystem::path& path) {
    watched_.erase(path);
}

std::vector<std::filesystem::path> FileWatcher::poll() {
    std::vector<std::filesystem::path> changed;
    for (auto& [root, known] : watched_) {
        std::map<std::filesystem::path, Stamp> now = scan(root);
        for (const auto& [file, stamp] : now) {
            const auto before = known.find(file);
            if (before == known.end() ? stamp.has_value() : before->second != stamp) {
                changed.push_back(file);  // created or modified (or a watched file removed)
            }
        }
        for (const auto& [file, stamp] : known) {
            if (!now.contains(file)) {
                changed.push_back(file);  // removed from a watched directory
            }
        }
        known = std::move(now);
    }
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    return changed;
}

}  // namespace bdex
