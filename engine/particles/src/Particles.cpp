#include <phi/particles/Particles.hpp>

#include <phi/scene/Components.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phi::particles {

namespace {

/// Uniform in [0, 1), from the emitter's own seed (xorshift).
f32 random(u32& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<f32>(seed >> 8) / static_cast<f32>(1u << 24);
}

/// A unit direction within `spread` of `axis`, uniform over the cone's cap.
Vec3 inCone(const Vec3& axis, f32 spread, u32& seed) {
    const f32 length = glm::length(axis);
    const Vec3 up = length > 1e-6f ? axis / length : Vec3(0.0f, 1.0f, 0.0f);
    const f32 cosine = 1.0f - random(seed) * (1.0f - std::cos(std::clamp(spread, 0.0f, std::numbers::pi_v<f32>)));
    const f32 sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
    const f32 turn = random(seed) * 2.0f * std::numbers::pi_v<f32>;
    const Vec3 helper = std::abs(up.y) < 0.99f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 side = glm::normalize(glm::cross(helper, up));
    const Vec3 other = glm::cross(up, side);
    return up * cosine + (side * std::cos(turn) + other * std::sin(turn)) * sine;
}

/// Emits one particle at `origin` that has lived `age` seconds already
/// (born during the frame), leaving with `carried` on top of its own speed.
/// How many particles the emitter may hold.
usize capacityOf(const ParticleEmitter& emitter) {
    return std::min(emitter.maxParticles, kParticleLimit);
}

void emit(ParticleEmitter& emitter, const Vec3& origin, const Vec3& axis, const Vec3& carried, f32 age) {
    if (emitter.particles.size() >= capacityOf(emitter)) {
        return;
    }
    u32& seed = emitter.seed;
    if (seed == 0) {
        seed = 1;  // xorshift's one stuck state
    }
    Particle particle;
    particle.position = origin;
    if (emitter.emitRadius > 0.0f) {
        particle.position += inCone(Vec3(0.0f, 1.0f, 0.0f), std::numbers::pi_v<f32>, seed) *
                             (emitter.emitRadius * std::cbrt(random(seed)));
    }
    const f32 speed = emitter.speed * (1.0f + emitter.speedVariance * (2.0f * random(seed) - 1.0f));
    particle.velocity = inCone(axis, emitter.spread, seed) * speed + carried;
    particle.lifetime =
        std::max(emitter.lifetime * (1.0f + emitter.lifetimeVariance * (2.0f * random(seed) - 1.0f)), 1e-3f);
    if (age > 0.0f) {
        if (age >= particle.lifetime) {
            return;  // born and gone within the frame
        }
        // Catch up on the time since its birth, as the update would have.
        particle.age = age;
        const f32 keep = emitter.drag > 0.0f ? std::exp(-emitter.drag * age) : 1.0f;
        particle.velocity = (particle.velocity + emitter.gravity * age) * keep;
        particle.position += particle.velocity * age;
    }
    emitter.particles.push_back(particle);
}

}  // namespace

