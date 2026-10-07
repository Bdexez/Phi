#include <bdex/core/Engine.hpp>
#include <bdex/physics/Physics.hpp>
#include <bdex/scene/Scene.hpp>
#include <bdex/scene/Components.hpp>
#include <bdex/scene/Serialization.hpp>

#include <gtest/gtest.h>

#include <limits>

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

TEST(Physics, ASlidingBallEndsUpRolling) {
    ecs::World world;
    addFloor(world);
    RigidBody body;
    body.velocity = Vec3(7.0f, 0.0f, 0.0f);
    body.restitution = 0.0f;
    const ecs::Entity ball = addBall(world, Vec3(0.0f, 0.5f, 0.0f), body);
    const Quat start = world.get<Transform>(ball).rotation;
    simulate(world, 3.0f);
    const RigidBody& rolled = world.get<RigidBody>(ball);
    // Friction trades speed for spin until the contact point stops
    // slipping: a solid sphere keeps 5/7 of its speed, then rolls on.
    EXPECT_NEAR(rolled.velocity.x, 5.0f, 0.1f);
    EXPECT_NEAR(rolled.angularVelocity.z, -rolled.velocity.x / 0.5f, 0.2f);  // v = w r
    EXPECT_NEAR(rolled.angularVelocity.x, 0.0f, 1e-3f);
    // And it visibly turned.
    EXPECT_LT(std::abs(glm::dot(world.get<Transform>(ball).rotation, start)), 0.99f);
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

TEST(Physics, RaysHitTheNearestCollider) {
    ecs::World world;
    const ecs::Entity floor = addFloor(world);  // top at y = 0
    const ecs::Entity ball = addBall(world, Vec3(0.0f, 1.0f, -5.0f), RigidBody{}, 0.5f);
    const ecs::Entity wall = world.create();
    world.emplace<Transform>(wall).position = Vec3(0.0f, 1.0f, -10.0f);
    world.emplace<BoxCollider>(wall, Vec3(2.0f, 2.0f, 0.5f));

    // Along -Z at y = 1: the ball first, then the wall.
    const auto first = raycast(world, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->entity, ball);
    EXPECT_NEAR(first->distance, 4.5f, 1e-4f);
    EXPECT_NEAR(first->normal.z, 1.0f, 1e-4f);

    // Beside the ball: the wall's front face (z = -9.5).
    const auto second = raycast(world, Vec3(1.5f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->entity, wall);
    EXPECT_NEAR(second->distance, 9.5f, 1e-4f);
    EXPECT_EQ(second->normal, Vec3(0.0f, 0.0f, 1.0f));

    // Straight down: the floor's top, with an upward normal.
    const auto ground = raycast(world, Vec3(10.0f, 3.0f, 10.0f), Vec3(0.0f, -1.0f, 0.0f));
    ASSERT_TRUE(ground.has_value());
    EXPECT_EQ(ground->entity, floor);
    EXPECT_NEAR(ground->point.y, 0.0f, 1e-4f);
    EXPECT_EQ(ground->normal, Vec3(0.0f, 1.0f, 0.0f));

    // Out of reach, or pointing away: nothing.
    EXPECT_FALSE(raycast(world, Vec3(1.5f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), 9.0f).has_value());
    EXPECT_FALSE(raycast(world, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)).has_value());
}

TEST(Physics, StepsReportHardContactsOnly) {
    ecs::World world;
    const ecs::Entity floor = addFloor(world);
    RigidBody body;
    body.restitution = 0.0f;
    const ecs::Entity ball = addBall(world, Vec3(0.0f, 5.5f, 0.0f), body);  // 5 m above the floor

    std::vector<Contact> contacts;
    for (int i = 0; i < 360; ++i) {  // 3 s: falls, hits, rests
        step(world, kStep, {}, &contacts);
    }
    ASSERT_EQ(contacts.size(), 1u);  // resting contacts stay below the threshold
    const Contact& hit = contacts.front();
    EXPECT_EQ(hit.a, ball);
    EXPECT_EQ(hit.b, floor);
    EXPECT_EQ(hit.normal, Vec3(0.0f, -1.0f, 0.0f));  // from the ball toward the floor
    EXPECT_NEAR(hit.point.y, 0.0f, 0.05f);
    // m (1 + e) v with v = sqrt(2 g h) = 9.9 m/s.
    EXPECT_NEAR(hit.impulse, std::sqrt(2.0f * 9.81f * 5.0f), 0.3f);

    // With no threshold, resting contacts are reported every step (while
    // the ball is awake: asleep, it no longer touches anything).
    EXPECT_TRUE(world.get<RigidBody>(ball).asleep);
    contacts.clear();
    Settings everything;
    everything.minimumContactImpulse = 0.0f;
    everything.sleepSpeed = 0.0f;
    step(world, kStep, everything, &contacts);
    EXPECT_EQ(contacts.size(), 1u);
}

TEST(Physics, TriggersReportBodiesPassingThrough) {
    ecs::World world;
    const ecs::Entity gate = world.create();
    world.emplace<Transform>(gate).position = Vec3(5.0f, 0.0f, 0.0f);
    world.emplace<BoxCollider>(gate, Vec3(0.5f, 2.0f, 2.0f));
    world.emplace<Trigger>(gate);
    RigidBody body;
    body.velocity = Vec3(10.0f, 0.0f, 0.0f);
    body.gravityScale = 0.0f;
    const ecs::Entity ball = addBall(world, Vec3(0.0f), body, 0.25f);

    std::vector<TriggerEvent> events;
    std::vector<f32> crossings;  // x when each event fired
    for (int i = 0; i < 120; ++i) {  // 1 s: x goes from 0 to 10
        const usize before = events.size();
        step(world, kStep, {}, nullptr, &events);
        if (events.size() > before) {
            crossings.push_back(world.get<Transform>(ball).position.x);
        }
    }
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].trigger, gate);
    EXPECT_EQ(events[0].body, ball);
    EXPECT_TRUE(events[0].entered);
    EXPECT_FALSE(events[1].entered);
    EXPECT_NEAR(crossings[0], 4.25f, 0.1f);  // touching the gate's near face
    EXPECT_NEAR(crossings[1], 5.75f, 0.1f);  // clear of its far face
    EXPECT_FLOAT_EQ(world.get<RigidBody>(ball).velocity.x, 10.0f);  // went straight through

    // A sphere zone, and a body destroyed while inside.
    const ecs::Entity zone = world.create();
    world.emplace<Transform>(zone).position = Vec3(0.0f, 10.0f, 0.0f);
    world.emplace<SphereCollider>(zone, 2.0f);
    world.emplace<Trigger>(zone);
    RigidBody still;
    still.gravityScale = 0.0f;
    const ecs::Entity resident = addBall(world, Vec3(0.0f, 10.5f, 0.0f), still);
    events.clear();
    step(world, kStep, {}, nullptr, &events);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].body, resident);
    EXPECT_TRUE(events[0].entered);
    world.destroy(resident);
    events.clear();
    step(world, kStep, {}, nullptr, &events);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].entered);
}

