#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/ecs/World.hpp>
#include <bdex/scene/Serialization.hpp>

#include <optional>
#include <vector>

namespace bdex::physics {

// --- Components -----------------------------------------------------------------
// Bodies live in world space: their entity's Transform is their pose, so
// they must not have a Parent. Rotation is not simulated.

/// Makes an entity with a collider move under gravity and collisions.
struct RigidBody {
    Vec3 velocity{0.0f};
    f32 mass = 1.0f;
    /// Bounciness: 0 stops dead on impact, 1 bounces back at full speed.
    f32 restitution = 0.4f;
    /// Coulomb friction coefficient against sliding.
    f32 friction = 0.4f;
    f32 gravityScale = 1.0f;

    /// Set by step(): position at the start of the last step, for
    /// interpolate(). Not meant to be written.
    Vec3 previousPosition{0.0f};
    bool hasPrevious = false;
};

/// Sphere centered on the entity, scaled by the largest Transform scale.
struct SphereCollider {
    f32 radius = 0.5f;
};

/// Axis-aligned box centered on the entity, scaled by its Transform (its
/// rotation is ignored). Without a RigidBody it is static: floors, walls...
struct BoxCollider {
    Vec3 halfExtents{0.5f};
};

/// Turns a static collider (box or sphere, without RigidBody) into a zone
/// that bodies pass through, reporting when they enter and leave it:
/// finish lines, damage zones, checkpoints.
struct Trigger {
    /// Bodies inside after the last step (maintained by step()).
    std::vector<ecs::Entity> inside;
};

/// A body entering or leaving a Trigger.
struct TriggerEvent {
    ecs::Entity trigger;
    ecs::Entity body;
    bool entered = true;  ///< false: it left (or stopped existing).
};

struct Settings {
    Vec3 gravity{0.0f, -9.81f, 0.0f};
    /// Contacts below this impulse (N.s) are not reported. Resting bodies
    /// collide gently every step: the default keeps them out.
    f32 minimumContactImpulse = 0.5f;
};

/// A collision resolved during a step.
struct Contact {
    ecs::Entity a;  ///< A dynamic body.
    ecs::Entity b;  ///< The other body or collider.
    Vec3 point{0.0f};
    Vec3 normal{0.0f};  ///< From a toward b.
    f32 impulse = 0.0f;  ///< Exchanged along the normal (N.s): how hard it hit.
};

/// Event published by the application for each reported contact.
struct Collision {
    Contact contact;
};

/// Advances every RigidBody of `world` by `dt` seconds (call it from
/// FixedUpdate with the fixed step): gravity, then collisions of dynamic
/// spheres with each other and with every box and static sphere, resolved
/// with restitution and friction. Deterministic for a given world.
///
/// Dynamic bodies need a SphereCollider; boxes are static.
/// When `contacts` is given, the collisions resolved during the step are
/// appended to it (see Settings::minimumContactImpulse); when `triggers` is
/// given, bodies entering and leaving triggers are.
void step(ecs::World& world, f32 dt, const Settings& settings = {}, std::vector<Contact>* contacts = nullptr,
          std::vector<TriggerEvent>* triggers = nullptr);

/// Draws bodies between their last two steps: sets the translation of each
/// body's WorldTransform to mix(previous, current position, `alpha`), the
/// fraction of a fixed step accumulated since (FrameTime::fixedAlpha). Call
/// it after scene::updateWorldTransforms. Transform, the simulated pose, is
/// left alone, so motion looks smooth at any frame rate.
void interpolate(ecs::World& world, f32 alpha);

struct RayHit {
    ecs::Entity entity;
    f32 distance = 0.0f;  ///< Along the ray's direction.
    Vec3 point{0.0f};
    Vec3 normal{0.0f};    ///< Of the surface hit, facing the ray.
};

/// First collider (sphere or box, dynamic or static) the ray from `origin`
/// along the unit `direction` touches within `maxDistance`: shooting, line
/// of sight, ground checks. A ray starting inside a collider hits it at
/// distance 0.
[[nodiscard]] std::optional<RayHit> raycast(const ecs::World& world, const Vec3& origin,
                                            const Vec3& direction, f32 maxDistance = 1e30f);

/// Registers RigidBody, SphereCollider, BoxCollider and Trigger with scene
/// files.
void addComponentSerializers(scene::ComponentSerializers& serializers);

}  // namespace bdex::physics
