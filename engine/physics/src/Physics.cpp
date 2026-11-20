#include <bdex/physics/Physics.hpp>

#include <bdex/scene/Components.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace bdex::physics {
namespace {

using scene::Transform;

struct Sphere {
    ecs::Entity entity;
    Vec3 center;
    f32 radius;
    RigidBody* body;  ///< Null for static spheres.
};

struct Box {
    Vec3 min;
    Vec3 max;
};

f32 largestScale(const Transform& transform) {
    return std::max({std::abs(transform.scale.x), std::abs(transform.scale.y), std::abs(transform.scale.z)});
}

/// Applies the impulse separating two bodies along `normal` (from a to b),
/// with restitution and friction. Static sides have no RigidBody.
void resolve(RigidBody* a, RigidBody* b, const Vec3& normal) {
    const f32 inverseA = a != nullptr && a->mass > 0.0f ? 1.0f / a->mass : 0.0f;
    const f32 inverseB = b != nullptr && b->mass > 0.0f ? 1.0f / b->mass : 0.0f;
    const f32 inverseSum = inverseA + inverseB;
    if (inverseSum <= 0.0f) {
        return;
    }
    const Vec3 velocityA = a != nullptr ? a->velocity : Vec3(0.0f);
    const Vec3 velocityB = b != nullptr ? b->velocity : Vec3(0.0f);
    const Vec3 relative = velocityB - velocityA;
    const f32 approaching = glm::dot(relative, normal);
    if (approaching >= 0.0f) {
        return;  // already separating
    }
    const f32 restitution = std::min(a != nullptr ? a->restitution : 1.0f, b != nullptr ? b->restitution : 1.0f);
    const f32 impulse = -(1.0f + restitution) * approaching / inverseSum;
    if (a != nullptr) a->velocity -= normal * (impulse * inverseA);
    if (b != nullptr) b->velocity += normal * (impulse * inverseB);

    // Friction: oppose sliding, at most mu times the normal impulse.
    Vec3 tangent = relative - normal * approaching;
    const f32 sliding = glm::length(tangent);
    if (sliding > 1e-6f) {
        tangent /= sliding;
        const f32 friction = std::sqrt((a != nullptr ? a->friction : 0.5f) * (b != nullptr ? b->friction : 0.5f));
        const f32 tangential = std::min(sliding / inverseSum, friction * impulse);
        if (a != nullptr) a->velocity += tangent * (tangential * inverseA);
        if (b != nullptr) b->velocity -= tangent * (tangential * inverseB);
    }
}

/// Moves spheres out of overlap: only most of the depth, which keeps
/// resting contacts from jittering.
void separate(Sphere& a, Sphere* b, const Vec3& normal, f32 depth) {
    constexpr f32 kSlop = 0.001f;
    constexpr f32 kShare = 0.8f;
    const f32 correction = std::max(depth - kSlop, 0.0f) * kShare;
    const f32 inverseA = a.body != nullptr && a.body->mass > 0.0f ? 1.0f / a.body->mass : 0.0f;
    const f32 inverseB = b != nullptr && b->body != nullptr && b->body->mass > 0.0f ? 1.0f / b->body->mass : 0.0f;
    const f32 inverseSum = inverseA + inverseB;
    if (inverseSum <= 0.0f) {
        return;
    }
    a.center -= normal * (correction * inverseA / inverseSum);
    if (b != nullptr) {
        b->center += normal * (correction * inverseB / inverseSum);
    }
}

}  // namespace