TEST(Physics, BoxesFollowTheirRotation) {
    ecs::World world;
    // A 30 degree ramp going down toward +X.
    const f32 tilt = glm::radians(30.0f);
    const ecs::Entity ramp = world.create();
    const Quat rampRotation = glm::angleAxis(-tilt, Vec3(0.0f, 0.0f, 1.0f));
    world.emplace<Transform>(ramp).rotation = rampRotation;  // (references die as pools grow)
    world.emplace<BoxCollider>(ramp, Vec3(5.0f, 0.1f, 2.0f));

    RigidBody body;
    body.restitution = 0.0f;
    body.friction = 0.1f;
    const ecs::Entity ball = addBall(world, Vec3(-2.0f, 2.5f, 0.0f), body, 0.3f);
    simulate(world, 0.8f);
    // It landed on the slope and slides down it, along the surface.
    const Vec3 velocity = world.get<RigidBody>(ball).velocity;
    EXPECT_GT(velocity.x, 0.5f);
    EXPECT_LT(velocity.y, 0.0f);
    const Vec3 slope(std::cos(tilt), -std::sin(tilt), 0.0f);
    EXPECT_NEAR(glm::dot(glm::normalize(velocity), slope), 1.0f, 0.02f);
    // Resting on the surface: its distance to the ramp's plane is its radius.
    const Vec3 up = rampRotation * Vec3(0.0f, 1.0f, 0.0f);
    EXPECT_NEAR(glm::dot(world.get<Transform>(ball).position, up), 0.1f + 0.3f, 0.02f);

    // Rays meet the tilted top face, with its tilted normal.
    const auto hit = raycast(world, Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, ramp);
    EXPECT_NEAR(glm::dot(hit->normal, up), 1.0f, 1e-4f);
    EXPECT_NEAR(hit->point.y, 0.1f / std::cos(tilt), 1e-3f);

    // A rotated trigger covers its rotated volume, not the unrotated one.
    const ecs::Entity zone = world.create();
    Transform& zonePose = world.emplace<Transform>(zone);
    zonePose.position = Vec3(0.0f, 20.0f, 0.0f);
    zonePose.rotation = glm::angleAxis(glm::radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));  // long along Y now
    // (zonePose is not used past this point: pools may move on emplace.)
    world.emplace<BoxCollider>(zone, Vec3(3.0f, 0.5f, 0.5f));
    world.emplace<Trigger>(zone);
    RigidBody floating;
    floating.gravityScale = 0.0f;
    const ecs::Entity insideRotated = addBall(world, Vec3(0.0f, 22.5f, 0.0f), floating, 0.1f);
    const ecs::Entity insideUnrotated = addBall(world, Vec3(2.5f, 20.0f, 0.0f), floating, 0.1f);
    step(world, kStep);
    const std::vector<ecs::Entity>& inside = world.get<Trigger>(zone).inside;
    EXPECT_NE(std::find(inside.begin(), inside.end(), insideRotated), inside.end());
    EXPECT_EQ(std::find(inside.begin(), inside.end(), insideUnrotated), inside.end());
}

