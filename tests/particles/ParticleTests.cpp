#include <bdex/particles/Particles.hpp>
#include <bdex/scene/Components.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace bdex;
using namespace bdex::particles;

namespace {

ecs::Entity addEmitter(ecs::World& world, const Vec3& at, const ParticleEmitter& settings) {
    const ecs::Entity entity = world.create();
    world.emplace<scene::Transform>(entity).position = at;
    world.emplace<ParticleEmitter>(entity, settings);
    return entity;
}

void run(ecs::World& world, f32 seconds, f32 dt = 0.01f) {
    for (int i = 0; i < static_cast<int>(std::round(seconds / dt)); ++i) {
        updateParticles(world, dt);
    }
}

}  // namespace

TEST(Particles, EmitAtTheirRateAndInBursts) {
    ecs::World world;
    ParticleEmitter steady;
    steady.rate = 10.0f;
    steady.lifetime = 100.0f;
    const ecs::Entity stream = addEmitter(world, Vec3(0.0f), steady);
    run(world, 1.0f);
    EXPECT_EQ(world.get<ParticleEmitter>(stream).particles.size(), 10u);

    // A burst comes out at once, bounded by the maximum.
    ParticleEmitter sparks;
    sparks.rate = 0.0f;
    sparks.burst = 50;
    sparks.maxParticles = 30;
    sparks.lifetime = 100.0f;
    const ecs::Entity pop = addEmitter(world, Vec3(0.0f), sparks);
    updateParticles(world, 0.01f);
    EXPECT_EQ(world.get<ParticleEmitter>(pop).particles.size(), 30u);
    EXPECT_EQ(world.get<ParticleEmitter>(pop).burst, 0u);

    // A huge frame emits at most the maximum, not a flood.
    world.get<ParticleEmitter>(stream).maxParticles = 100;
    updateParticles(world, 1000.0f);
    EXPECT_LE(world.get<ParticleEmitter>(stream).particles.size(), 100u);
}

TEST(Particles, LeaveInTheirConeFallAndExpire) {
    ecs::World world;
    ParticleEmitter fountain;
    fountain.rate = 0.0f;
    fountain.burst = 500;
    fountain.direction = Vec3(1.0f, 0.0f, 0.0f);
    fountain.spread = glm::radians(20.0f);
    fountain.speed = 4.0f;
    fountain.speedVariance = 0.25f;
    fountain.lifetime = 1.0f;
    fountain.lifetimeVariance = 0.2f;
    fountain.gravity = Vec3(0.0f);
    const ecs::Entity entity = addEmitter(world, Vec3(0.0f, 5.0f, 0.0f), fountain);
    updateParticles(world, 0.0f);
    const ParticleEmitter& emitter = world.get<ParticleEmitter>(entity);
    ASSERT_EQ(emitter.particles.size(), 500u);
    f32 widest = 0.0f;
    for (const Particle& particle : emitter.particles) {
        EXPECT_EQ(particle.position, Vec3(0.0f, 5.0f, 0.0f));
        const f32 speed = glm::length(particle.velocity);
        EXPECT_GE(speed, 3.0f - 1e-4f);
        EXPECT_LE(speed, 5.0f + 1e-4f);
        widest = std::max(widest, std::acos(std::clamp(particle.velocity.x / speed, -1.0f, 1.0f)));
        EXPECT_GE(particle.lifetime, 0.8f - 1e-4f);
        EXPECT_LE(particle.lifetime, 1.2f + 1e-4f);
    }
    EXPECT_LE(widest, glm::radians(20.0f) + 1e-3f);
    EXPECT_GT(widest, glm::radians(15.0f));  // the cone is filled, not just its axis

    // All gone once the longest lifetime has passed.
    run(world, 1.25f);
    EXPECT_TRUE(world.get<ParticleEmitter>(entity).particles.empty());

    // Gravity bends a straight-up shot as it should.
    ParticleEmitter shot;
    shot.rate = 0.0f;
    shot.burst = 1;
    shot.spread = 0.0f;
    shot.speed = 5.0f;
    shot.speedVariance = 0.0f;
    shot.lifetime = 10.0f;
    const ecs::Entity cannon = addEmitter(world, Vec3(0.0f), shot);
    updateParticles(world, 0.0f);
    run(world, 0.5f, 0.001f);
    EXPECT_NEAR(world.get<ParticleEmitter>(cannon).particles[0].position.y, 5.0f * 0.5f - 0.5f * 9.81f * 0.25f, 0.01f);
}

