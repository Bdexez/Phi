#include <phi/core/JobSystem.hpp>

#include <algorithm>
#include <atomic>
#include <format>

namespace phi {

namespace {

/// The trace row of the running thread: 0 for any thread outside a pool,
/// its index plus one for a worker.
thread_local u32 traceRow = 0;

}  // namespace

JobSystem::JobSystem(u32 threadCount) {
    if (threadCount == 0) {
        const u32 hardware = std::thread::hardware_concurrency();
        threadCount = hardware > 1 ? hardware - 1 : 1;
    }
    workers_.reserve(threadCount);
    for (u32 i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this, i] { workerLoop(i + 1); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

void JobSystem::enqueue(std::function<void()> job) {
    {
        std::lock_guard lock(mutex_);
        queue_.push_back(std::move(job));
    }
    wake_.notify_one();
}

bool JobSystem::runPendingJob() {
    std::function<void()> job;
    {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        job = std::move(queue_.front());
        queue_.pop_front();
    }
    traced("job", "jobs", job);
    return true;
}

void JobSystem::setTrace(TraceRecorder* trace) {
    if (trace != nullptr) {
        trace->nameThread(0, "main");
        for (u32 i = 0; i < threadCount(); ++i) {
            trace->nameThread(i + 1, std::format("worker {}", i + 1));
        }
    }
    trace_.store(trace, std::memory_order_seq_cst);
    // A job that read the old recorder may still be writing to it.
    while (recording_.load(std::memory_order_seq_cst) != 0) {
        std::this_thread::yield();
    }
}

void JobSystem::traced(std::string_view name, std::string_view category, const std::function<void()>& work) {
    if (trace_.load(std::memory_order_relaxed) == nullptr) {
        work();
        return;
    }
    // Announce the recording before reading the recorder: setTrace either
    // sees this count, or this job sees its new value.
    recording_.fetch_add(1, std::memory_order_seq_cst);
    TraceRecorder* trace = trace_.load(std::memory_order_seq_cst);
    const auto start = TraceRecorder::Clock::now();
    work();
    if (trace != nullptr) {
        trace->record(name, category, start, TraceRecorder::Clock::now(), traceRow);
    }
    recording_.fetch_sub(1, std::memory_order_seq_cst);
}

void JobSystem::workerLoop(u32 index) {
    traceRow = index;
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // stopping, and every queued job has been drained
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        traced("job", "jobs", job);
    }
}

void JobSystem::parallelFor(usize count, usize grain,
                            const std::function<void(usize, usize)>& fn) {
    if (count == 0) {
        return;
    }
    grain = std::max<usize>(grain, 1);
    const usize chunks = (count + grain - 1) / grain;
    if (chunks == 1) {
        // Too little to share: done here, but still on the timeline, which
        // then shows the work stayed on the caller.
        traced("parallelFor", "jobs", [&] { fn(0, count); });
        return;
    }

    // Chunks are claimed through an atomic cursor by the workers *and* the
    // caller. `fn` is only dereferenced after a successful claim, and the
    // caller does not return before every claimed chunk has completed, so the
    // reference stays valid for as long as it can be used.
    struct Shared {
        std::atomic<usize> next{0};
        std::atomic<usize> completed{0};
        usize chunks = 0;
        usize count = 0;
        usize grain = 0;
        const std::function<void(usize, usize)>* fn = nullptr;
        std::mutex mutex;
        std::condition_variable done;
    };
    const auto shared = std::make_shared<Shared>();
    shared->chunks = chunks;
    shared->count = count;
    shared->grain = grain;
    shared->fn = &fn;

    const auto work = [this, shared] {
        for (;;) {
            const usize chunk = shared->next.fetch_add(1, std::memory_order_relaxed);
            if (chunk >= shared->chunks) {
                return;
            }
            const usize begin = chunk * shared->grain;
            const usize end = std::min(shared->count, begin + shared->grain);
            traced("parallelFor", "jobs", [&] { (*shared->fn)(begin, end); });
            if (shared->completed.fetch_add(1, std::memory_order_acq_rel) + 1 == shared->chunks) {
                std::lock_guard lock(shared->mutex);
                shared->done.notify_all();
            }
        }
    };

    const usize helpers = std::min<usize>(workers_.size(), chunks - 1);
    for (usize i = 0; i < helpers; ++i) {
        enqueue(work);
    }
    work();

    std::unique_lock lock(shared->mutex);
    shared->done.wait(lock, [&] {
        return shared->completed.load(std::memory_order_acquire) == shared->chunks;
    });
}

}  // namespace phi