TEST(Physics, DampingSlowsBodiesWhateverTheStep) {
    // In free space, velocity decays as exp(-damping * t) for any step.
    for (const f32 dt : {1.0f / 30.0f, 1.0f / 240.0f}) {
        ecs::World world;
        RigidBody body;
        body.velocity = Vec3(4.0f, 0.0f, 0.0f);
        body.gravityScale = 0.0f;
        body.linearDamping = 0.5f;
        const ecs::Entity ball = addBall(world, Vec3(0.0f), body);
        for (int i = 0; i < static_cast<int>(std::round(2.0f / dt)); ++i) {
            step(world, dt);
        }
        EXPECT_NEAR(world.get<RigidBody>(ball).velocity.x, 4.0f * std::exp(-1.0f), 1e-3f);
    }

    // Angular damping brings a rolling ball to rest, still rolling (not
    // sliding) on the way.
    ecs::World world;
    addFloor(world);
    RigidBody body;
    body.velocity = Vec3(5.0f, 0.0f, 0.0f);
    body.angularVelocity = Vec3(0.0f, 0.0f, -10.0f);  // already rolling: v = w r
    body.restitution = 0.0f;
    body.angularDamping = 1.0f;
    const ecs::Entity ball = addBall(world, Vec3(0.0f, 0.5f, 0.0f), body);
    simulate(world, 1.0f);
    const RigidBody& rolling = world.get<RigidBody>(ball);
    EXPECT_LT(rolling.velocity.x, 4.0f);
    EXPECT_NEAR(rolling.angularVelocity.z, -rolling.velocity.x / 0.5f, 0.1f);
    // Only the spin is damped, and a rolling solid sphere keeps 2/7 of its
    // motion in its spin (I / (I + m r^2)), so it slows at 2/7 the rate.
    simulate(world, 10.0f);
    EXPECT_NEAR(world.get<RigidBody>(ball).velocity.x, 5.0f * std::exp(-11.0f * 2.0f / 7.0f), 0.01f);
}

TEST(Physics, RestingBodiesSleepUntilHit) {
    ecs::World world;
    addFloor(world);
    RigidBody body;
    body.angularDamping = 2.0f;
    const ecs::Entity bottom = addBall(world, Vec3(0.0f, 0.5f, 0.0f), body);
    const ecs::Entity top = addBall(world, Vec3(0.02f, 1.6f, 0.0f), body);  // stacked, almost balanced
    simulate(world, 0.2f);
    EXPECT_FALSE(world.get<RigidBody>(bottom).asleep);
    // Prop the top ball so the stack stays: a sleeping bottom ball acts as
    // static, so the top one rests on it and falls asleep in turn.
    world.get<Transform>(top).position.x = 0.0f;
    simulate(world, 2.0f);
    EXPECT_TRUE(world.get<RigidBody>(bottom).asleep);
    EXPECT_TRUE(world.get<RigidBody>(top).asleep);
    const Vec3 rested = world.get<Transform>(top).position;
    simulate(world, 1.0f);
    EXPECT_EQ(world.get<Transform>(top).position, rested);  // not even jitter

    // A ball thrown at the stack wakes what it hits.
    RigidBody thrown;
    thrown.velocity = Vec3(-8.0f, 0.0f, 0.0f);
    thrown.gravityScale = 0.0f;
    addBall(world, Vec3(3.0f, 0.5f, 0.0f), thrown);
    simulate(world, 0.5f);
    EXPECT_FALSE(world.get<RigidBody>(bottom).asleep);
    EXPECT_LT(world.get<Transform>(bottom).position.x, -0.5f);

    // Giving a sleeper a velocity wakes it; so does turning sleep off.
    simulate(world, 5.0f);
    ASSERT_TRUE(world.get<RigidBody>(top).asleep);
    world.get<RigidBody>(top).velocity = Vec3(0.0f, 3.0f, 0.0f);
    step(world, kStep);
    EXPECT_FALSE(world.get<RigidBody>(top).asleep);
    EXPECT_GT(world.get<RigidBody>(top).velocity.y, 2.0f);
}