TEST(Particles, ReplayIdenticallyAndChangeWithAge) {
    ecs::World a;
    ecs::World b;
    ParticleEmitter smoke;
    smoke.rate = 30.0f;
    smoke.emitRadius = 0.5f;
    smoke.seed = 42;
    const ecs::Entity first = addEmitter(a, Vec3(1.0f, 2.0f, 3.0f), smoke);
    const ecs::Entity second = addEmitter(b, Vec3(1.0f, 2.0f, 3.0f), smoke);
    run(a, 1.0f);
    run(b, 1.0f);
    const auto& left = a.get<ParticleEmitter>(first).particles;
    const auto& right = b.get<ParticleEmitter>(second).particles;
    ASSERT_EQ(left.size(), right.size());
    for (usize i = 0; i < left.size(); ++i) {
        EXPECT_EQ(left[i].position, right[i].position);
    }

    ParticleEmitter look;
    look.startColor = Vec4(4.0f, 2.0f, 0.5f, 1.0f);
    look.endColor = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    look.startSize = 0.2f;
    look.endSize = 0.0f;
    Particle halfway;
    halfway.lifetime = 2.0f;
    halfway.age = 1.0f;
    EXPECT_EQ(colorAt(look, halfway), Vec4(2.0f, 1.0f, 0.25f, 0.5f));
    EXPECT_FLOAT_EQ(sizeAt(look, halfway), 0.1f);
}

TEST(Particles, AMovingEmitterLeavesAnEvenTrail) {
    ParticleEmitter trail;
    trail.rate = 95.0f;  // not a whole number per frame
    trail.speed = 0.0f;
    trail.speedVariance = 0.0f;
    trail.gravity = Vec3(0.0f);
    trail.lifetime = 100.0f;
    trail.lifetimeVariance = 0.0f;

    for (const f32 inherit : {0.0f, 1.0f}) {
        ecs::World world;
        trail.inheritVelocity = inherit;
        const ecs::Entity emitter = addEmitter(world, Vec3(0.0f), trail);
        updateParticles(world, 0.1f);  // standing still: where it stands
        const usize first = world.get<ParticleEmitter>(emitter).particles.size();
        for (const Particle& particle : world.get<ParticleEmitter>(emitter).particles) {
            EXPECT_EQ(particle.position, Vec3(0.0f));
        }

        // Moving at 10 m/s along X for a frame.
        world.get<scene::Transform>(emitter).position.x = 1.0f;
        updateParticles(world, 0.1f);
        const auto& particles = world.get<ParticleEmitter>(emitter).particles;
        ASSERT_GE(particles.size(), first + 9);
        f32 oldest = 0.0f;
        f32 youngest = 1.0f;
        for (usize i = first; i < particles.size(); ++i) {
            const Particle& particle = particles[i];
            ASSERT_GE(particle.age, 0.0f);
            ASSERT_LE(particle.age, 0.1f);
            oldest = std::max(oldest, particle.age);
            youngest = std::min(youngest, particle.age);
            if (inherit == 0.0f) {
                // Left where the emitter was when it was born.
                EXPECT_NEAR(particle.position.x, 1.0f - 10.0f * particle.age, 1e-4f) << i;
            } else {
                // Carried along: still level with it.
                EXPECT_NEAR(particle.position.x, 1.0f, 1e-4f) << i;
                EXPECT_NEAR(particle.velocity.x, 10.0f, 1e-3f) << i;
            }
        }
        EXPECT_GT(oldest - youngest, 0.08f);  // births spread over the frame
    }

    // Teleported, with the trail cleared: nothing streaks across the jump.
    ecs::World world;
    trail.inheritVelocity = 0.0f;
    const ecs::Entity emitter = addEmitter(world, Vec3(0.0f), trail);
    updateParticles(world, 0.1f);
    world.get<scene::Transform>(emitter).position.x = 50.0f;
    world.get<ParticleEmitter>(emitter).hasPreviousOrigin = false;
    world.get<ParticleEmitter>(emitter).particles.clear();
    updateParticles(world, 0.1f);
    for (const Particle& particle : world.get<ParticleEmitter>(emitter).particles) {
        EXPECT_EQ(particle.position.x, 50.0f);
    }
}

