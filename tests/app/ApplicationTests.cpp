// The application layer end to end: command line, configuration layering,
// exit codes and lifecycle callbacks, on the headless window module.

#include <bdex/app/Application.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/physics/Physics.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace bdex;

namespace {

/// Records what the application went through.
class ProbeApp final : public app::Application {
public:
    bool failStart = false;
    std::vector<std::string> trace;
    Config seen;  // the engine configuration, as onStart saw it
    bool hadActions = false;
    bool jumpBound = false;
    int updates = 0;

    int run(std::vector<std::string> arguments) {
        arguments.insert(arguments.begin(), "probe");
        std::vector<char*> argv;
        for (std::string& argument : arguments) {
            argv.push_back(argument.data());
        }
        log::setConsoleEnabled(false);
        const int code = Application::run(static_cast<int>(argv.size()), argv.data());
        log::setConsoleEnabled(true);
        return code;
    }

protected:
    void onConfigure(Config& config) override {
        config.set("engine.modules", "window_null");
        config.set("probe.layer", "application");
        config.set("input.jump", "Space");
    }
    Result<> onStart() override {
        trace.push_back("start");
        seen = engine().config();
        hadActions = actions() != nullptr;
        jumpBound = hadActions && actions()->has("jump");
        if (failStart) {
            return fail("refusing to start");
        }
        return {};
    }
    void onUpdate(const FrameTime&) override { ++updates; }
    void onStop() override {
        trace.push_back("stop");
    }
};

}  // namespace

TEST(Application, RunsTheLifecycleForTheRequestedFrames) {
    ProbeApp app;
    EXPECT_EQ(app.run({"--frames", "5"}), 0);
    EXPECT_EQ(app.trace, (std::vector<std::string>{"start", "stop"}));
    EXPECT_EQ(app.updates, 5);
    // The window module provides input, so actions from [input] exist.
    EXPECT_TRUE(app.hadActions);
    EXPECT_TRUE(app.jumpBound);
}

TEST(Application, CommandLineOverridesFileOverridesDefaults) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "bdex_tests_app.ini";
    std::ofstream(file) << "[probe]\nlayer = file\nonly_in_file = yes\n";

    ProbeApp app;
    EXPECT_EQ(app.run({"--config", file.string(), "--frames", "1"}), 0);
    EXPECT_EQ(app.seen.getString("probe.layer"), "file");
    EXPECT_TRUE(app.seen.getBool("probe.only_in_file"));

    ProbeApp overridden;
    EXPECT_EQ(overridden.run({"--config", file.string(), "--set", "probe.layer=cli", "--frames", "1"}), 0);
    EXPECT_EQ(overridden.seen.getString("probe.layer"), "cli");
    std::filesystem::remove(file);
}

TEST(Application, ReportsProblemsThroughExitCodes) {
    ProbeApp badOption;
    EXPECT_EQ(badOption.run({"--no-such-option"}), 2);
    EXPECT_TRUE(badOption.trace.empty());  // nothing started

    ProbeApp missingValue;
    EXPECT_EQ(missingValue.run({"--frames"}), 2);

    ProbeApp help;
    EXPECT_EQ(help.run({"--help"}), 0);
    EXPECT_TRUE(help.trace.empty());

    ProbeApp unknownModule;
    EXPECT_EQ(unknownModule.run({"--modules", "window_null,no_such_module"}), 1);
    EXPECT_TRUE(unknownModule.trace.empty());

    ProbeApp missingConfig;
    EXPECT_EQ(missingConfig.run({"--config", "/nonexistent/probe.ini"}), 1);

    // A failing onStart still gets onStop, and no frame runs.
    ProbeApp refusing;
    refusing.failStart = true;
    EXPECT_EQ(refusing.run({"--frames", "5"}), 1);
    EXPECT_EQ(refusing.trace, (std::vector<std::string>{"start", "stop"}));
    EXPECT_EQ(refusing.updates, 0);
}

TEST(Application, StepsPhysicsInTheFixedUpdate) {
    struct Dropper final : app::Application {
        f32 height = 0.0f;
        ecs::Entity ball;
        int run(std::vector<std::string> arguments) {
            arguments.insert(arguments.begin(), "dropper");
            std::vector<char*> argv;
            for (std::string& argument : arguments) {
                argv.push_back(argument.data());
            }
            log::setConsoleEnabled(false);
            const int code = Application::run(static_cast<int>(argv.size()), argv.data());
            log::setConsoleEnabled(true);
            return code;
        }
        void onConfigure(Config& config) override { config.set("engine.modules", "window_null"); }
        Result<> onStart() override {
            ball = scene().createEntity("ball");
            scene().world().emplace<physics::SphereCollider>(ball);
            scene().world().emplace<physics::RigidBody>(ball);
            return {};
        }
        void onStop() override { height = scene().world().get<scene::Transform>(ball).position.y; }
    };

    // 10 frames of 0.1 s with a 0.1 s step: 10 steps of a 2 m/s^2 gravity.
    Dropper dropper;
    EXPECT_EQ(dropper.run({"--frames", "10", "--set", "engine.forced_delta=0.1", "--set",
                           "engine.fixed_timestep=0.1", "--set", "physics.gravity=0 -2 0"}),
              0);
    // Semi-implicit Euler: y = -g dt^2 (1 + 2 + ... + 10) = -2 * 0.01 * 55.
    EXPECT_NEAR(dropper.height, -1.1f, 1e-4f);
}

TEST(Application, PublishesCollisions) {
    struct Bouncer final : app::Application {
        int collisions = 0;
        Subscription subscription;
        int run(std::vector<std::string> arguments) {
            arguments.insert(arguments.begin(), "bouncer");
            std::vector<char*> argv;
            for (std::string& argument : arguments) {
                argv.push_back(argument.data());
            }
            log::setConsoleEnabled(false);
            const int code = Application::run(static_cast<int>(argv.size()), argv.data());
            log::setConsoleEnabled(true);
            return code;
        }
        void onConfigure(Config& config) override { config.set("engine.modules", "window_null"); }
        Result<> onStart() override {
            ecs::World& world = scene().world();
            const ecs::Entity floor = scene().createEntity("floor");
            world.get<scene::Transform>(floor).position = Vec3(0.0f, -0.5f, 0.0f);
            world.emplace<physics::BoxCollider>(floor, Vec3(10.0f, 0.5f, 10.0f));
            const ecs::Entity ball = scene().createEntity("ball");
            world.get<scene::Transform>(ball).position = Vec3(0.0f, 3.0f, 0.0f);
            world.emplace<physics::SphereCollider>(ball);
            world.emplace<physics::RigidBody>(ball).restitution = 0.0f;
            subscription = engine().events().subscribe<physics::Collision>(
                [this](const physics::Collision&) { ++collisions; });
            return {};
        }
        void onStop() override { subscription.reset(); }
    };
    Bouncer bouncer;
    EXPECT_EQ(bouncer.run({"--frames", "120", "--set", "engine.forced_delta=0.016"}), 0);
    EXPECT_EQ(bouncer.collisions, 1);
}
