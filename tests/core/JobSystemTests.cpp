#include <bdex/core/JobSystem.hpp>

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <set>

#include <atomic>
#include <numeric>
#include <vector>

using namespace bdex;

TEST(JobSystem, SubmitReturnsResults) {
    JobSystem jobs(4);
    EXPECT_EQ(jobs.threadCount(), 4u);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 64; ++i) {
        futures.push_back(jobs.submit([i] { return i * i; }));
    }
    int sum = 0;
    for (auto& future : futures) {
        sum += future.get();
    }
    EXPECT_EQ(sum, 85344);  // sum of squares 0..63
}

TEST(JobSystem, ParallelForCoversEveryIndexExactlyOnce) {
    JobSystem jobs(4);
    std::vector<std::atomic<int>> hits(10'000);

    jobs.parallelFor(hits.size(), 128, [&](usize begin, usize end) {
        for (usize i = begin; i < end; ++i) {
            hits[i].fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (const auto& hit : hits) {
        ASSERT_EQ(hit.load(), 1);
    }
}

TEST(JobSystem, ParallelForCanBeNestedInsideAJob) {
    JobSystem jobs(2);
    std::atomic<int> total{0};

    // Saturate the pool with jobs that each run their own parallelFor.
    std::vector<std::future<void>> futures;
    for (int job = 0; job < 8; ++job) {
        futures.push_back(jobs.submit([&] {
            jobs.parallelFor(100, 10, [&](usize begin, usize end) {
                total.fetch_add(static_cast<int>(end - begin), std::memory_order_relaxed);
            });
        }));
    }
    for (auto& future : futures) {
        future.get();
    }
    EXPECT_EQ(total.load(), 800);
}

TEST(JobSystem, ParallelForHandlesTrivialRanges) {
    JobSystem jobs(2);
    int calls = 0;
    jobs.parallelFor(0, 16, [&](usize, usize) { ++calls; });
    EXPECT_EQ(calls, 0);
    jobs.parallelFor(5, 16, [&](usize begin, usize end) {
        ++calls;
        EXPECT_EQ(begin, 0u);
        EXPECT_EQ(end, 5u);
    });
    EXPECT_EQ(calls, 1);
}

TEST(JobSystem, AJobCanWaitForJobsItSubmitted) {
    // One worker: a job blocking on a job queued behind it would never see
    // it run. wait() runs queued jobs meanwhile.
    JobSystem jobs(1);
    auto outer = jobs.submit([&jobs] {
        auto middle = jobs.submit([&jobs] {
            auto inner = jobs.submit([] { return 5; });
            jobs.wait(inner);
            return inner.get() * 2;
        });
        jobs.wait(middle);
        return middle.get() + 1;
    });
    EXPECT_EQ(outer.get(), 11);
}

TEST(JobSystem, TracesItsWorkOnEachThreadsRow) {
    JobSystem jobs(3);
    {
        TraceRecorder trace;
        jobs.setTrace(&trace);
        std::atomic<int> sum = 0;
        jobs.parallelFor(64, 1, [&](usize begin, usize end) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));  // long enough to spread
            sum += static_cast<int>(end - begin);
        });
        jobs.setTrace(nullptr);
        EXPECT_EQ(sum, 64);

        usize chunks = 0;
        std::set<u32> rows;
        for (const TraceEvent& event : trace.events()) {
            if (event.name == "parallelFor") {
                ++chunks;
                rows.insert(event.thread);
            }
        }
        EXPECT_EQ(chunks, 64u);  // one span per chunk
        EXPECT_GT(rows.size(), 1u);  // on more than one row
        const std::string text = trace.toChromeJson();
        EXPECT_NE(text.find("worker 3"), std::string::npos);
    }

    // A job still running when tracing stops: setTrace(nullptr) returns once
    // it no longer records, so the recorder can go at once.
    auto release = std::make_shared<std::atomic<bool>>(false);
    auto started = std::make_shared<std::atomic<bool>>(false);
    std::future<void> slow;
    {
        TraceRecorder trace;
        jobs.setTrace(&trace);
        slow = jobs.submit([release, started] {
            *started = true;
            while (!*release) {
                std::this_thread::yield();
            }
        });
        while (!*started) {  // running, and so recording, before tracing stops
            std::this_thread::yield();
        }
        std::thread stopper([release] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            *release = true;
        });
        jobs.setTrace(nullptr);  // waits for the job, which records its span
        stopper.join();
        EXPECT_EQ(trace.events().size(), 1u);
    }  // destroyed: nothing may write to it any more
    slow.get();
}
