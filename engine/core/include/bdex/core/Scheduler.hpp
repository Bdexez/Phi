#pragma once

#include <bdex/core/Time.hpp>
#include <bdex/core/Types.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bdex {

/// Ordered stages of a frame. The engine loop itself does nothing but run
/// these phases; all behaviour (input polling, gameplay, rendering...) is
/// contributed by modules and applications as tasks.
enum class Phase : u8 {
    BeginFrame,
    Input,        ///< Poll OS events, refresh input state.
    PreUpdate,
    FixedUpdate,  ///< Runs 0..n times per frame with a constant delta.
    Update,       ///< Gameplay.
    PostUpdate,   ///< Derived data (world transforms, culling...).
    PreRender,
    Render,
    PostRender,
    EndFrame,
    Count,
};

[[nodiscard]] std::string_view phaseName(Phase phase);

using TaskId = u64;

/// CPU time spent in a task, for profiling.
struct TaskStats {
    std::string name;
    Phase phase = Phase::Update;
    f64 lastMs = 0.0;     ///< Duration of the latest run.
    f64 averageMs = 0.0;  ///< Mean over every run since the last reset.
    u64 runs = 0;         ///< FixedUpdate tasks may run several times a frame.
};

/// Registry of per-frame tasks grouped by phase.
/// Within a phase, tasks run by ascending `order`, then by registration order.
/// Each run is timed (two clock reads), see taskStats().
/// Main-thread only.
class Scheduler {
public:
    using Task = std::function<void(const FrameTime&)>;

    /// Called from a running task, the new task is put in place once the
    /// running phase ends: it never runs in the middle of that phase, but a
    /// task added for a later phase runs in the same frame.
    TaskId add(Phase phase, std::string name, Task task, i32 order = 0);
    /// Safe to call from inside a running task (including on itself).
    void remove(TaskId id);
    void clear();

    void run(Phase phase, const FrameTime& time);

    [[nodiscard]] usize taskCount() const;
    /// Names of the tasks of a phase, in execution order (for diagnostics).
    [[nodiscard]] std::vector<std::string> taskNames(Phase phase) const;

    /// Timing of every task that ran, phase by phase in execution order.
    [[nodiscard]] std::vector<TaskStats> taskStats() const;
    void resetStats();

private:
    struct Entry {
        TaskId id = 0;
        i32 order = 0;
        std::string name;
        // Shared so run() can keep a running task alive (it may remove itself
        // or grow the vector) without copying the callable every frame.
        std::shared_ptr<const Task> task;
        f64 lastSeconds = 0.0;
        f64 totalSeconds = 0.0;
        u64 runs = 0;
    };

    void insert(Phase phase, Entry entry);
    void settle();  // applies what was deferred while tasks ran

    std::array<std::vector<Entry>, static_cast<usize>(Phase::Count)> phases_;
    // Tasks added while a phase runs: inserting them right away could shift
    // the entry being executed.
    std::vector<std::pair<Phase, Entry>> pendingAdds_;
    TaskId nextId_ = 1;
    u32 runDepth_ = 0;
    bool needsCompaction_ = false;
};

/// RAII helper that removes a task when it goes out of scope.
class ScopedTask {
public:
    ScopedTask() = default;
    ScopedTask(Scheduler& scheduler, TaskId id) : scheduler_(&scheduler), id_(id) {}
    ScopedTask(ScopedTask&& other) noexcept
        : scheduler_(other.scheduler_), id_(other.id_) {
        other.id_ = 0;
    }
    ScopedTask& operator=(ScopedTask&& other) noexcept {
        if (this != &other) {
            reset();
            scheduler_ = other.scheduler_;
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }
    ScopedTask(const ScopedTask&) = delete;
    ScopedTask& operator=(const ScopedTask&) = delete;
    ~ScopedTask() { reset(); }

    void reset() {
        if (id_ != 0 && scheduler_ != nullptr) {
            scheduler_->remove(id_);
        }
        id_ = 0;
    }

private:
    Scheduler* scheduler_ = nullptr;
    TaskId id_ = 0;
};

}  // namespace bdex
