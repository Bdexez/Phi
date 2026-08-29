#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/ecs/World.hpp>
#include <bdex/scene/Serialization.hpp>

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

struct Settings {
    Vec3 gravity{0.0f, -9.81f, 0.0f};
};

/// Advances every RigidBody of `world` by `dt` seconds (call it from
/// FixedUpdate with the fixed step): gravity, then collisions of dynamic
/// spheres with each other and with every box and static sphere, resolved
/// with restitution and friction. Deterministic for a given world.
///
/// Dynamic bodies need a SphereCollider; boxes are static.
void step(ecs::World& world, f32 dt, const Settings& settings = {});

/// Registers RigidBody, SphereCollider and BoxCollider with scene files.
void addComponentSerializers(scene::ComponentSerializers& serializers);

}  // namespace bdex::physics
