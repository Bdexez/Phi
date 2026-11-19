#include <phi/core/Trace.hpp>

#include <phi/core/FileSystem.hpp>

#include <charconv>
#include <cmath>
#include <format>

namespace phi {

namespace {

void appendJsonString(std::string& out, std::string_view text) {
    out += '"';
    usize chunkStart = 0;
    for (usize i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c == '\\' || c < 0x20) {
            if (i > chunkStart) {
                out.append(text.data() + chunkStart, i - chunkStart);
            }
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += std::format("\\u{:04x}", static_cast<unsigned int>(c)); break;
            }
            chunkStart = i + 1;
        }
    }
    if (text.size() > chunkStart) {
        out.append(text.data() + chunkStart, text.size() - chunkStart);
    }
    out += '"';
}

void appendJsonDouble(std::string& out, f64 value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    char buffer[32];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec == std::errc{}) {
        out.append(buffer, static_cast<usize>(end - buffer));
    } else {
        out += "0";
    }
}

void appendJsonU32(std::string& out, u32 value) {
    char buffer[16];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec == std::errc{}) {
        out.append(buffer, static_cast<usize>(end - buffer));
    }
}

}  // namespace

void TraceRecorder::record(std::string_view name, std::string_view category, Clock::time_point start,
                           Clock::time_point end, u32 thread) {
    const std::lock_guard lock(mutex_);
    if (events_.size() >= maxEvents_) {
        truncated_ = true;
        return;
    }
    if (!started_) {
        origin_ = start;
        started_ = true;
    }
    const auto micros = [](Clock::duration duration) {
        return std::chrono::duration<f64, std::micro>(duration).count();
    };
    events_.push_back({std::string(name), std::string(category), micros(start - origin_), micros(end - start), thread});
}

void TraceRecorder::nameThread(u32 thread, std::string_view name) {
    const std::lock_guard lock(mutex_);
    threadNames_[thread] = std::string(name);
}

std::string TraceRecorder::toChromeJson() const {
    const std::lock_guard lock(mutex_);
    std::string out;
    out.reserve(64 + threadNames_.size() * 80 + events_.size() * 112);
    out += "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
    bool first = true;
    for (const auto& [thread, name] : threadNames_) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += "{\"args\":{\"name\":";
        appendJsonString(out, name);
        out += "},\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":";
        appendJsonU32(out, thread);
        out += '}';
    }
    for (const TraceEvent& event : events_) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += "{\"cat\":";
        appendJsonString(out, event.category);
        out += ",\"dur\":";
        appendJsonDouble(out, event.durationMicroseconds);
        out += ",\"name\":";
        appendJsonString(out, event.name);
        out += ",\"ph\":\"X\",\"pid\":1,\"tid\":";
        appendJsonU32(out, event.thread);
        out += ",\"ts\":";
        appendJsonDouble(out, event.startMicroseconds);
        out += '}';
    }
    out += "]}";
    return out;
}

Result<> TraceRecorder::writeFile(const std::filesystem::path& path) const {
    const std::string text = toChromeJson();
    return fs::writeBinaryFile(path, text.data(), text.size());
}

}  // namespace phi