TEST(Physics, FastBallsDoNotTunnelThroughThinWalls) {
    for (const f32 speed : {30.0f, 200.0f}) {
        ecs::World world;
        const ecs::Entity wall = world.create();
        world.emplace<Transform>(wall).position = Vec3(5.0f, 0.0f, 0.0f);
        world.emplace<BoxCollider>(wall, Vec3(0.05f, 3.0f, 3.0f));  // 10 cm thick
        RigidBody body;
        body.velocity = Vec3(speed, 0.0f, 0.0f);
        body.gravityScale = 0.0f;
        body.restitution = 0.5f;
        const ecs::Entity ball = addBall(world, Vec3(0.0f), body, 0.1f);
        std::vector<Contact> contacts;
        for (int i = 0; i < 30; ++i) {
            step(world, kStep, {}, &contacts);
        }
        EXPECT_LT(world.get<Transform>(ball).position.x, 5.0f) << speed;
        EXPECT_LT(world.get<RigidBody>(ball).velocity.x, 0.0f) << speed;  // bounced back
        ASSERT_FALSE(contacts.empty()) << speed;
        EXPECT_EQ(contacts.front().b, wall);
        EXPECT_NEAR(contacts.front().point.x, 4.95f, 0.02f) << speed;  // on the near face
    }

    // Paths that start in contact or only graze a box keep going.
    ecs::World world;
    addFloor(world);
    const ecs::Entity post = world.create();
    world.emplace<Transform>(post).position = Vec3(5.0f, 1.0f, 1.0f);  // grown, entered mid-step
    world.emplace<BoxCollider>(post, Vec3(0.5f));
    RigidBody fast;
    fast.velocity = Vec3(40.0f, 0.0f, 0.0f);
    fast.friction = 0.0f;
    // Slides along the floor, well clear of the post.
    const ecs::Entity skimmer = addBall(world, Vec3(0.0f, 0.5f, -5.0f), fast, 0.5f);
    // Runs along the post's top front edge (y 1.5, z 0.5), 0.45 m off on
    // both axes: inside the post grown by the radius, yet 0.64 m from it.
    fast.gravityScale = 0.0f;
    const ecs::Entity grazer = addBall(world, Vec3(0.0f, 1.95f, 0.05f), fast, 0.5f);
    for (int i = 0; i < 30; ++i) {
        step(world, kStep);
    }
    for (const ecs::Entity ball : {skimmer, grazer}) {
        EXPECT_NEAR(world.get<Transform>(ball).position.x, 40.0f * 30.0f * kStep, 0.01f);
        EXPECT_FLOAT_EQ(world.get<RigidBody>(ball).velocity.x, 40.0f);
    }
}

TEST(Physics, ClonedTriggersStartEmpty) {
    ecs::World world;
    const ecs::Entity zone = world.create();
    world.emplace<Transform>(zone);
    world.emplace<SphereCollider>(zone, 2.0f);
    world.emplace<Trigger>(zone);
    RigidBody still;
    still.gravityScale = 0.0f;
    const ecs::Entity ball = addBall(world, Vec3(0.5f, 0.0f, 0.0f), still);
    step(world, kStep);
    ASSERT_EQ(world.get<Trigger>(zone).inside.size(), 1u);

    // A copy placed elsewhere: nothing entered it, so nothing may leave it.
    // Assigning a trigger to itself keeps who is inside.
    Trigger& original = world.get<Trigger>(zone);
    Trigger& alias = original;
    original = alias;
    ASSERT_EQ(world.get<Trigger>(zone).inside.size(), 1u);

    const ecs::Entity copy = world.clone(zone);
    world.get<Transform>(copy).position = Vec3(50.0f, 0.0f, 0.0f);
    EXPECT_TRUE(world.get<Trigger>(copy).inside.empty());
    std::vector<TriggerEvent> events;
    step(world, kStep, {}, nullptr, &events);
    EXPECT_TRUE(events.empty());
    // And one placed over the ball sees it enter.
    const ecs::Entity overlapping = world.clone(zone);
    step(world, kStep, {}, nullptr, &events);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].trigger, overlapping);
    EXPECT_EQ(events[0].body, ball);
    EXPECT_TRUE(events[0].entered);
}

