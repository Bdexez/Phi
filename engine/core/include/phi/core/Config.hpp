#pragma once

#include <phi/core/Result.hpp>
#include <phi/core/Types.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace phi {

/// Flat key/value configuration, addressed as "section.key".
///
/// Loaded from INI-style text:
///
///   # comment
///   [window]
///   title = My Game
///   width = 1280
///
/// Modules read their own section; the engine does not interpret it, so a new
/// module can introduce settings without touching any shared code.
class Config {
public:
    [[nodiscard]] static Result<Config> parse(std::string_view text);
    [[nodiscard]] static Result<Config> loadFile(const std::filesystem::path& path);

    /// INI text that parse() reads back to the same entries: keys grouped in
    /// sections by their first component ("window.width" goes to [window]
    /// as "width"), keys without a dot first. Values must not start or end
    /// with whitespace or contain line breaks, which INI cannot represent.
    [[nodiscard]] std::string toText() const;
    /// Writes toText() to `path` (settings menus, rebinding...).
    [[nodiscard]] Result<> saveFile(const std::filesystem::path& path) const;

    void set(std::string_view key, std::string_view value);
    /// Sets the value only if the key is absent (used for defaults).
    void setDefault(std::string_view key, std::string_view value);
    /// Copies every entry of `other` over this config.
    void merge(const Config& other);

    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] std::optional<std::string> find(std::string_view key) const;

    // Typed getters return `fallback` when the key is missing, and also when
    // its value cannot be parsed, in which case a warning is logged.

    [[nodiscard]] std::string getString(std::string_view key, std::string_view fallback = "") const;
    [[nodiscard]] i64 getInt(std::string_view key, i64 fallback = 0) const;
    [[nodiscard]] f64 getFloat(std::string_view key, f64 fallback = 0.0) const;
    /// true/false, yes/no, on/off or 1/0, in any letter case.
    [[nodiscard]] bool getBool(std::string_view key, bool fallback = false) const;
    /// Comma-separated list, entries trimmed, empty entries dropped.
    [[nodiscard]] std::vector<std::string> getList(std::string_view key) const;

    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& entries() const {
        return values_;
    }

private:
    std::map<std::string, std::string, std::less<>> values_;
};

}  // namespace phi
