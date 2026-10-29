#include "Sparks.hpp"

#include <bdex/particles/Particles.hpp>
#include <bdex/physics/Physics.hpp>
#include <bdex/scene/Scene.hpp>

#include <algorithm>

namespace sandbox {

using namespace bdex;

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

void Sparks::update(ecs::World& world) {
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