TEST(Physics, BrokenCollidersAreLeftOut) {
    // A NaN or infinite pose (a bad scene file, a blown-up simulation) must
    // neither crash the step nor disturb the rest of the world.
    ecs::World world;
    addFloor(world);
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const f32 inf = std::numeric_limits<f32>::infinity();
    const ecs::Entity brokenBox = world.create();
    world.emplace<Transform>(brokenBox).position = Vec3(nan, 0.0f, 0.0f);
    world.emplace<BoxCollider>(brokenBox, Vec3(1.0f));
    const ecs::Entity hugeBox = world.create();
    world.emplace<Transform>(hugeBox).position = Vec3(1e30f, 0.0f, -1e30f);
    world.emplace<BoxCollider>(hugeBox, Vec3(1.0f));
    RigidBody body;
    const ecs::Entity lost = addBall(world, Vec3(0.0f, inf, 0.0f), body);
    body.velocity = Vec3(nan);
    addBall(world, Vec3(2.0f, 1.0f, 0.0f), body);
    const ecs::Entity healthy = addBall(world, Vec3(-2.0f, 3.0f, 0.0f), RigidBody{});

    simulate(world, 3.0f);
    EXPECT_NEAR(world.get<Transform>(healthy).position.y, 0.5f, 0.01f);  // landed as usual
    EXPECT_TRUE(std::isinf(world.get<Transform>(lost).position.y));      // left where it was

    // Rays ignore them too: a NaN box used to be hit by every ray at 0.
    const auto hit = raycast(world, Vec3(-2.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, healthy);
}

namespace {

ecs::Entity addCharacter(ecs::World& world, const Vec3& position, f32 radius = 0.5f) {
    const ecs::Entity character = world.create();
    world.emplace<Transform>(character).position = position;
    world.emplace<SphereCollider>(character, radius);  // no RigidBody: kinematic
    return character;
}

ecs::Entity addBox(ecs::World& world, const Vec3& position, const Vec3& half, const Quat& rotation = Quat(1, 0, 0, 0)) {
    const ecs::Entity box = world.create();
    Transform& pose = world.emplace<Transform>(box);
    pose.position = position;
    pose.rotation = rotation;
    world.emplace<BoxCollider>(box, half);
    return box;
}

}  // namespace

TEST(CharacterMovement, StopsAtWallsAndSlidesAlongThem) {
    ecs::World world;
    addFloor(world);
    addBox(world, Vec3(3.0f, 1.0f, 0.0f), Vec3(0.5f, 1.0f, 5.0f));  // wall facing -X at x = 2.5
    const ecs::Entity hero = addCharacter(world, Vec3(0.0f, 0.5f, 0.0f));

    // Straight into it: stops a skin away, blocked, still on the ground.
    MoveResult result = moveAndSlide(world, hero, Vec3(5.0f, 0.0f, 0.0f));
    EXPECT_TRUE(result.blocked);
    EXPECT_NEAR(world.get<Transform>(hero).position.x, 2.0f, 0.01f);
    EXPECT_NEAR(world.get<Transform>(hero).position.y, 0.5f, 1e-4f);

    // Diagonally: the part along the wall goes through.
    result = moveAndSlide(world, hero, Vec3(1.0f, 0.0f, 2.0f));
    EXPECT_NEAR(world.get<Transform>(hero).position.x, 2.0f, 0.01f);
    EXPECT_NEAR(world.get<Transform>(hero).position.z, 2.0f, 0.01f);

    // Away from it: free.
    result = moveAndSlide(world, hero, Vec3(-1.0f, 0.0f, 0.0f));
    EXPECT_FALSE(result.blocked);
    EXPECT_NEAR(result.moved.x, -1.0f, 1e-5f);
}

TEST(CharacterMovement, LandsOnTheGroundAndJumpsOffIt) {
    ecs::World world;
    addFloor(world);
    const ecs::Entity hero = addCharacter(world, Vec3(0.0f, 3.0f, 0.0f));
    MoveResult result = moveAndSlide(world, hero, Vec3(0.0f, -5.0f, 0.0f));
    EXPECT_TRUE(result.grounded);
    EXPECT_NEAR(result.groundNormal.y, 1.0f, 1e-5f);
    EXPECT_NEAR(world.get<Transform>(hero).position.y, 0.5f, 0.01f);

    // Pressing down while standing: stays, grounded. Walking: moves.
    result = moveAndSlide(world, hero, Vec3(0.3f, -0.1f, 0.0f));
    EXPECT_TRUE(result.grounded);
    EXPECT_NEAR(world.get<Transform>(hero).position.x, 0.3f, 1e-3f);
    EXPECT_NEAR(world.get<Transform>(hero).position.y, 0.5f, 0.01f);

    // Jumping: the ground does not hold it back.
    result = moveAndSlide(world, hero, Vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FALSE(result.blocked);
    EXPECT_FALSE(result.grounded);
    EXPECT_NEAR(world.get<Transform>(hero).position.y, 1.5f, 0.01f);
}

TEST(CharacterMovement, ClimbsGentleSlopesButNotSteepOnes) {
    for (const f32 degrees : {30.0f, 60.0f}) {
        ecs::World world;
        addFloor(world);
        // A slope rising toward +X from x = 1.
        const f32 tilt = glm::radians(degrees);
        const Quat rotation = glm::angleAxis(tilt, Vec3(0.0f, 0.0f, 1.0f));
        addBox(world, Vec3(1.0f, 0.0f, 0.0f) + rotation * Vec3(5.0f, -0.5f, 0.0f), Vec3(5.0f, 0.5f, 5.0f), rotation);
        const ecs::Entity hero = addCharacter(world, Vec3(0.0f, 0.5f, 0.0f));
        f32 highest = 0.5f;
        for (int frame = 0; frame < 60; ++frame) {
            moveAndSlide(world, hero, Vec3(0.1f, 0.0f, 0.0f));  // walk toward the slope
            moveAndSlide(world, hero, Vec3(0.0f, -0.05f, 0.0f));  // gravity
            highest = std::max(highest, world.get<Transform>(hero).position.y);
        }
        if (degrees < 45.0f) {
            EXPECT_GT(highest, 2.0f) << degrees;  // walked up it
        } else {
            EXPECT_LT(highest, 1.0f) << degrees;  // could not
        }
    }
}

TEST(CharacterMovement, BumpsIntoBallsAndPassesTriggers) {
    ecs::World world;
    addFloor(world);
    const ecs::Entity hero = addCharacter(world, Vec3(0.0f, 0.5f, 0.0f));
    addBall(world, Vec3(3.0f, 0.5f, 0.0f), RigidBody{});
    const ecs::Entity zone = addBox(world, Vec3(1.5f, 0.5f, 0.0f), Vec3(0.2f, 2.0f, 2.0f));
    world.emplace<Trigger>(zone);
    const MoveResult result = moveAndSlide(world, hero, Vec3(5.0f, 0.0f, 0.0f));
    EXPECT_TRUE(result.blocked);
    EXPECT_NEAR(world.get<Transform>(hero).position.x, 2.0f, 0.01f);  // touching the ball, through the zone
}

namespace {

ecs::Entity addCrate(ecs::World& world, const Vec3& position, const RigidBody& body = {},
                     const Quat& rotation = Quat(1, 0, 0, 0), const Vec3& half = Vec3(0.5f)) {
    const ecs::Entity crate = world.create();
    Transform& pose = world.emplace<Transform>(crate);
    pose.position = position;
    pose.rotation = rotation;
    world.emplace<BoxCollider>(crate, half);
    world.emplace<RigidBody>(crate, body);
    return crate;
}

/// How closely one of the crate's axes points up (1: lying on a face).
f32 flatness(const Quat& rotation) {
    f32 best = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        Vec3 direction(0.0f);
        direction[axis] = 1.0f;
        best = std::max(best, std::abs(glm::dot(rotation * direction, Vec3(0.0f, 1.0f, 0.0f))));
    }
    return best;
}

}  // namespace

TEST(BoxDynamics, CratesLandAndSettleOnAFace) {
    ecs::World world;
    const ecs::Entity floor = addFloor(world);
    RigidBody body;
    body.restitution = 0.2f;
    const ecs::Entity flat = addCrate(world, Vec3(-3.0f, 3.0f, 0.0f), body);
    // Dropped on a corner, it tumbles onto a face.
    const Quat tilted = glm::angleAxis(0.6f, glm::normalize(Vec3(1.0f, 0.0f, 0.7f)));
    const ecs::Entity tumbling = addCrate(world, Vec3(3.0f, 2.0f, 0.0f), body, tilted);

    std::vector<Contact> contacts;
    for (int i = 0; i < static_cast<int>(4.0f / kStep); ++i) {
        step(world, kStep, {}, &contacts);
    }
    for (const ecs::Entity crate : {flat, tumbling}) {
        const Transform& pose = world.get<Transform>(crate);
        EXPECT_NEAR(pose.position.y, 0.5f, 0.02f);
        EXPECT_GT(flatness(pose.rotation), 0.995f);
        EXPECT_LT(glm::length(world.get<RigidBody>(crate).velocity), 0.05f);
    }
    EXPECT_TRUE(world.get<RigidBody>(flat).asleep);  // at rest, it sleeps
    // Its landing was reported, against the floor, pushing down into it.
    ASSERT_FALSE(contacts.empty());
    EXPECT_EQ(contacts.front().b, floor);
    EXPECT_NEAR(contacts.front().normal.y, -1.0f, 0.05f);
}

TEST(BoxDynamics, FrictionHoldsCratesOnGentleSlopesOnly) {
    // A crate laid on a ramp: it grips at 15 degrees with a rough surface,
    // slides down 35 degrees with a slick one.
    for (const auto& [degrees, friction, shouldSlide] :
         {std::tuple{15.0f, 0.9f, false}, std::tuple{35.0f, 0.1f, true}}) {
        ecs::World world;
        const Quat tilt = glm::angleAxis(glm::radians(degrees), Vec3(0.0f, 0.0f, 1.0f));
        const ecs::Entity ramp = world.create();
        world.emplace<Transform>(ramp).rotation = tilt;
        world.emplace<BoxCollider>(ramp, Vec3(10.0f, 0.5f, 3.0f));
        const Vec3 up = tilt * Vec3(0.0f, 1.0f, 0.0f);
        RigidBody body;
        body.friction = friction;
        body.restitution = 0.0f;
        const Vec3 start = up * 1.005f;  // resting on the surface
        const ecs::Entity crate = addCrate(world, start, body, tilt);
        for (int i = 0; i < static_cast<int>(2.0f / kStep); ++i) {
            step(world, kStep);
        }
        const f32 moved = glm::length(world.get<Transform>(crate).position - start);
        if (shouldSlide) {
            EXPECT_GT(moved, 1.0f) << degrees;
        } else {
            EXPECT_LT(moved, 0.05f) << degrees;
        }
        EXPECT_GT(glm::dot(world.get<Transform>(crate).rotation * Vec3(0.0f, 1.0f, 0.0f), up), 0.99f) << degrees;
    }
}

TEST(BoxDynamics, SpinsFreelyAndBounces) {
    // In free fall a cube keeps its spin and turns by it.
    ecs::World world;
    RigidBody spinning;
    spinning.gravityScale = 0.0f;
    spinning.angularVelocity = Vec3(0.0f, 2.0f, 0.0f);
    const ecs::Entity cube = addCrate(world, Vec3(0.0f, 10.0f, 0.0f), spinning);
    simulate(world, 1.0f);
    EXPECT_NEAR(world.get<RigidBody>(cube).angularVelocity.y, 2.0f, 1e-4f);
    EXPECT_NEAR(glm::angle(world.get<Transform>(cube).rotation), 2.0f, 0.02f);

    // A bouncy crate dropped flat comes back up.
    ecs::World bouncing;
    addFloor(bouncing);
    RigidBody rubber;
    rubber.restitution = 0.8f;
    const ecs::Entity crate = addCrate(bouncing, Vec3(0.0f, 2.5f, 0.0f), rubber);
    f32 highest = 0.0f;
    bool landed = false;
    for (int i = 0; i < static_cast<int>(2.0f / kStep); ++i) {
        step(bouncing, kStep);
        const f32 y = bouncing.get<Transform>(crate).position.y;
        landed |= y < 0.55f;
        if (landed) {
            highest = std::max(highest, y);
        }
    }
    EXPECT_GT(highest, 1.5f);  // fell 2 m, rose over 1 m back
}

TEST(BoxDynamics, CratesRollOffStaticSpheres) {
    ecs::World world;
    addFloor(world);
    const ecs::Entity boulder = world.create();
    world.emplace<Transform>(boulder).position = Vec3(0.0f, 1.0f, 0.0f);
    world.emplace<SphereCollider>(boulder, 1.0f);  // static
    // Dropped slightly off its top: the crate lands on the boulder, tips
    // off it and comes to rest on the floor beside it.
    const ecs::Entity crate = addCrate(world, Vec3(0.4f, 3.5f, 0.0f), RigidBody{}, Quat(1, 0, 0, 0), Vec3(0.3f));
    f32 lowestOverBoulder = 1e9f;
    for (int i = 0; i < static_cast<int>(4.0f / kStep); ++i) {
        step(world, kStep);
        const Vec3 position = world.get<Transform>(crate).position;
        if (std::abs(position.x) < 0.5f) {
            lowestOverBoulder = std::min(lowestOverBoulder, position.y);
        }
    }
    EXPECT_GT(lowestOverBoulder, 1.9f);  // never sank into the boulder
    const Vec3 rest = world.get<Transform>(crate).position;
    EXPECT_NEAR(rest.y, 0.3f, 0.02f);
    EXPECT_GT(rest.x, 0.8f);  // fell off to the side it leaned toward
}

TEST(BoxDynamics, FastCratesDoNotFallThroughThinFloors) {
    // A floor 2 cm thick, as the sandbox's: a crate falling at 15 m/s
    // moves 12 cm per step.
    ecs::World world;
    const ecs::Entity floor = world.create();
    world.emplace<Transform>(floor);
    world.emplace<BoxCollider>(floor, Vec3(20.0f, 0.01f, 20.0f));
    RigidBody body;
    body.velocity = Vec3(2.0f, -15.0f, 0.0f);
    body.angularVelocity = Vec3(3.0f, 0.0f, 5.0f);
    body.restitution = 0.0f;
    const ecs::Entity crate = addCrate(world, Vec3(0.0f, 3.0f, 0.0f), body, Quat(1, 0, 0, 0), Vec3(0.35f));
    f32 lowest = 1e9f;
    for (int i = 0; i < static_cast<int>(3.0f / kStep); ++i) {
        step(world, kStep);
        lowest = std::min(lowest, world.get<Transform>(crate).position.y);
    }
    EXPECT_GT(lowest, 0.2f);
    EXPECT_NEAR(world.get<Transform>(crate).position.y, 0.01f + 0.35f, 0.02f);
    EXPECT_GT(flatness(world.get<Transform>(crate).rotation), 0.995f);
}

TEST(BoxDynamics, CratesStackAndThePileSleeps) {
    ecs::World world;
    addFloor(world);
    RigidBody body;
    body.restitution = 0.0f;
    body.friction = 0.6f;
    std::vector<ecs::Entity> stack;
    for (int level = 0; level < 3; ++level) {
        stack.push_back(addCrate(world, Vec3(0.0f, 0.51f + static_cast<f32>(level) * 1.01f, 0.0f), body));
    }
    simulate(world, 4.0f);
    for (int level = 0; level < 3; ++level) {
        const Transform& pose = world.get<Transform>(stack[static_cast<usize>(level)]);
        EXPECT_NEAR(pose.position.y, 0.5f + static_cast<f32>(level), 0.03f) << level;
        EXPECT_LT(std::abs(pose.position.x) + std::abs(pose.position.z), 0.05f) << level;
        EXPECT_GT(flatness(pose.rotation), 0.999f) << level;
        EXPECT_TRUE(world.get<RigidBody>(stack[static_cast<usize>(level)]).asleep) << level;
    }

    // A crate thrown at the top one knocks it off, waking the pile.
    RigidBody thrown;
    thrown.velocity = Vec3(-12.0f, 0.0f, 0.0f);
    thrown.gravityScale = 0.0f;
    addCrate(world, Vec3(3.0f, 2.5f, 0.0f), thrown, Quat(1, 0, 0, 0), Vec3(0.3f));
    simulate(world, 1.0f);
    EXPECT_FALSE(world.get<RigidBody>(stack[2]).asleep);
    EXPECT_LT(world.get<Transform>(stack[2]).position.x, -0.5f);
}

TEST(BoxDynamics, BallsAndCratesPushEachOther) {
    // A ball rolled hard into a crate shoves it along the floor.
    ecs::World world;
    addFloor(world);
    RigidBody crateBody;
    crateBody.friction = 0.4f;
    const ecs::Entity crate = addCrate(world, Vec3(0.0f, 0.5f, 0.0f), crateBody);
    RigidBody ballBody;
    ballBody.velocity = Vec3(-10.0f, 0.0f, 0.0f);
    ballBody.mass = 2.0f;
    ballBody.restitution = 0.3f;
    const ecs::Entity ball = addBall(world, Vec3(3.0f, 0.5f, 0.0f), ballBody, 0.5f);
    simulate(world, 1.5f);
    EXPECT_LT(world.get<Transform>(crate).position.x, -0.5f);  // shoved
    EXPECT_GT(world.get<Transform>(ball).position.x, world.get<Transform>(crate).position.x + 0.9f);  // not through it

    // Two crates meeting head on in free fall swap their speeds.
    ecs::World space;
    RigidBody left;
    left.gravityScale = 0.0f;
    left.restitution = 1.0f;
    left.velocity = Vec3(3.0f, 0.0f, 0.0f);
    RigidBody right = left;
    right.velocity = Vec3(-3.0f, 0.0f, 0.0f);
    const ecs::Entity a = addCrate(space, Vec3(-2.0f, 0.0f, 0.0f), left);
    const ecs::Entity b = addCrate(space, Vec3(2.0f, 0.0f, 0.0f), right);
    simulate(space, 1.0f);
    const f32 va = space.get<RigidBody>(a).velocity.x;
    const f32 vb = space.get<RigidBody>(b).velocity.x;
    EXPECT_NEAR(va + vb, 0.0f, 1e-3f);  // momentum kept
    EXPECT_NEAR(va, -3.0f, 0.3f);
    EXPECT_NEAR(vb, 3.0f, 0.3f);
    EXPECT_GT(space.get<Transform>(b).position.x - space.get<Transform>(a).position.x, 1.0f);  // never overlapped
}

TEST(BoxDynamics, CratesPassTriggersAndBlockCharacters) {
    ecs::World world;
    addFloor(world);
    // A crate sliding across a gate zone, and a spherical zone it ends in.
    const ecs::Entity gate = world.create();
    world.emplace<Transform>(gate).position = Vec3(3.0f, 1.0f, 0.0f);
    world.emplace<BoxCollider>(gate, Vec3(0.1f, 2.0f, 2.0f));
    world.emplace<Trigger>(gate);
    RigidBody slick;
    slick.friction = 0.0f;
    slick.velocity = Vec3(4.0f, 0.0f, 0.0f);
    const ecs::Entity crate = addCrate(world, Vec3(0.0f, 0.5f, 0.0f), slick);
    std::vector<TriggerEvent> events;
    for (int i = 0; i < static_cast<int>(1.5f / kStep); ++i) {
        step(world, kStep, {}, nullptr, &events);
    }
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].body, crate);
    EXPECT_TRUE(events[0].entered);
    EXPECT_FALSE(events[1].entered);

    const ecs::Entity zone = world.create();
    world.emplace<Transform>(zone).position = world.get<Transform>(crate).position + Vec3(0.0f, 1.2f, 0.0f);
    world.emplace<SphereCollider>(zone, 1.0f);
    world.emplace<Trigger>(zone);
    world.get<RigidBody>(crate).velocity = Vec3(0.0f);
    step(world, kStep);
    EXPECT_EQ(world.get<Trigger>(zone).inside, (std::vector<ecs::Entity>{crate}));

    // A character walking into the crate stops at it.
    const Vec3 at = world.get<Transform>(crate).position;
    const ecs::Entity hero = addCharacter(world, at + Vec3(-3.0f, 0.0f, 0.0f));
    moveAndSlide(world, hero, Vec3(5.0f, 0.0f, 0.0f));
    EXPECT_NEAR(world.get<Transform>(hero).position.x, at.x - 1.0f, 0.02f);
}

