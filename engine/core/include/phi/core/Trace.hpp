#pragma once

#include <phi/core/Result.hpp>
#include <phi/core/Types.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace phi {

/// One timed span: a frame, a phase, a task.
struct TraceEvent {
    std::string name;
    std::string category;
    f64 startMicroseconds = 0.0;  ///< Since the recorder's first event.
    f64 durationMicroseconds = 0.0;
    u32 thread = 0;
};

/// Collects timed spans and writes them in the Chrome trace event format,
/// which Perfetto (ui.perfetto.dev) and chrome://tracing display as a
/// timeline: spans of one thread nest by time, so frames contain their
/// phases, which contain their tasks. Where averages hide a slow frame
/// among a hundred, a trace shows which one and why.
///
/// Recording stops at `maxEvents` (memory stays bounded on long runs).
/// Any thread may record; give each its own `thread` number (0 is the
/// main thread) and name it with nameThread, so the timeline shows one row
/// per thread. Read events() once recording has stopped.
class TraceRecorder {
public:
    using Clock = std::chrono::steady_clock;

    explicit TraceRecorder(usize maxEvents = 2'000'000) : maxEvents_(maxEvents) {}

    void record(std::string_view name, std::string_view category, Clock::time_point start, Clock::time_point end,
                u32 thread = 0);

    /// Labels a thread's row in the timeline ("main", "worker 2").
    void nameThread(u32 thread, std::string_view name);

    [[nodiscard]] const std::vector<TraceEvent>& events() const { return events_; }
    /// Whether events were dropped for reaching `maxEvents`.
    [[nodiscard]] bool truncated() const { return truncated_; }

    /// {"traceEvents": [{"name", "cat", "ph": "X", "ts", "dur", "pid", "tid"}...]}
    [[nodiscard]] std::string toChromeJson() const;
    Result<> writeFile(const std::filesystem::path& path) const;

private:
    usize maxEvents_;
    mutable std::mutex mutex_;
    std::vector<TraceEvent> events_;
    std::map<u32, std::string> threadNames_;
    Clock::time_point origin_;
    bool started_ = false;
    bool truncated_ = false;
};

}  // namespace phi