void updateParticles(ecs::World& world, f32 dt) {
    if (!(dt >= 0.0f) || !std::isfinite(dt)) {
        return;
    }
    world.view<ParticleEmitter>().each([&](ecs::Entity entity, ParticleEmitter& emitter) {
        // Age and move; the expired go (order does not matter for drawing,
        // so swap with the last).
        const f32 keep = emitter.drag > 0.0f ? std::exp(-emitter.drag * dt) : 1.0f;
        for (usize i = 0; i < emitter.particles.size();) {
            Particle& particle = emitter.particles[i];
            particle.age += dt;
            if (particle.age >= particle.lifetime) {
                particle = emitter.particles.back();
                emitter.particles.pop_back();
                continue;
            }
            particle.velocity = (particle.velocity + emitter.gravity * dt) * keep;
            particle.position += particle.velocity * dt;
            ++i;
        }

        Vec3 origin(0.0f);
        Vec3 axis = emitter.direction;
        if (const auto* placed = world.tryGet<scene::WorldTransform>(entity)) {
            origin = Vec3(placed->matrix[3]);
            if (emitter.localDirection) {
                // Rotation only: scale (even uneven) must not bend the cone.
                const Mat3 frame(placed->matrix);
                axis = Mat3(glm::normalize(frame[0]), glm::normalize(frame[1]), glm::normalize(frame[2])) * axis;
            }
        } else if (const auto* local = world.tryGet<scene::Transform>(entity)) {
            origin = local->position;
            if (emitter.localDirection) {
                axis = local->rotation * axis;
            }
        }
        // The way the emitter came this frame, and how fast.
        const Vec3 from = emitter.hasPreviousOrigin ? emitter.previousOrigin : origin;
        const Vec3 carried =
            dt > 0.0f ? (origin - from) / dt * (std::isfinite(emitter.inheritVelocity) ? emitter.inheritVelocity : 0.0f)
                      : Vec3(0.0f);
        emitter.previousOrigin = origin;
        emitter.hasPreviousOrigin = true;

        // A burst emits what fits, then is spent: asking for four billion
        // must not loop four billion times.
        const usize room = capacityOf(emitter) - std::min(emitter.particles.size(), capacityOf(emitter));
        const usize burst = std::min<usize>(emitter.burst, room);
        emitter.burst = 0;
        for (usize i = 0; i < burst; ++i) {
            emit(emitter, origin, axis, carried, 0.0f);
        }
        if (emitter.emitting && emitter.rate > 0.0f && std::isfinite(emitter.rate)) {
            const f32 before = emitter.pending;
            emitter.pending += emitter.rate * dt;
            // Bounded: a long frame should not emit thousands in one go.
            emitter.pending = std::min(emitter.pending, static_cast<f32>(capacityOf(emitter)));
            for (u32 count = 1; emitter.pending >= 1.0f; emitter.pending -= 1.0f, ++count) {
                // The count reached `count` this many seconds into the frame:
                // the particle is born there, on the emitter's way.
                const f32 born = std::clamp((static_cast<f32>(count) - before) / emitter.rate, 0.0f, dt);
                const f32 along = dt > 0.0f ? born / dt : 1.0f;
                // from + (origin - from) * along, unlike mix, is exact for a still emitter.
                emit(emitter, from + (origin - from) * along, axis, carried, dt - born);
            }
        }
    });
}

void addComponentSerializers(scene::ComponentSerializers& serializers) {
    serializers.add<ParticleEmitter>(
        "ParticleEmitter",
        [](const ParticleEmitter& emitter, scene::Fields& out, const scene::EntityRefs&) {
            using scene::formatField;
            out["rate"] = formatField(emitter.rate);
            out["burst"] = formatField(emitter.burst);
            out["max_particles"] = formatField(emitter.maxParticles);
            out["lifetime"] = formatField(emitter.lifetime);
            out["lifetime_variance"] = formatField(emitter.lifetimeVariance);
            out["direction"] = formatField(emitter.direction);
            out["local_direction"] = formatField(emitter.localDirection);
            out["spread"] = formatField(emitter.spread);
            out["speed"] = formatField(emitter.speed);
            out["speed_variance"] = formatField(emitter.speedVariance);
            out["emit_radius"] = formatField(emitter.emitRadius);
            out["inherit_velocity"] = formatField(emitter.inheritVelocity);
            out["gravity"] = formatField(emitter.gravity);
            out["drag"] = formatField(emitter.drag);
            out["start_color"] = formatField(emitter.startColor);
            out["end_color"] = formatField(emitter.endColor);
            out["start_size"] = formatField(emitter.startSize);
            out["end_size"] = formatField(emitter.endSize);
            out["additive"] = formatField(emitter.additive);
            out["emitting"] = formatField(emitter.emitting);
            out["seed"] = formatField(emitter.seed);
        },
        [](ParticleEmitter& emitter, const scene::Fields& in, const scene::EntityRefs&) -> Result<> {
            using scene::parseField;
            for (auto parsed : {parseField(in, "rate", emitter.rate), parseField(in, "burst", emitter.burst),
                                parseField(in, "max_particles", emitter.maxParticles),
                                parseField(in, "lifetime", emitter.lifetime),
                                parseField(in, "lifetime_variance", emitter.lifetimeVariance),
                                parseField(in, "direction", emitter.direction),
                                parseField(in, "local_direction", emitter.localDirection),
                                parseField(in, "spread", emitter.spread),
                                parseField(in, "speed", emitter.speed),
                                parseField(in, "speed_variance", emitter.speedVariance),
                                parseField(in, "emit_radius", emitter.emitRadius),
                                parseField(in, "inherit_velocity", emitter.inheritVelocity),
                                parseField(in, "gravity", emitter.gravity), parseField(in, "drag", emitter.drag),
                                parseField(in, "start_color", emitter.startColor),
                                parseField(in, "end_color", emitter.endColor),
                                parseField(in, "start_size", emitter.startSize),
                                parseField(in, "end_size", emitter.endSize),
                                parseField(in, "additive", emitter.additive),
                                parseField(in, "emitting", emitter.emitting), parseField(in, "seed", emitter.seed)}) {
                if (!parsed) {
                    return parsed;
                }
            }
            return {};
        });
}

}  // namespace phi::particles
