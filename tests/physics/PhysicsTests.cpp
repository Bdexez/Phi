#include <bdex/core/Engine.hpp>
#include <bdex/physics/Physics.hpp>
#include <bdex/scene/Scene.hpp>
#include <bdex/scene/Components.hpp>
#include <bdex/scene/Serialization.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace bdex;
using namespace bdex::physics;
using scene::Transform;

namespace {

constexpr f32 kStep = 1.0f / 120.0f;

ecs::Entity addBall(ecs::World& world, const Vec3& position, const RigidBody& body, f32 radius = 0.5f) {
    const ecs::Entity ball = world.create();
    world.emplace<Transform>(ball).position = position;
    world.emplace<SphereCollider>(ball, radius);
    world.emplace<RigidBody>(ball, body);
    return ball;
}

ecs::Entity addFloor(ecs::World& world) {
    const ecs::Entity floor = world.create();
    world.emplace<Transform>(floor).position = Vec3(0.0f, -0.5f, 0.0f);  // top face at y = 0
    world.emplace<BoxCollider>(floor, Vec3(50.0f, 0.5f, 50.0f));
    return floor;
}

void simulate(ecs::World& world, f32 seconds) {
    for (int i = 0; i < static_cast<int>(seconds / kStep); ++i) {
        step(world, kStep);
    }
}

}  // namespace

TEST(Physics, ABallComesToRestOnTheFloor) {
    ecs::World world;
    addFloor(world);
    const ecs::Entity ball = addBall(world, Vec3(0.0f, 5.0f, 0.0f), RigidBody{});
    simulate(world, 6.0f);
    EXPECT_NEAR(world.get<Transform>(ball).position.y, 0.5f, 0.01f);  // resting on top
    EXPECT_LT(glm::length(world.get<RigidBody>(ball).velocity), 0.05f);
}

TEST(Physics, RestitutionSetsTheBounce) {
    // Highest point after the first bounce, from a drop of 4 m.
    const auto bounceHeight = [](f32 restitution) {
        ecs::World world;
        addFloor(world);
        RigidBody body;
        body.restitution = restitution;
        const ecs::Entity ball = addBall(world, Vec3(0.0f, 4.5f, 0.0f), body);
        f32 highest = 0.0f;
        bool bounced = false;
        for (int i = 0; i < 600; ++i) {
            step(world, kStep);
            const f32 y = world.get<Transform>(ball).position.y - 0.5f;
            const f32 vy = world.get<RigidBody>(ball).velocity.y;
            bounced = bounced || vy > 0.0f;
            if (bounced) {
                highest = std::max(highest, y);
            }
        }
        return highest;
    };
    // Height scales with restitution squared: 4 m * 0.25 = 1 m for 0.5.
    EXPECT_NEAR(bounceHeight(0.5f), 1.0f, 0.15f);
    EXPECT_LT(bounceHeight(0.0f), 0.02f);
    EXPECT_GT(bounceHeight(0.9f), 3.0f);
}

TEST(Physics, ElasticEqualMassesSwapVelocities) {
    ecs::World world;
    RigidBody left;
    left.velocity = Vec3(2.0f, 0.0f, 0.0f);
    left.restitution = 1.0f;
    left.gravityScale = 0.0f;
    RigidBody right = left;
    right.velocity = Vec3(0.0f);
    const ecs::Entity a = addBall(world, Vec3(-1.0f, 0.0f, 0.0f), left);
    const ecs::Entity b = addBall(world, Vec3(1.0f, 0.0f, 0.0f), right);
    simulate(world, 1.0f);
    EXPECT_NEAR(world.get<RigidBody>(a).velocity.x, 0.0f, 1e-3f);
    EXPECT_NEAR(world.get<RigidBody>(b).velocity.x, 2.0f, 1e-3f);
}

