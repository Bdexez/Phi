#include <phi/core/Scheduler.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace phi;

TEST(Scheduler, RunsTasksByOrderThenRegistration) {
    Scheduler scheduler;
    std::vector<std::string> trace;
    scheduler.add(Phase::Update, "late", [&](const FrameTime&) { trace.push_back("late"); }, 10);
    scheduler.add(Phase::Update, "a", [&](const FrameTime&) { trace.push_back("a"); });
    scheduler.add(Phase::Update, "early", [&](const FrameTime&) { trace.push_back("early"); }, -5);
    scheduler.add(Phase::Update, "b", [&](const FrameTime&) { trace.push_back("b"); });
    scheduler.add(Phase::Render, "render", [&](const FrameTime&) { trace.push_back("render"); });

    scheduler.run(Phase::Update, {});
    EXPECT_EQ(trace, (std::vector<std::string>{"early", "a", "b", "late"}));
    EXPECT_EQ(scheduler.taskNames(Phase::Update),
              (std::vector<std::string>{"early", "a", "b", "late"}));
    EXPECT_EQ(scheduler.taskCount(), 5u);
}

TEST(Scheduler, TaskCanRemoveItselfWhileRunning) {
    Scheduler scheduler;
    int runs = 0;
    TaskId id = 0;
    id = scheduler.add(Phase::Update, "once", [&](const FrameTime&) {
        ++runs;
        scheduler.remove(id);
    });
    int other = 0;
    scheduler.add(Phase::Update, "always", [&](const FrameTime&) { ++other; });

    scheduler.run(Phase::Update, {});
    scheduler.run(Phase::Update, {});

    EXPECT_EQ(runs, 1);
    EXPECT_EQ(other, 2);
    EXPECT_EQ(scheduler.taskCount(), 1u);
}

TEST(Scheduler, ScopedTaskRemovesOnDestruction) {
    Scheduler scheduler;
    int runs = 0;
    {
        const ScopedTask task(scheduler,
                              scheduler.add(Phase::Input, "scoped", [&](const FrameTime&) { ++runs; }));
        scheduler.run(Phase::Input, {});
    }
    scheduler.run(Phase::Input, {});
    EXPECT_EQ(runs, 1);
}

TEST(Scheduler, PassesFrameTimeThrough) {
    Scheduler scheduler;
    FrameTime seen;
    scheduler.add(Phase::Update, "capture", [&](const FrameTime& time) { seen = time; });
    scheduler.run(Phase::Update, FrameTime{0.5, 2.0, 7});
    EXPECT_DOUBLE_EQ(seen.delta, 0.5);
    EXPECT_DOUBLE_EQ(seen.elapsed, 2.0);
    EXPECT_EQ(seen.frame, 7u);
}

TEST(Scheduler, TasksAddedWhileRunningNeverRerunTheCurrentOne) {
    Scheduler scheduler;
    std::vector<std::string> trace;
    bool added = false;
    scheduler.add(Phase::Update, "spawner", [&](const FrameTime&) {
        trace.push_back("spawner");
        if (!added) {
            added = true;
            // Sorts before the running task.
            scheduler.add(Phase::Update, "early", [&](const FrameTime&) { trace.push_back("early"); }, -1);
            scheduler.add(Phase::Render, "render", [&](const FrameTime&) { trace.push_back("render"); });
        }
    });

    scheduler.run(Phase::Update, {});
    EXPECT_EQ(trace, (std::vector<std::string>{"spawner"}));
    // Tasks added during a run are in place once it ends, for every phase.
    EXPECT_EQ(scheduler.taskNames(Phase::Update), (std::vector<std::string>{"early", "spawner"}));
    scheduler.run(Phase::Render, {});
    scheduler.run(Phase::Update, {});
    EXPECT_EQ(trace, (std::vector<std::string>{"spawner", "render", "early", "spawner"}));
}

TEST(Scheduler, TimesEveryTaskRun) {
    Scheduler scheduler;
    scheduler.add(Phase::Update, "slow", [](const FrameTime&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    });
    scheduler.add(Phase::FixedUpdate, "fixed", [](const FrameTime&) {});
    scheduler.add(Phase::Render, "never", [](const FrameTime&) {});

    scheduler.run(Phase::FixedUpdate, {});
    scheduler.run(Phase::FixedUpdate, {});
    scheduler.run(Phase::Update, {});

    const std::vector<TaskStats> stats = scheduler.taskStats();
    ASSERT_EQ(stats.size(), 2u);  // tasks that never ran are left out
    EXPECT_EQ(stats[0].name, "fixed");
    EXPECT_EQ(stats[0].phase, Phase::FixedUpdate);
    EXPECT_EQ(stats[0].runs, 2u);
    EXPECT_EQ(stats[1].name, "slow");
    EXPECT_EQ(stats[1].runs, 1u);
    EXPECT_GE(stats[1].lastMs, 3.0);
    EXPECT_DOUBLE_EQ(stats[1].averageMs, stats[1].lastMs);

    scheduler.resetStats();
    EXPECT_TRUE(scheduler.taskStats().empty());
}