void step(ecs::World& world, f32 dt, const Settings& settings) {
    if (dt <= 0.0f) {
        return;
    }

    // Gather: dynamic and static spheres, static boxes.
    std::vector<Sphere> spheres;
    world.view<Transform, const SphereCollider>().each(
        [&](ecs::Entity entity, Transform& transform, const SphereCollider& collider) {
            spheres.push_back({entity, transform.position, collider.radius * largestScale(transform),
                               world.tryGet<RigidBody>(entity)});
        });
    std::vector<Box> boxes;
    world.view<const Transform, const BoxCollider>().exclude<RigidBody>().each(
        [&](const Transform& transform, const BoxCollider& collider) {
            const Vec3 half = collider.halfExtents * glm::abs(transform.scale);
            boxes.push_back({transform.position - half, transform.position + half});
        });

    // Integrate (semi-implicit Euler).
    for (Sphere& sphere : spheres) {
        if (sphere.body != nullptr) {
            sphere.body->previousPosition = sphere.center;
            sphere.body->hasPrevious = true;
            sphere.body->velocity += settings.gravity * (sphere.body->gravityScale * dt);
            sphere.center += sphere.body->velocity * dt;
        }
    }

    // Spheres against static boxes. Boxes go into a grid over the XZ plane
    // (most worlds are wide rather than tall), except very large ones (a
    // floor) that every sphere checks; each sphere only visits the boxes
    // around it, in their original order so results do not depend on the
    // grid.
    f32 cell = 1.0f;
    {
        // Typical box size, ignoring the large ones that set the maximum.
        std::vector<f32> sizes;
        for (const Box& box : boxes) {
            sizes.push_back(std::max(box.max.x - box.min.x, box.max.z - box.min.z));
        }
        if (!sizes.empty()) {
            std::nth_element(sizes.begin(), sizes.begin() + static_cast<std::ptrdiff_t>(sizes.size() / 2), sizes.end());
            cell = std::max(sizes[sizes.size() / 2] * 2.0f, 0.5f);
        }
    }
    const auto cellOf = [cell](f32 coordinate) { return static_cast<i64>(std::floor(coordinate / cell)); };
    const auto key = [](i64 x, i64 z) { return (static_cast<u64>(x) << 32) ^ static_cast<u64>(z & 0xffffffff); };
    std::vector<u32> large;
    std::unordered_map<u64, std::vector<u32>> grid;
    for (u32 i = 0; i < boxes.size(); ++i) {
        const Box& box = boxes[i];
        const i64 x0 = cellOf(box.min.x);
        const i64 x1 = cellOf(box.max.x);
        const i64 z0 = cellOf(box.min.z);
        const i64 z1 = cellOf(box.max.z);
        if ((x1 - x0 + 1) * (z1 - z0 + 1) > 16) {
            large.push_back(i);
            continue;
        }
        for (i64 x = x0; x <= x1; ++x) {
            for (i64 z = z0; z <= z1; ++z) {
                grid[key(x, z)].push_back(i);
            }
        }
    }

    std::vector<u32> candidates;
    for (Sphere& sphere : spheres) {
        if (sphere.body == nullptr) {
            continue;
        }
        // A correction moves the sphere by less than its radius: a margin of
        // one radius keeps every box it may touch this step.
        const f32 reach = sphere.radius * 2.0f;
        candidates.assign(large.begin(), large.end());
        for (i64 x = cellOf(sphere.center.x - reach); x <= cellOf(sphere.center.x + reach); ++x) {
            for (i64 z = cellOf(sphere.center.z - reach); z <= cellOf(sphere.center.z + reach); ++z) {
                if (const auto found = grid.find(key(x, z)); found != grid.end()) {
                    candidates.insert(candidates.end(), found->second.begin(), found->second.end());
                }
            }
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        for (const u32 boxIndex : candidates) {
            const Box& box = boxes[boxIndex];
            const Vec3 closest = glm::clamp(sphere.center, box.min, box.max);
            Vec3 offset = sphere.center - closest;
            f32 distance = glm::length(offset);
            Vec3 normal;
            f32 depth;
            if (distance > 1e-6f) {
                if (distance >= sphere.radius) {
                    continue;
                }
                normal = -offset / distance;  // from the sphere toward the box
                depth = sphere.radius - distance;
            } else {
                // Center inside the box: push out through the nearest face.
                const Vec3 toMin = sphere.center - box.min;
                const Vec3 toMax = box.max - sphere.center;
                f32 nearest = toMin.x;
                normal = Vec3(1.0f, 0.0f, 0.0f);
                const auto consider = [&](f32 gap, const Vec3& axis) {
                    if (gap < nearest) {
                        nearest = gap;
                        normal = axis;
                    }
                };
                consider(toMin.y, Vec3(0.0f, 1.0f, 0.0f));
                consider(toMin.z, Vec3(0.0f, 0.0f, 1.0f));
                consider(toMax.x, Vec3(-1.0f, 0.0f, 0.0f));
                consider(toMax.y, Vec3(0.0f, -1.0f, 0.0f));
                consider(toMax.z, Vec3(0.0f, 0.0f, -1.0f));
                depth = nearest + sphere.radius;
            }
            separate(sphere, nullptr, normal, depth);
            resolve(sphere.body, nullptr, normal);
        }
    }

    // Sphere pairs: sort and sweep along X.
    std::vector<usize> order(spheres.size());
    for (usize i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](usize a, usize b) {
        const f32 left = spheres[a].center.x - spheres[a].radius;
        const f32 right = spheres[b].center.x - spheres[b].radius;
        return left != right ? left < right : a < b;  // stable tie-break: deterministic
    });
    for (usize i = 0; i < order.size(); ++i) {
        Sphere& a = spheres[order[i]];
        for (usize j = i + 1; j < order.size(); ++j) {
            Sphere& b = spheres[order[j]];
            if (b.center.x - b.radius > a.center.x + a.radius) {
                break;  // no later sphere reaches back to a
            }
            if (a.body == nullptr && b.body == nullptr) {
                continue;
            }
            const Vec3 offset = b.center - a.center;
            const f32 distance = glm::length(offset);
            const f32 reach = a.radius + b.radius;
            if (distance >= reach) {
                continue;
            }
            const Vec3 normal = distance > 1e-6f ? offset / distance : Vec3(0.0f, 1.0f, 0.0f);
            separate(a, &b, normal, reach - distance);
            resolve(a.body, b.body, normal);
        }
    }

    // Write the poses back.
    for (const Sphere& sphere : spheres) {
        if (sphere.body != nullptr) {
            world.get<Transform>(sphere.entity).position = sphere.center;
        }
    }
}

