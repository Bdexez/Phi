#include <phi/core/Engine.hpp>
#include <phi/core/Json.hpp>
#include <phi/core/Trace.hpp>

#include <gtest/gtest.h>

#include <string>
#include <thread>

using namespace phi;

TEST(Trace, WritesChromeTraceEvents) {
    TraceRecorder trace;
    const auto start = TraceRecorder::Clock::now();
    trace.record("frame 0", "frame", start, start + std::chrono::milliseconds(16));
    trace.record("physics \"step\"", "FixedUpdate", start + std::chrono::milliseconds(2),
                 start + std::chrono::milliseconds(5), 3);
    const auto parsed = json::parse(trace.toChromeJson());
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    const json::Value& events = (*parsed)["traceEvents"];
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0]["ph"].asString(), "X");
    EXPECT_DOUBLE_EQ(events[0]["ts"].asNumber(), 0.0);  // times from the first event
    EXPECT_DOUBLE_EQ(events[0]["dur"].asNumber(), 16000.0);  // microseconds
    EXPECT_EQ(events[1]["name"].asString(), "physics \"step\"");
    EXPECT_EQ(events[1]["cat"].asString(), "FixedUpdate");
    EXPECT_DOUBLE_EQ(events[1]["ts"].asNumber(), 2000.0);
    EXPECT_DOUBLE_EQ(events[1]["tid"].asNumber(), 3.0);

    // Bounded: past the limit, events are dropped and the trace says so.
    TraceRecorder small(1);
    small.record("a", "x", start, start);
    small.record("b", "x", start, start);
    EXPECT_EQ(small.events().size(), 1u);
    EXPECT_TRUE(small.truncated());
}

TEST(Trace, NestsTasksInPhasesInFrames) {
    Config config;
    config.set("engine.forced_delta", "0.05");
    config.set("engine.fixed_timestep", "0.025");  // two fixed steps per frame
    Engine engine(config);
    engine.scheduler().add(Phase::Update, "think", [](const FrameTime&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    engine.scheduler().add(Phase::FixedUpdate, "simulate", [](const FrameTime&) {});
    ASSERT_TRUE(engine.initialize().has_value());

    TraceRecorder trace;
    engine.setTrace(&trace);
    engine.tick();
    engine.tick();
    engine.setTrace(nullptr);
    engine.tick();  // not recorded

    const auto count = [&](std::string_view name) {
        return std::count_if(trace.events().begin(), trace.events().end(),
                             [&](const TraceEvent& event) { return event.name == name; });
    };
    EXPECT_EQ(count("frame 0"), 1);
    EXPECT_EQ(count("frame 1"), 1);
    EXPECT_EQ(count("frame 2"), 0);
    EXPECT_EQ(count("think"), 2);
    EXPECT_EQ(count("simulate"), 4);
    EXPECT_EQ(count("FixedUpdate"), 4);  // a phase span per run

    // Each task lies within its phase's span, and each phase within its frame.
    const auto find = [&](std::string_view name) {
        return *std::find_if(trace.events().begin(), trace.events().end(),
                             [&](const TraceEvent& event) { return event.name == name; });
    };
    const auto inside = [](const TraceEvent& inner, const TraceEvent& outer) {
        return inner.startMicroseconds >= outer.startMicroseconds &&
               inner.startMicroseconds + inner.durationMicroseconds <=
                   outer.startMicroseconds + outer.durationMicroseconds + 1e-3;
    };
    const TraceEvent think = find("think");
    const TraceEvent update = find("Update");
    EXPECT_GE(think.durationMicroseconds, 1000.0);
    EXPECT_TRUE(inside(think, update));
    EXPECT_TRUE(inside(update, find("frame 0")));
    EXPECT_EQ(update.category, "phase");
    EXPECT_EQ(think.category, "Update");
    engine.shutdown();
}

TEST(Trace, ThreadsRecordConcurrentlyOnNamedRows) {
    TraceRecorder trace;
    trace.nameThread(0, "main");
    trace.nameThread(2, "worker 2");
    std::vector<std::thread> threads;
    for (u32 row = 0; row < 4; ++row) {
        threads.emplace_back([&trace, row] {
            for (int i = 0; i < 500; ++i) {
                const auto now = TraceRecorder::Clock::now();
                trace.record("work", "jobs", now, now, row);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(trace.events().size(), 2000u);

    const auto parsed = json::parse(trace.toChromeJson());
    ASSERT_TRUE(parsed.has_value());
    const json::Value& events = (*parsed)["traceEvents"];
    // The row labels come first, as metadata events.
    EXPECT_EQ(events[0]["ph"].asString(), "M");
    EXPECT_EQ(events[0]["name"].asString(), "thread_name");
    EXPECT_EQ(events[0]["args"]["name"].asString(), "main");
    EXPECT_EQ(events[1]["args"]["name"].asString(), "worker 2");
    EXPECT_DOUBLE_EQ(events[1]["tid"].asNumber(), 2.0);
    EXPECT_EQ(events.size(), 2002u);
}
