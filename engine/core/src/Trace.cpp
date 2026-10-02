#include <bdex/core/Trace.hpp>

#include <bdex/core/FileSystem.hpp>
#include <bdex/core/Json.hpp>

namespace bdex {

void TraceRecorder::record(std::string_view name, std::string_view category, Clock::time_point start,
                           Clock::time_point end, u32 thread) {
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

std::string TraceRecorder::toChromeJson() const {
    json::Array spans;
    spans.reserve(events_.size());
    for (const TraceEvent& event : events_) {
        spans.emplace_back(json::Object{
            {"name", event.name},
            {"cat", event.category},
            {"ph", "X"},  // a complete event: start and duration
            {"ts", event.startMicroseconds},
            {"dur", event.durationMicroseconds},
            {"pid", 1},
            {"tid", static_cast<f64>(event.thread)},
        });
    }
    return json::write(json::Object{{"traceEvents", std::move(spans)}, {"displayTimeUnit", "ms"}});
}

Result<> TraceRecorder::writeFile(const std::filesystem::path& path) const {
    const std::string text = toChromeJson();
    return fs::writeBinaryFile(path, text.data(), text.size());
}

}  // namespace bdex
