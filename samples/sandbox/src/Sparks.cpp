#include "Sparks.hpp"

#include <phi/particles/Particles.hpp>
#include <phi/physics/Physics.hpp>
#include <phi/scene/Scene.hpp>

#include <algorithm>
#include <numbers>

namespace sandbox {

using namespace phi;

void Sparks::start(Engine& engine) {
    collisions_ = engine.events().subscribe<physics::Collision>([this, &engine](const physics::Collision& event) {
        const physics::Contact& contact = event.contact;
        auto* scenes = engine.services().tryGet<scene::SceneManager>();
        if (!enabled || contact.impulse < 3.0f || bursts_.size() >= kMaxBursts || scenes == nullptr || scenes->active() == nullptr) {
            return;
        }
        scene::Scene& level = *scenes->active();
        ecs::World& world = level.world();
        const ecs::Entity burst = world.create();
        world.emplace<scene::Transform>(burst).position = contact.point;
        world.emplace<scene::Transient>(burst);  // an effect, not part of the level
        particles::ParticleEmitter sparks;
        sparks.rate = 0.0f;
        sparks.burst = static_cast<u32>(std::clamp(contact.impulse * 3.0f, 6.0f, 60.0f));
        sparks.direction = -contact.normal;  // off the surface struck
        sparks.spread = glm::radians(65.0f);
        sparks.speed = 3.0f;
        sparks.speedVariance = 0.5f;
        sparks.drag = 1.5f;
        sparks.lifetime = 0.45f;
        sparks.lifetimeVariance = 0.4f;
        // Bright enough to glow under bloom, cooling to a dark red.
        sparks.startColor = Vec4(3.0f, 1.3f, 0.3f, 1.0f);
        sparks.endColor = Vec4(1.0f, 0.15f, 0.0f, 0.0f);
        sparks.startSize = 0.06f;
        sparks.endSize = 0.02f;
        sparks.seed = ++seed_ * 2654435761u;  // each burst its own scatter
        world.emplace<particles::ParticleEmitter>(burst, std::move(sparks));
        bursts_.push_back(burst);
    });
}

void Sparks::addTrail(ecs::World& world, ecs::Entity body) {
    particles::ParticleEmitter trail;
    trail.rate = 240.0f;  // spread along the way within each frame: no clumps
    trail.emitting = false;
    trail.spread = std::numbers::pi_v<f32>;  // every way, slowly
    trail.speed = 0.2f;
    trail.inheritVelocity = 0.15f;  // a little of the ball's run
    trail.gravity = Vec3(0.0f);
    trail.drag = 2.0f;
    trail.lifetime = 0.35f;
    trail.lifetimeVariance = 0.3f;
    trail.emitRadius = 0.1f;
    // Blended rather than added: light would vanish against the bright sky.
    trail.additive = false;
    trail.startColor = Vec4(1.0f, 0.55f, 0.25f, 0.55f);
    trail.endColor = Vec4(0.9f, 0.85f, 0.8f, 0.0f);
    trail.startSize = 0.2f;
    trail.endSize = 0.05f;
    trail.seed = body.index * 2654435761u + 1u;
    world.emplace<particles::ParticleEmitter>(body, std::move(trail));
}

void Sparks::update(ecs::World& world) {
    world.view<const physics::RigidBody, particles::ParticleEmitter>().each(
        [&](const physics::RigidBody& body, particles::ParticleEmitter& trail) {
            trail.emitting = enabled && glm::length(body.velocity) > kTrailSpeed;
        });
    std::erase_if(bursts_, [&](ecs::Entity burst) {
        const auto* emitter = world.tryGet<particles::ParticleEmitter>(burst);
        if (emitter != nullptr && (emitter->burst > 0 || !emitter->particles.empty())) {
            return false;
        }
        world.destroy(burst);  // spent (or gone with a scene load)
        return true;
    });
}

}  // namespace sandbox