TEST(Physics, CollisionsConserveMomentum) {
    ecs::World world;
    RigidBody heavy;
    heavy.mass = 3.0f;
    heavy.velocity = Vec3(1.0f, 0.0f, 0.5f);
    heavy.gravityScale = 0.0f;
    RigidBody light;
    light.mass = 1.0f;
    light.velocity = Vec3(-2.0f, 0.0f, 0.0f);
    light.gravityScale = 0.0f;
    const ecs::Entity a = addBall(world, Vec3(-1.0f, 0.0f, 0.0f), heavy);
    const ecs::Entity b = addBall(world, Vec3(1.0f, 0.2f, 0.0f), light);
    const Vec3 before = heavy.velocity * heavy.mass + light.velocity * light.mass;
    simulate(world, 2.0f);
    const Vec3 after = world.get<RigidBody>(a).velocity * 3.0f + world.get<RigidBody>(b).velocity * 1.0f;
    EXPECT_NEAR(glm::length(after - before), 0.0f, 1e-3f);
    EXPECT_GT(glm::length(world.get<Transform>(b).position - world.get<Transform>(a).position), 0.99f);
}

TEST(Physics, FrictionStopsSliding) {
    ecs::World world;
    addFloor(world);
    RigidBody body;
    body.velocity = Vec3(3.0f, 0.0f, 0.0f);
    body.restitution = 0.0f;
    const ecs::Entity slider = addBall(world, Vec3(0.0f, 0.5f, 0.0f), body);
    simulate(world, 3.0f);
    EXPECT_LT(std::abs(world.get<RigidBody>(slider).velocity.x), 0.05f);
    EXPECT_GT(world.get<Transform>(slider).position.x, 0.2f);  // it slid some distance first
}

TEST(Physics, StepsAreDeterministicAndFreeFallIsExact) {
    const auto run = [] {
        ecs::World world;
        addFloor(world);
        for (int i = 0; i < 20; ++i) {
            RigidBody body;
            body.velocity = Vec3(static_cast<f32>(i % 5) - 2.0f, 0.0f, static_cast<f32>(i % 3) - 1.0f);
            addBall(world, Vec3(static_cast<f32>(i % 4), 2.0f + static_cast<f32>(i), static_cast<f32>(i / 4)), body, 0.4f);
        }
        simulate(world, 3.0f);
        std::vector<Vec3> positions;
        world.view<const Transform, const RigidBody>().each(
            [&](const Transform& transform, const RigidBody&) { positions.push_back(transform.position); });
        return positions;
    };
    EXPECT_EQ(run(), run());

    // Nothing to hit: gravity only (semi-implicit Euler).
    ecs::World world;
    const ecs::Entity ball = addBall(world, Vec3(0.0f), RigidBody{});
    step(world, 0.5f);
    EXPECT_NEAR(world.get<RigidBody>(ball).velocity.y, -9.81f * 0.5f, 1e-5f);
    EXPECT_NEAR(world.get<Transform>(ball).position.y, -9.81f * 0.25f, 1e-5f);
}

TEST(Physics, SceneFilesKeepBodiesAndColliders) {
    scene::ComponentSerializers serializers = scene::ComponentSerializers::withSceneComponents();
    addComponentSerializers(serializers);
    ecs::World world;
    addFloor(world);
    RigidBody body;
    body.velocity = Vec3(1.0f, 2.0f, 3.0f);
    body.mass = 2.5f;
    addBall(world, Vec3(0.0f, 1.0f, 0.0f), body, 0.75f);

    ecs::World copy;
    const auto loaded = serializers.load(copy, serializers.save(world));
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    usize bodies = 0;
    copy.view<const RigidBody, const SphereCollider>().each([&](const RigidBody& b, const SphereCollider& c) {
        ++bodies;
        EXPECT_EQ(b.velocity, Vec3(1.0f, 2.0f, 3.0f));
        EXPECT_FLOAT_EQ(b.mass, 2.5f);
        EXPECT_FLOAT_EQ(c.radius, 0.75f);
    });
    EXPECT_EQ(bodies, 1u);
    EXPECT_EQ(copy.view<const BoxCollider>().count(), 1u);
}

