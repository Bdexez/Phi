#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/ecs/World.hpp>
#include <bdex/scene/Serialization.hpp>

#include <algorithm>
#include <vector>

namespace bdex::particles {

/// One live particle, in world space.
struct Particle {
    Vec3 position{0.0f};
    Vec3 velocity{0.0f};
    f32 age = 0.0f;       ///< Seconds since it was emitted.
    f32 lifetime = 1.0f;  ///< Seconds it lives.
};

/// Emits particles from its entity's world position: sparks, smoke, dust.
/// Particles live in world space, so moving the emitter leaves a trail.
/// updateParticles moves them; a renderer draws them as camera-facing
/// quads, colored and sized by their age.
struct ParticleEmitter {
    // --- Emission ------------------------------------------------------------
    f32 rate = 20.0f;          ///< Particles per second, continuously (0: bursts only).
    u32 burst = 0;             ///< Emitted all at once on the next update, then reset.
    u32 maxParticles = 1000;   ///< Beyond this, new ones are not emitted.
    f32 lifetime = 1.5f;       ///< Seconds; each varies by +/- lifetimeVariance of it.
    f32 lifetimeVariance = 0.25f;
    Vec3 direction{0.0f, 1.0f, 0.0f};  ///< Center of the cone they leave in, in world space...
    /// ...or in the emitter's own frame, turning with it: an exhaust, a
    /// hose, a gun's muzzle.
    bool localDirection = false;
    f32 spread = glm::radians(25.0f);  ///< Half-angle of that cone.
    f32 speed = 2.0f;                  ///< Meters per second; varies by +/- speedVariance of it.
    f32 speedVariance = 0.3f;
    f32 emitRadius = 0.0f;             ///< Born anywhere within this ball around the emitter.
    /// Share of the emitter's own velocity its particles leave with: 0
    /// leaves them where they were born, 1 carries them along with it.
    f32 inheritVelocity = 0.0f;

    // --- Motion --------------------------------------------------------------
    Vec3 gravity{0.0f, -9.81f, 0.0f};  ///< Smoke wants it small or upward.
    f32 drag = 0.0f;                   ///< Velocity decays as exp(-drag * t).

    // --- Look ----------------------------------------------------------------
    Vec4 startColor{1.0f};  ///< rgb (may exceed 1 to glow with bloom), alpha.
    Vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
    f32 startSize = 0.1f;  ///< Meters across.
    f32 endSize = 0.1f;
    bool additive = true;  ///< Adds light (sparks, fire); else blends (smoke).
    bool emitting = true;

    // --- Runtime, kept by updateParticles -----------------------------------
    std::vector<Particle> particles;
    f32 pending = 0.0f;  ///< Fraction of a particle carried to the next update.
    /// Where the emitter was at the last update: particles of a frame are
    /// born along the way from there, so a fast emitter leaves an even
    /// trail. Clear hasPreviousOrigin after teleporting it, or the trail
    /// streaks across the jump.
    Vec3 previousOrigin{0.0f};
    bool hasPreviousOrigin = false;
    u32 seed = 1;        ///< Advances with each random draw: emitters replay identically.
};

/// A particle's look at its age.
[[nodiscard]] inline Vec4 colorAt(const ParticleEmitter& emitter, const Particle& particle) {
    const f32 t = particle.lifetime > 0.0f ? std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f) : 1.0f;
    return glm::mix(emitter.startColor, emitter.endColor, t);
}
[[nodiscard]] inline f32 sizeAt(const ParticleEmitter& emitter, const Particle& particle) {
    const f32 t = particle.lifetime > 0.0f ? std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f) : 1.0f;
    return glm::mix(emitter.startSize, emitter.endSize, t);
}

/// Ages and moves every emitter's particles by `dt` seconds, drops the
/// expired, and emits new ones from the emitter's world position
/// (WorldTransform if present, else Transform): a burst all at once where
/// it stands, its rate spread over the frame, each particle born where the
/// emitter was at that instant and already aged since.
void updateParticles(ecs::World& world, f32 dt);

/// Registers ParticleEmitter with scene files: its settings and seed, not
/// its live particles (a loaded emitter starts empty, and bursts again if
/// it was saved with one pending).
void addComponentSerializers(scene::ComponentSerializers& serializers);

}  // namespace bdex::particles