TEST(Particles, EmittersRoundTripThroughSceneFilesWithoutTheirParticles) {
    ecs::World world;
    ParticleEmitter fire;  // every setting off its default
    fire.rate = 33.5f;
    fire.burst = 7;
    fire.maxParticles = 250;
    fire.lifetime = 0.75f;
    fire.lifetimeVariance = 0.1f;
    fire.direction = Vec3(0.0f, 0.0f, 1.0f);
    fire.localDirection = true;
    fire.spread = 0.4f;
    fire.speed = 6.0f;
    fire.speedVariance = 0.05f;
    fire.emitRadius = 0.3f;
    fire.inheritVelocity = 0.5f;
    fire.gravity = Vec3(0.0f, 1.5f, 0.0f);
    fire.drag = 2.0f;
    fire.startColor = Vec4(3.0f, 1.2f, 0.2f, 1.0f);
    fire.endColor = Vec4(0.2f, 0.0f, 0.0f, 0.25f);
    fire.startSize = 0.3f;
    fire.endSize = 0.05f;
    fire.additive = false;
    fire.emitting = false;
    fire.seed = 4000000000u;
    const ecs::Entity entity = addEmitter(world, Vec3(0.0f), fire);
    world.get<ParticleEmitter>(entity).particles.resize(12);  // live state: not saved

    scene::ComponentSerializers serializers = scene::ComponentSerializers::withSceneComponents();
    addComponentSerializers(serializers);
    const std::string text = serializers.save(world);
    ecs::World loaded;
    ASSERT_TRUE(serializers.load(loaded, text).has_value());
    const ParticleEmitter* copy = nullptr;
    loaded.view<const ParticleEmitter>().each([&](const ParticleEmitter& emitter) { copy = &emitter; });
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->rate, fire.rate);
    EXPECT_EQ(copy->burst, fire.burst);
    EXPECT_EQ(copy->maxParticles, fire.maxParticles);
    EXPECT_EQ(copy->lifetime, fire.lifetime);
    EXPECT_EQ(copy->lifetimeVariance, fire.lifetimeVariance);
    EXPECT_EQ(copy->direction, fire.direction);
    EXPECT_EQ(copy->localDirection, fire.localDirection);
    EXPECT_EQ(copy->spread, fire.spread);
    EXPECT_EQ(copy->speed, fire.speed);
    EXPECT_EQ(copy->speedVariance, fire.speedVariance);
    EXPECT_EQ(copy->emitRadius, fire.emitRadius);
    EXPECT_EQ(copy->inheritVelocity, fire.inheritVelocity);
    EXPECT_EQ(copy->gravity, fire.gravity);
    EXPECT_EQ(copy->drag, fire.drag);
    EXPECT_EQ(copy->startColor, fire.startColor);
    EXPECT_EQ(copy->endColor, fire.endColor);
    EXPECT_EQ(copy->startSize, fire.startSize);
    EXPECT_EQ(copy->endSize, fire.endSize);
    EXPECT_EQ(copy->additive, fire.additive);
    EXPECT_EQ(copy->emitting, fire.emitting);
    EXPECT_EQ(copy->seed, fire.seed);
    EXPECT_TRUE(copy->particles.empty());

}

TEST(Particles, LocalDirectionsTurnWithTheEmitter) {
    ParticleEmitter nozzle;
    nozzle.rate = 0.0f;
    nozzle.burst = 20;
    nozzle.direction = Vec3(1.0f, 0.0f, -1.0f);  // ahead and to the right, in its own frame
    nozzle.spread = 0.0f;
    nozzle.speed = 3.0f;
    nozzle.speedVariance = 0.0f;
    nozzle.gravity = Vec3(0.0f);

    for (const bool local : {false, true}) {
        ecs::World world;
        nozzle.localDirection = local;
        const ecs::Entity entity = addEmitter(world, Vec3(0.0f), nozzle);
        // Turned a quarter to the left (ahead becomes -X, right becomes -Z),
        // and squashed unevenly, which must not bend the cone: through a
        // WorldTransform, as a child's would be.
        const Quat turn = glm::angleAxis(glm::radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
        world.emplace<scene::WorldTransform>(entity).matrix =
            glm::translate(Mat4(1.0f), Vec3(2.0f, 0.0f, 0.0f)) * glm::mat4_cast(turn) *
            glm::scale(Mat4(1.0f), Vec3(5.0f, 1.0f, 0.2f));
        updateParticles(world, 0.0f);
        const Vec3 expected = (local ? Vec3(-1.0f, 0.0f, -1.0f) : Vec3(1.0f, 0.0f, -1.0f)) * (3.0f / std::sqrt(2.0f));
        for (const Particle& particle : world.get<ParticleEmitter>(entity).particles) {
            EXPECT_NEAR(glm::length(particle.velocity - expected), 0.0f, 1e-4f) << local;
            EXPECT_EQ(particle.position, Vec3(2.0f, 0.0f, 0.0f));
        }
    }

    // Without a WorldTransform, the Transform's rotation turns it.
    ecs::World world;
    nozzle.direction = Vec3(0.0f, 0.0f, -1.0f);
    nozzle.localDirection = true;
    const ecs::Entity entity = addEmitter(world, Vec3(0.0f), nozzle);
    world.get<scene::Transform>(entity).rotation = glm::angleAxis(glm::radians(-90.0f), Vec3(1.0f, 0.0f, 0.0f));
    updateParticles(world, 0.0f);
    EXPECT_NEAR(glm::length(world.get<ParticleEmitter>(entity).particles[0].velocity - Vec3(0.0f, -3.0f, 0.0f)), 0.0f,
                1e-4f);
}
