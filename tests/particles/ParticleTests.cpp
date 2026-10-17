#include <bdex/particles/Particles.hpp>
#include <bdex/scene/Components.hpp>

#include <gtest/gtest.h>

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
