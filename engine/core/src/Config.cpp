#include <bdex/core/Config.hpp>

#include <bdex/core/FileSystem.hpp>
#include <bdex/core/Log.hpp>

#include <algorithm>
#include <charconv>

namespace bdex {
namespace {

std::string_view trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const usize begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    const usize end = text.find_last_not_of(whitespace);
    return text.substr(begin, end - begin + 1);
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) { return lower(x) == lower(y); });
}

/// A present but unusable value is most likely a typo: say so instead of
/// silently running with the default.
template <class T>
void warnMalformed(std::string_view key, std::string_view value, std::string_view expected,
                   const T& fallback) {
    log::warn("config", "'{}' = '{}' is not {}; using {}", key, value, expected, fallback);
}

}  // namespace

Result<Config> Config::parse(std::string_view text) {
    Config config;
    std::string section;
    usize lineNumber = 0;

    while (!text.empty()) {
        ++lineNumber;
        const usize newline = text.find('\n');
        std::string_view line = trim(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                return fail("config line {}: missing ']' in section header", lineNumber);
            }
            section = std::string(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const usize equals = line.find('=');
        if (equals == std::string_view::npos) {
            return fail("config line {}: expected 'key = value'", lineNumber);
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        if (key.empty()) {
            return fail("config line {}: empty key", lineNumber);
        }
        config.set(section.empty() ? std::string(key) : section + "." + std::string(key), value);
    }
    return config;
}

Result<Config> Config::loadFile(const std::filesystem::path& path) {
    auto text = fs::readTextFile(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return parse(*text);
}

std::string Config::toText() const {
    std::string text;
    // Top-level keys first: after a section header they would belong to it.
    for (const auto& [key, value] : values_) {
        if (key.find('.') == std::string::npos) {
            text += key + " = " + value + "\n";
        }
    }
    std::string_view section;
    for (const auto& [key, value] : values_) {
        const usize dot = key.find('.');
        if (dot == std::string::npos) {
            continue;
        }
        const std::string_view keySection = std::string_view(key).substr(0, dot);
        if (keySection != section) {
            section = keySection;
            text += (text.empty() ? "[" : "\n[") + std::string(section) + "]\n";
        }
        text += key.substr(dot + 1) + " = " + value + "\n";
    }
    return text;
}

Result<> Config::saveFile(const std::filesystem::path& path) const {
    const std::string text = toText();
    return fs::writeBinaryFile(path, text.data(), text.size());
}

void Config::set(std::string_view key, std::string_view value) {
    values_.insert_or_assign(std::string(key), std::string(value));
}

void Config::setDefault(std::string_view key, std::string_view value) {
    values_.try_emplace(std::string(key), std::string(value));
}

void Config::merge(const Config& other) {
    for (const auto& [key, value] : other.values_) {
        values_.insert_or_assign(key, value);
    }
}

bool Config::has(std::string_view key) const {
    return values_.contains(key);
}

std::optional<std::string> Config::find(std::string_view key) const {
    const auto it = values_.find(key);
    return it == values_.end() ? std::nullopt : std::optional(it->second);
}

std::string Config::getString(std::string_view key, std::string_view fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? std::string(fallback) : it->second;
}

i64 Config::getInt(std::string_view key, i64 fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    i64 value = 0;
    const char* end = it->second.data() + it->second.size();
    const auto [ptr, ec] = std::from_chars(it->second.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        warnMalformed(key, it->second, "an integer", fallback);
        return fallback;
    }
    return value;
}

f64 Config::getFloat(std::string_view key, f64 fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    f64 value = 0.0;
    const char* end = it->second.data() + it->second.size();
    const auto [ptr, ec] = std::from_chars(it->second.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        warnMalformed(key, it->second, "a number", fallback);
        return fallback;
    }
    return value;
}

bool Config::getBool(std::string_view key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    const std::string& value = it->second;
    for (const std::string_view yes : {"true", "1", "yes", "on"}) {
        if (equalsIgnoringCase(value, yes)) {
            return true;
        }
    }
    for (const std::string_view no : {"false", "0", "no", "off"}) {
        if (equalsIgnoringCase(value, no)) {
            return false;
        }
    }
    warnMalformed(key, value, "a boolean (true/false, yes/no, on/off, 1/0)", fallback);
    return fallback;
}

std::vector<std::string> Config::getList(std::string_view key) const {
    std::vector<std::string> result;
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return result;
    }
    std::string_view rest = it->second;
    while (!rest.empty()) {
        const usize comma = rest.find(',');
        const std::string_view item = trim(rest.substr(0, comma));
        if (!item.empty()) {
            result.emplace_back(item);
        }
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
    }
    return result;
}

}  // namespace bdex
