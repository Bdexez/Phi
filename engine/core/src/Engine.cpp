#include <phi/core/Engine.hpp>

#include <phi/core/JobSystem.hpp>

#include <phi/core/Log.hpp>

#include <algorithm>
#include <format>
#include <thread>

namespace phi {
namespace {

// A long stall (debugger, window drag) must not explode the simulation.
constexpr f64 kMaxFrameDelta = 0.25;

}  // namespace

Engine::Engine(Config config) : config_(std::move(config)) {}

Engine::~Engine() {
    shutdown();
}

Result<> Engine::initialize() {
    fixedTimestep_ = std::max(config_.getFloat("engine.fixed_timestep", 1.0 / 60.0), 1e-4);
    forcedDelta_ = std::max(config_.getFloat("engine.forced_delta", 0.0), 0.0);
    setTimeScale(config_.getFloat("engine.time_scale", 1.0));
    maxFrames_ = static_cast<u64>(std::max<i64>(config_.getInt("engine.max_frames", 0), 0));
    maxFixedSteps_ = static_cast<u32>(std::clamp<i64>(config_.getInt("engine.max_fixed_steps", 8), 1, 1000));
    if (const f64 maxFps = config_.getFloat("engine.max_fps", 0.0); maxFps > 0.0) {
        minFrameTime_ = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<f64>(1.0 / maxFps));
    }
    return modules_.initializeAll(*this);
}

void Engine::run() {
    while (tick()) {
    }
}

bool Engine::tick() {
    if (minFrameTime_.count() > 0) {
        // Frame starts are scheduled from the previous deadline rather than
        // from "now", so the rate does not drift; a frame running late
        // resets the schedule instead of bursting to catch up.
        const Clock::time_point start = Clock::now();
        if (nextFrame_ > start) {
            std::this_thread::sleep_until(nextFrame_);
        }
        nextFrame_ = std::max(nextFrame_, start) + minFrameTime_;
    }
    const Clock::time_point now = Clock::now();
    const Clock::time_point frameStart = now;
    if (firstTick_) {
        lastTick_ = now;
        firstTick_ = false;
    }
    const f64 wallDelta = std::chrono::duration<f64>(now - lastTick_).count();
    lastTick_ = now;
    time_.unscaledDelta = forcedDelta_ > 0.0 ? forcedDelta_ : std::min(wallDelta, kMaxFrameDelta);
    time_.delta = time_.unscaledDelta * timeScale_;
    time_.elapsed += time_.delta;

    scheduler_.run(Phase::BeginFrame, time_);
    scheduler_.run(Phase::Input, time_);
    scheduler_.run(Phase::PreUpdate, time_);

    // Fixed-step simulation: consume the accumulated time in constant slices.
    // The fixed clock advances by exactly one step per run, independently of
    // the frame clock, so FixedUpdate is deterministic for a given step count.
    fixedAccumulator_ += time_.delta;
    FrameTime fixedTime = time_;
    fixedTime.delta = fixedTimestep_;
    fixedTime.fixedAlpha = 0.0;
    u32 steps = 0;
    while (fixedAccumulator_ >= fixedTimestep_ && steps < maxFixedSteps_) {
        fixedElapsed_ += fixedTimestep_;
        fixedTime.elapsed = fixedElapsed_;
        scheduler_.run(Phase::FixedUpdate, fixedTime);
        fixedAccumulator_ -= fixedTimestep_;
        ++steps;
    }
    if (fixedAccumulator_ >= fixedTimestep_) {
        fixedAccumulator_ = 0.0;  // cannot keep up: drop the backlog
    }
    time_.fixedAlpha = std::clamp(fixedAccumulator_ / fixedTimestep_, 0.0, 1.0);

    scheduler_.run(Phase::Update, time_);
    scheduler_.run(Phase::PostUpdate, time_);
    events_.dispatchQueued();
    scheduler_.run(Phase::PreRender, time_);
    scheduler_.run(Phase::Render, time_);
    scheduler_.run(Phase::PostRender, time_);
    scheduler_.run(Phase::EndFrame, time_);

    if (trace_ != nullptr) {
        trace_->record(std::format("frame {}", time_.frame), "frame", frameStart, Clock::now());
    }
    ++time_.frame;
    if (maxFrames_ != 0 && time_.frame >= maxFrames_) {
        exitRequested_ = true;
    }
    return !exitRequested_;
}

void Engine::setTrace(TraceRecorder* trace) {
    trace_ = trace;
    scheduler_.setTrace(trace);
    if (auto* jobs = services_.tryGet<JobSystem>()) {
        jobs->setTrace(trace);  // their work too, on their own rows
    }
}

void Engine::shutdown() {
    if (shutDown_) {
        return;
    }
    shutDown_ = true;

    modules_.shutdownAll(*this);
    // Anything a plugin left behind is destroyed while its code is still
    // mapped; only then are the libraries released.
    scheduler_.clear();
    events_.clear();
    services_.clear();
    modules_.unloadLibraries();
}

}  // namespace phi
