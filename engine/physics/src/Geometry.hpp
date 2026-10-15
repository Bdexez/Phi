#pragma once

// Geometry shared by the physics step, its box dynamics, raycasts and
// character movement: oriented boxes and the tests between them, spheres
// and rays. Internal to the physics library.

#include <bdex/physics/Physics.hpp>
#include <bdex/scene/Components.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace bdex::physics::detail {

using scene::Transform;

inline bool finite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// Oriented box: the collider's extents in the entity's rotated frame.
struct Box {
    ecs::Entity entity;
    Vec3 center;
    Vec3 half;
    Quat rotation;
    Vec3 boundsMin;  ///< World-space box around it, for the broad phase.
    Vec3 boundsMax;
    Vec3 velocity{0.0f};  ///< Kinematic boxes only.
    Vec3 angularVelocity{0.0f};

    /// The surface's velocity at `point` (zero for still boxes).
    [[nodiscard]] Vec3 velocityAt(const Vec3& point) const {
        return velocity + glm::cross(angularVelocity, point - center);
    }
};

inline Box makeBox(ecs::Entity entity, const Transform& transform, const BoxCollider& collider,
                   const Kinematic* motion = nullptr) {
    Box box{entity, transform.position, collider.halfExtents * glm::abs(transform.scale),
            glm::normalize(transform.rotation), {}, {}};
    if (motion != nullptr) {
        box.velocity = motion->velocity;
        box.angularVelocity = motion->angularVelocity;
    }
    const Mat3 axes = glm::mat3_cast(box.rotation);
    const Vec3 extent = glm::abs(axes[0]) * box.half.x + glm::abs(axes[1]) * box.half.y + glm::abs(axes[2]) * box.half.z;
    box.boundsMin = box.center - extent;
    box.boundsMax = box.center + extent;
    return box;
}

struct BoxRayHit {
    f32 distance;
    Vec3 normal;
};

/// First point where a ray (unit `direction`) enters an oriented box grown by
/// `margin` on every side, within `maxDistance`; distance 0 when it starts
/// inside.
inline std::optional<BoxRayHit> rayBox(const Box& box, const Vec3& origin, const Vec3& direction, f32 maxDistance,
                                f32 margin = 0.0f) {
    // In the box's frame, where it is axis-aligned.
    const Quat toLocal = glm::conjugate(box.rotation);
    const Vec3 localOrigin = toLocal * (origin - box.center);
    const Vec3 localDirection = toLocal * direction;
    const Vec3 high = box.half + Vec3(margin);
    const Vec3 low = -high;
    // Slabs: the ray is inside the box between its latest entry and
    // earliest exit across the three axes.
    f32 enter = 0.0f;
    f32 exit = maxDistance;
    Vec3 normal = -localDirection;  // when starting inside
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(localDirection[axis]) < 1e-9f) {
            if (localOrigin[axis] < low[axis] || localOrigin[axis] > high[axis]) {
                return std::nullopt;
            }
            continue;
        }
        f32 near = (low[axis] - localOrigin[axis]) / localDirection[axis];
        f32 far = (high[axis] - localOrigin[axis]) / localDirection[axis];
        f32 side = -1.0f;
        if (near > far) {
            std::swap(near, far);
            side = 1.0f;
        }
        if (near > enter) {
            enter = near;
            normal = Vec3(0.0f);
            normal[axis] = side;
        }
        exit = std::min(exit, far);
        if (enter > exit) {
            return std::nullopt;
        }
    }
    return BoxRayHit{enter, box.rotation * normal};
}

struct Penetration {
    Vec3 normal;  ///< From the sphere toward the box.
    f32 depth;
};

/// Overlap of a sphere with an oriented box, worked out in the box's frame.
inline std::optional<Penetration> sphereBoxContact(const Box& box, const Vec3& center, f32 radius) {
    const Quat toLocal = glm::conjugate(box.rotation);
    const Vec3 local = toLocal * (center - box.center);
    const Vec3 closest = glm::clamp(local, -box.half, box.half);
    const Vec3 offset = local - closest;
    const f32 distance = glm::length(offset);
    if (distance > 1e-6f) {
        if (distance >= radius) {
            return std::nullopt;
        }
        return Penetration{box.rotation * (-offset / distance), radius - distance};
    }
    // Center inside the box: push out through the nearest face.
    f32 nearest = box.half.x - local.x;
    Vec3 normal(-1.0f, 0.0f, 0.0f);
    for (int axis = 0; axis < 3; ++axis) {
        for (const f32 side : {-1.0f, 1.0f}) {
            const f32 gap = box.half[axis] - side * local[axis];
            if (gap < nearest) {
                nearest = gap;
                normal = Vec3(0.0f);
                normal[axis] = -side;
            }
        }
    }
    return Penetration{box.rotation * normal, nearest + radius};
}

/// Whether two oriented boxes overlap, by separating axes: the three face
/// axes of each and the nine products of their edges.
inline bool boxesOverlap(const Box& a, const Box& b) {
    const Mat3 axesA = glm::mat3_cast(a.rotation);
    const Mat3 axesB = glm::mat3_cast(b.rotation);
    const Vec3 between = b.center - a.center;
    const auto separates = [&](const Vec3& axis) {
        f32 radius = 0.0f;
        for (int i = 0; i < 3; ++i) {
            radius += std::abs(glm::dot(axis, axesA[i])) * a.half[i] + std::abs(glm::dot(axis, axesB[i])) * b.half[i];
        }
        return std::abs(glm::dot(between, axis)) > radius;
    };
    for (int i = 0; i < 3; ++i) {
        if (separates(axesA[i]) || separates(axesB[i])) {
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const Vec3 cross = glm::cross(axesA[i], axesB[j]);
            if (glm::dot(cross, cross) > 1e-8f && separates(glm::normalize(cross))) {
                return false;
            }
        }
    }
    return true;
}

/// A sphere collider as boxes meet it: static (no body) or dynamic.
struct SphereShape {
    ecs::Entity entity;
    Vec3 center;
    f32 radius;
    RigidBody* body = nullptr;
};

inline f32 largestScale(const Transform& transform) {
    return std::max({std::abs(transform.scale.x), std::abs(transform.scale.y), std::abs(transform.scale.z)});
}

}  // namespace bdex::physics::detail