TEST(Physics, BodiesAreDrawnBetweenTheirLastTwoSteps) {
    ecs::World world;
    RigidBody body;
    body.velocity = Vec3(10.0f, 0.0f, 0.0f);
    body.gravityScale = 0.0f;
    const ecs::Entity ball = addBall(world, Vec3(0.0f), body);
    world.emplace<scene::WorldTransform>(ball);

    interpolate(world, 0.5f);  // never stepped: nothing to blend yet
    EXPECT_EQ(Vec3(world.get<scene::WorldTransform>(ball).matrix[3]), Vec3(0.0f));

    step(world, 0.1f);  // x: 0 -> 1
    for (const f32 alpha : {0.0f, 0.25f, 1.0f}) {
        interpolate(world, alpha);
        EXPECT_NEAR(world.get<scene::WorldTransform>(ball).matrix[3].x, alpha, 1e-5f);
    }
    EXPECT_FLOAT_EQ(world.get<Transform>(ball).position.x, 1.0f);  // the simulated pose is untouched
}

TEST(Physics, InterpolationSmoothsMotionBetweenFixedSteps) {
    // Displayed x of a ball moving at a constant 10 m/s, simulated at
    // 60 Hz and drawn at about 144 Hz, through the engine loop.
    const auto frameSteps = [](bool interpolated) {
        Config config;
        config.set("engine.forced_delta", "0.0069444");
        config.set("engine.fixed_timestep", "0.0166667");
        Engine engine(config);
        EXPECT_TRUE(engine.initialize().has_value());
        ecs::World world;
        RigidBody body;
        body.velocity = Vec3(10.0f, 0.0f, 0.0f);
        body.gravityScale = 0.0f;
        const ecs::Entity ball = addBall(world, Vec3(0.0f), body);
        world.emplace<scene::WorldTransform>(ball);
        engine.scheduler().add(Phase::FixedUpdate, "physics",
                               [&](const FrameTime& time) { step(world, static_cast<f32>(time.delta)); });
        engine.scheduler().add(Phase::PostUpdate, "transforms", [&](const FrameTime& time) {
            scene::updateWorldTransforms(world);
            if (interpolated) {
                interpolate(world, static_cast<f32>(time.fixedAlpha));
            }
        });
        std::vector<f32> steps;
        f32 previous = 0.0f;
        for (int frame = 0; frame < 60; ++frame) {
            engine.tick();
            const f32 drawn = world.get<scene::WorldTransform>(ball).matrix[3].x;
            if (frame > 10) {  // past the start
                steps.push_back(drawn - previous);
            }
            previous = drawn;
        }
        const auto [smallest, largest] = std::minmax_element(steps.begin(), steps.end());
        return *largest - *smallest;
    };
    // Without interpolation the ball stands still some frames and jumps a
    // whole step on others; with it, every frame moves it about as much.
    EXPECT_GT(frameSteps(false), 0.1f);
    EXPECT_LT(frameSteps(true), 0.01f);
}

TEST(Physics, BallsFindTheirBoxAmongMany) {
    ecs::World world;
    addFloor(world);
    // A 20 x 20 field of 1 m crates, 2 m apart (top faces at y = 1).
    for (int x = 0; x < 20; ++x) {
        for (int z = 0; z < 20; ++z) {
            const ecs::Entity crate = world.create();
            world.emplace<Transform>(crate).position = Vec3(static_cast<f32>(x) * 2.0f, 0.5f, static_cast<f32>(z) * 2.0f);
            world.emplace<BoxCollider>(crate);
        }
    }
    RigidBody body;
    body.restitution = 0.0f;
    const ecs::Entity onCrate = addBall(world, Vec3(14.0f, 3.0f, 22.0f), body, 0.4f);   // above crate (7, 11)
    const ecs::Entity between = addBall(world, Vec3(15.0f, 3.0f, 23.0f), body, 0.4f);   // in the gap
    simulate(world, 3.0f);
    EXPECT_NEAR(world.get<Transform>(onCrate).position.y, 1.4f, 0.02f);
    EXPECT_NEAR(world.get<Transform>(between).position.y, 0.4f, 0.02f);
}