void interpolate(ecs::World& world, f32 alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    world.view<const RigidBody, const Transform, scene::WorldTransform>().each(
        [alpha](const RigidBody& body, const Transform& transform, scene::WorldTransform& drawn) {
            if (body.hasPrevious) {
                drawn.matrix[3] = Vec4(glm::mix(body.previousPosition, transform.position, alpha), 1.0f);
            }
        });
}

void addComponentSerializers(scene::ComponentSerializers& serializers) {
    using scene::EntityRefs;
    using scene::Fields;
    using scene::formatField;
    using scene::parseField;
    serializers.add<RigidBody>(
        "RigidBody",
        [](const RigidBody& body, Fields& out, const EntityRefs&) {
            out["velocity"] = formatField(body.velocity);
            out["mass"] = formatField(body.mass);
            out["restitution"] = formatField(body.restitution);
            out["friction"] = formatField(body.friction);
            out["gravity_scale"] = formatField(body.gravityScale);
        },
        [](RigidBody& body, const Fields& in, const EntityRefs&) -> Result<> {
            for (auto parsed : {parseField(in, "velocity", body.velocity), parseField(in, "mass", body.mass),
                                parseField(in, "restitution", body.restitution),
                                parseField(in, "friction", body.friction),
                                parseField(in, "gravity_scale", body.gravityScale)}) {
                if (!parsed) {
                    return parsed;
                }
            }
            return {};
        });
    serializers.add<SphereCollider>(
        "SphereCollider",
        [](const SphereCollider& sphere, Fields& out, const EntityRefs&) { out["radius"] = formatField(sphere.radius); },
        [](SphereCollider& sphere, const Fields& in, const EntityRefs&) { return parseField(in, "radius", sphere.radius); });
    serializers.add<BoxCollider>(
        "BoxCollider",
        [](const BoxCollider& box, Fields& out, const EntityRefs&) { out["half_extents"] = formatField(box.halfExtents); },
        [](BoxCollider& box, const Fields& in, const EntityRefs&) { return parseField(in, "half_extents", box.halfExtents); });
}

}  // namespace bdex::physics