TEST(BoxDynamics, CratesMeetingEdgeToEdgeDoNotSinkIn) {
    // A static box turned 45 degrees about Z shows an edge along Z toward
    // +X; a crate turned 45 degrees about Y shows an edge along Y toward -X.
    // Thrown at it, they cross edge on edge, where no corner of either is
    // inside the other.
    ecs::World world;
    const ecs::Entity post = world.create();
    world.emplace<Transform>(post).rotation = glm::angleAxis(glm::radians(45.0f), Vec3(0.0f, 0.0f, 1.0f));
    world.emplace<BoxCollider>(post, Vec3(0.5f));
    RigidBody body;
    body.gravityScale = 0.0f;
    body.restitution = 0.5f;
    body.velocity = Vec3(-3.0f, 0.0f, 0.0f);
    const ecs::Entity crate =
        addCrate(world, Vec3(3.0f, 0.0f, 0.0f), body, glm::angleAxis(glm::radians(45.0f), Vec3(0.0f, 1.0f, 0.0f)));
    // The edges touch when the centers are 2 x 0.707 apart.
    f32 closest = 1e9f;
    for (int i = 0; i < static_cast<int>(1.5f / kStep); ++i) {
        step(world, kStep);
        closest = std::min(closest, world.get<Transform>(crate).position.x);
    }
    EXPECT_GT(closest, 2.0f * 0.7071f - 0.03f);
    EXPECT_GT(world.get<RigidBody>(crate).velocity.x, 0.5f);  // bounced back
}
