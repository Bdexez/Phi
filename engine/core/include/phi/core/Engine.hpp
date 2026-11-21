#pragma once

#include <phi/core/Config.hpp>
#include <phi/core/EventBus.hpp>
#include <phi/core/ModuleManager.hpp>
#include <phi/core/Result.hpp>
#include <phi/core/Scheduler.hpp>
#include <phi/core/ServiceRegistry.hpp>
#include <phi/core/Time.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace phi {

/// The engine kernel.
///
/// The kernel knows nothing about windows, GPUs or scenes. It owns the four
/// mechanisms modules are built from, and a loop that runs the frame phases:
///
///   - ModuleManager   : loads modules and orders their initialization
///   - ServiceRegistry : interface -> implementation lookup
///   - EventBus        : decoupled notifications
///   - Scheduler       : per-frame tasks, grouped by Phase
///
/// Everything else is contributed by modules.
///
/// Recognized configuration keys:
///   engine.fixed_timestep  seconds per FixedUpdate step (default 1/60)
///   engine.max_fixed_steps FixedUpdate steps run at most per frame; the
///                          time beyond is dropped so a slow frame cannot
///                          snowball (default 8)
///   engine.max_frames      exit after N frames, 0 = unlimited (default 0)
///   engine.forced_delta    if > 0, use this delta instead of the wall clock
///                          (deterministic runs, captures, tests)
///   engine.max_fps         if > 0, tick() waits so frames start at most this
///                          often (saves power when vsync is off; default 0)
class Engine {
public:
    explicit Engine(Config config = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] Config& config() { return config_; }
    [[nodiscard]] const Config& config() const { return config_; }
    [[nodiscard]] ServiceRegistry& services() { return services_; }
    [[nodiscard]] EventBus& events() { return events_; }
    [[nodiscard]] Scheduler& scheduler() { return scheduler_; }

    /// Records each frame, phase and task into `trace` from now on (null
    /// stops), and the JobSystem service's jobs on their own threads' rows;
    /// see TraceRecorder. The recorder must outlive the recording.
    void setTrace(TraceRecorder* trace);

    [[nodiscard]] ModuleManager& modules() { return modules_; }
    [[nodiscard]] const FrameTime& time() const { return time_; }

    /// Initializes the loaded modules in dependency order.
    Result<> initialize();

    /// Runs frames until requestExit() is called or engine.max_frames is hit.
    void run();

    /// Runs a single frame. Returns false once the engine should stop.
    bool tick();

    void requestExit() { exitRequested_ = true; }

    /// Game time runs at this multiple of real time: 0.25 for slow motion,
    /// 0 pauses (fixed steps stop, FrameTime::delta is 0); FrameTime's
    /// unscaledDelta keeps real time. Starts from engine.time_scale (1).
    void setTimeScale(f64 scale) { timeScale_ = std::isfinite(scale) ? std::clamp(scale, 0.0, 100.0) : 1.0; }
    [[nodiscard]] f64 timeScale() const { return timeScale_; }
    [[nodiscard]] bool exitRequested() const { return exitRequested_; }

    /// Shuts modules down, then releases tasks, events, services and plugin
    /// libraries, in that order. Idempotent; also called by the destructor.
    void shutdown();

private:
    using Clock = std::chrono::steady_clock;

    // modules_ is declared first so it is destroyed last: plugin libraries
    // must stay mapped while services, events and tasks (which may hold code
    // from plugins) are destroyed.
    ModuleManager modules_;
    Config config_;
    ServiceRegistry services_;
    EventBus events_;
    Scheduler scheduler_;
    TraceRecorder* trace_ = nullptr;

    FrameTime time_;
    Clock::time_point lastTick_;
    f64 fixedTimestep_ = 1.0 / 60.0;
    f64 fixedAccumulator_ = 0.0;
    f64 fixedElapsed_ = 0.0;
    f64 forcedDelta_ = 0.0;
    f64 timeScale_ = 1.0;
    u64 maxFrames_ = 0;
    u32 maxFixedSteps_ = 8;
    Clock::duration minFrameTime_{0};
    Clock::time_point nextFrame_;
    bool firstTick_ = true;
    bool exitRequested_ = false;
    bool shutDown_ = false;
};

}  // namespace phi
