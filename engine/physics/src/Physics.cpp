#include <bdex/physics/Physics.hpp>

#include "BoxDynamics.hpp"
#include "Geometry.hpp"

#include <bdex/scene/Components.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <vector>

namespace bdex::physics {
namespace {

using scene::Transform;
using namespace detail;

struct Sphere {
    ecs::Entity entity;
    Vec3 center;
    f32 radius;
    RigidBody* body;  ///< Null for static spheres.
    Transform* transform;  ///< Stable during a step: nothing is added to the world.
};

/// One side of a contact: its body (null for static colliders) and the arm
/// from its center to the contact point.
struct ContactSide {
    RigidBody* body;
    Vec3 arm;
    f32 radius;  ///< Of the sphere, for its inertia; 0 for boxes.
};

f32 inverseMass(const ContactSide& side) {
    return side.body != nullptr && side.body->mass > 0.0f ? 1.0f / side.body->mass : 0.0f;
}

/// Inverse moment of inertia of a solid sphere (2/5 m r^2), 0 when static.
f32 inverseInertia(const ContactSide& side) {
    const f32 inverse = inverseMass(side);
    return inverse > 0.0f && side.radius > 0.0f ? inverse / (0.4f * side.radius * side.radius) : 0.0f;
}

Vec3 contactVelocity(const ContactSide& side) {
    return side.body != nullptr ? side.body->velocity + glm::cross(side.body->angularVelocity, side.arm) : Vec3(0.0f);
}

/// Applies the impulse separating two bodies along `normal` (from a to b),
/// with restitution and friction at the contact point, which also spins
/// them, and returns the normal impulse (0 when they were already
/// separating). Static sides have no RigidBody.
f32 resolve(const ContactSide& a, const ContactSide& b, const Vec3& normal) {
    const f32 inverseA = inverseMass(a);
    const f32 inverseB = inverseMass(b);
    const f32 inverseSum = inverseA + inverseB;
    if (inverseSum <= 0.0f) {
        return 0.0f;
    }
    // For spheres the arms lie along the normal, so spin does not change
    // the approach speed.
    const Vec3 relative = contactVelocity(b) - contactVelocity(a);
    const f32 approaching = glm::dot(relative, normal);
    if (approaching >= 0.0f) {
        return 0.0f;  // already separating
    }
    const f32 restitution =
        std::min(a.body != nullptr ? a.body->restitution : 1.0f, b.body != nullptr ? b.body->restitution : 1.0f);
    const f32 impulse = -(1.0f + restitution) * approaching / inverseSum;
    if (a.body != nullptr) a.body->velocity -= normal * (impulse * inverseA);
    if (b.body != nullptr) b.body->velocity += normal * (impulse * inverseB);

    // Friction: oppose sliding at the contact point, at most mu times the
    // normal impulse. Its effective mass includes the spheres' inertia, so
    // a sliding ball ends up rolling.
    Vec3 tangent = relative - normal * approaching;
    const f32 sliding = glm::length(tangent);
    if (sliding > 1e-6f) {
        tangent /= sliding;
        const f32 friction =
            std::sqrt((a.body != nullptr ? a.body->friction : 0.5f) * (b.body != nullptr ? b.body->friction : 0.5f));
        const f32 inverseAInertia = inverseInertia(a);
        const f32 inverseBInertia = inverseInertia(b);
        const f32 effective = inverseSum + inverseAInertia * glm::dot(a.arm, a.arm) +
                              inverseBInertia * glm::dot(b.arm, b.arm);
        const f32 tangential = std::min(sliding / effective, friction * impulse);
        const Vec3 push = tangent * tangential;  // on a; b gets the opposite
        if (a.body != nullptr) {
            a.body->velocity += push * inverseA;
            a.body->angularVelocity += glm::cross(a.arm, push) * inverseAInertia;
        }
        if (b.body != nullptr) {
            b.body->velocity -= push * inverseB;
            b.body->angularVelocity += glm::cross(b.arm, -push) * inverseBInertia;
        }
    }
    return impulse;
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

void step(ecs::World& world, f32 dt, const Settings& settings, std::vector<Contact>* contacts,
          std::vector<TriggerEvent>* triggers) {
    if (dt <= 0.0f) {
        return;
    }
    const auto report = [&](ecs::Entity a, ecs::Entity b, const Vec3& point, const Vec3& normal, f32 impulse) {
        if (contacts != nullptr && impulse >= settings.minimumContactImpulse && impulse > 0.0f) {
            contacts->push_back({a, b, point, normal, impulse});
        }
    };

    // Gather: dynamic and static spheres, static boxes.
    std::vector<Sphere> spheres;
    world.view<Transform, const SphereCollider>().exclude<Trigger>().each(
        [&](ecs::Entity entity, Transform& transform, const SphereCollider& collider) {
            spheres.push_back({entity, transform.position, collider.radius * largestScale(transform),
                               world.tryGet<RigidBody>(entity), &transform});
        });
    std::vector<Box> boxes;
    world.view<const Transform, const BoxCollider>().exclude<RigidBody, Trigger>().each(
        [&](ecs::Entity entity, const Transform& transform, const BoxCollider& collider) {
            boxes.push_back(makeBox(entity, transform, collider));
        });

    // Sleeping bodies have zero velocity; one that has been given some
    // since was woken by its owner.
    const bool sleeping = settings.sleepSpeed > 0.0f;
    for (Sphere& sphere : spheres) {
        if (sphere.body != nullptr && sphere.body->asleep &&
            (!sleeping || sphere.body->velocity != Vec3(0.0f) || sphere.body->angularVelocity != Vec3(0.0f))) {
            wake(*sphere.body);
        }
    }
    const auto isAsleep = [](const Sphere& sphere) { return sphere.body != nullptr && sphere.body->asleep; };
    // Faster than resting contact: what gravity adds in one step, and some.
    const f32 wakeSpeed = 2.0f * glm::length(settings.gravity) * dt + settings.sleepSpeed;

    // Integrate (semi-implicit Euler).
    for (Sphere& sphere : spheres) {
        if (sphere.body != nullptr) {
            sphere.body->previousPosition = sphere.center;
            sphere.body->previousRotation = sphere.transform->rotation;
            sphere.body->hasPrevious = true;
            if (sphere.body->asleep) {
                continue;
            }
            sphere.body->velocity += settings.gravity * (sphere.body->gravityScale * dt);
            if (sphere.body->linearDamping > 0.0f) {
                sphere.body->velocity *= std::exp(-sphere.body->linearDamping * dt);
            }
            if (sphere.body->angularDamping > 0.0f) {
                sphere.body->angularVelocity *= std::exp(-sphere.body->angularDamping * dt);
            }
            sphere.center += sphere.body->velocity * dt;
        }
    }

    // Colliders that stopped being finite (a NaN from a bad scene file or a
    // blown-up simulation) are left out: they would poison the grid and
    // break the sort below. Such bodies stay where they were.
    std::erase_if(spheres, [&](const Sphere& sphere) {
        return !finite(sphere.center) || !std::isfinite(sphere.radius) || sphere.radius < 0.0f;
    });
    std::erase_if(boxes, [&](const Box& box) {
        return !finite(box.boundsMin) || !finite(box.boundsMax) || !finite(box.half);
    });

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
            sizes.push_back(std::max(box.boundsMax.x - box.boundsMin.x, box.boundsMax.z - box.boundsMin.z));
        }
        if (!sizes.empty()) {
            std::nth_element(sizes.begin(), sizes.begin() + static_cast<std::ptrdiff_t>(sizes.size() / 2), sizes.end());
            cell = std::max(sizes[sizes.size() / 2] * 2.0f, 0.5f);
        }
    }
    // Clamped: converting a float beyond i64's range is undefined behavior,
    // and coordinates this far out are nonsense anyway.
    const auto cellOf = [cell](f32 coordinate) {
        return static_cast<i64>(std::clamp(std::floor(static_cast<f64>(coordinate) / cell), -1e15, 1e15));
    };
    const auto key = [](i64 x, i64 z) { return (static_cast<u64>(x) << 32) ^ static_cast<u64>(z & 0xffffffff); };
    std::vector<u32> large;
    std::unordered_map<u64, std::vector<u32>> grid;
    for (u32 i = 0; i < boxes.size(); ++i) {
        const Box& box = boxes[i];
        const i64 x0 = cellOf(box.boundsMin.x);
        const i64 x1 = cellOf(box.boundsMax.x);
        const i64 z0 = cellOf(box.boundsMin.z);
        const i64 z1 = cellOf(box.boundsMax.z);
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

    // Boxes whose cells overlap a region of the XZ plane, in order, plus the
    // large ones; all of them when the region spans more cells than there
    // are boxes.
    std::vector<u32> candidates;
    const auto gather = [&](const Vec3& low, const Vec3& high) {
        const i64 x0 = cellOf(low.x);
        const i64 x1 = cellOf(high.x);
        const i64 z0 = cellOf(low.z);
        const i64 z1 = cellOf(high.z);
        candidates.clear();
        if (static_cast<f64>(x1 - x0 + 1) * static_cast<f64>(z1 - z0 + 1) > static_cast<f64>(boxes.size())) {
            for (u32 i = 0; i < boxes.size(); ++i) {
                candidates.push_back(i);
            }
            return;
        }
        candidates.assign(large.begin(), large.end());
        for (i64 x = x0; x <= x1; ++x) {
            for (i64 z = z0; z <= z1; ++z) {
                if (const auto found = grid.find(key(x, z)); found != grid.end()) {
                    candidates.insert(candidates.end(), found->second.begin(), found->second.end());
                }
            }
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    };
    for (Sphere& sphere : spheres) {
        if (sphere.body == nullptr || sphere.body->asleep) {
            continue;
        }
        // A correction moves the sphere by less than its radius: a margin of
        // one radius keeps every box it may touch this step.
        const f32 reach = sphere.radius * 2.0f;

        // Fast spheres could pass through a thin box between two steps:
        // follow their path and stop it a little inside the first box it
        // meets, so the contact below resolves the impact.
        const Vec3 from = sphere.body->previousPosition;
        const Vec3 travel = sphere.center - from;
        if (const f32 length = glm::length(travel); length > sphere.radius * 0.5f) {
            const Vec3 direction = travel / length;
            gather(glm::min(from, sphere.center) - Vec3(reach), glm::max(from, sphere.center) + Vec3(reach));
            f32 nearest = length;
            for (const u32 boxIndex : candidates) {
                const Box& box = boxes[boxIndex];
                // The box grown by the radius is a little larger than the
                // swept sphere's true reach at edges and corners, hence the
                // check of an actual contact where the path enters it. Paths
                // starting in contact (rolling on a floor) are left alone.
                const auto hit = rayBox(box, from, direction, nearest, sphere.radius);
                if (hit && hit->distance > 0.0f &&
                    sphereBoxContact(box, from + direction * hit->distance, sphere.radius * 1.01f + 1e-3f)) {
                    nearest = hit->distance;
                }
            }
            if (nearest < length) {
                sphere.center = from + direction * std::min(nearest + sphere.radius * 0.02f, length);
            }
        }

        gather(sphere.center - Vec3(reach), sphere.center + Vec3(reach));

        for (const u32 boxIndex : candidates) {
            const Box& box = boxes[boxIndex];
            const auto contact = sphereBoxContact(box, sphere.center, sphere.radius);
            if (!contact) {
                continue;
            }
            const Vec3 normal = contact->normal;
            const f32 depth = contact->depth;
            separate(sphere, nullptr, normal, depth);
            report(sphere.entity, box.entity, sphere.center + normal * sphere.radius, normal,
                   resolve({sphere.body, normal * sphere.radius, sphere.radius}, {nullptr, Vec3(0.0f), 0.0f}, normal));
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
            const bool aMoves = a.body != nullptr && !a.body->asleep;
            const bool bMoves = b.body != nullptr && !b.body->asleep;
            if (!aMoves && !bMoves) {
                continue;
            }
            const Vec3 offset = b.center - a.center;
            const f32 distance = glm::length(offset);
            const f32 reach = a.radius + b.radius;
            if (distance >= reach) {
                continue;
            }
            const Vec3 normal = distance > 1e-6f ? offset / distance : Vec3(0.0f, 1.0f, 0.0f);
            // A hit wakes a sleeper; merely resting on it leaves it asleep,
            // and static, so piles can sleep.
            Sphere* sleeper = isAsleep(a) ? &a : isAsleep(b) ? &b : nullptr;
            if (sleeper != nullptr) {
                const Vec3 mover = (sleeper == &a ? b : a).body->velocity;
                if (std::abs(glm::dot(mover, normal)) > wakeSpeed) {
                    wake(*sleeper->body);
                    sleeper = nullptr;
                }
            }
            RigidBody* bodyA = sleeper == &a ? nullptr : a.body;
            RigidBody* bodyB = sleeper == &b ? nullptr : b.body;
            if (sleeper == &a) {
                separate(b, nullptr, -normal, reach - distance);
            } else {
                separate(a, sleeper == &b ? nullptr : &b, normal, reach - distance);
            }
            const f32 impulse =
                resolve({bodyA, normal * a.radius, a.radius}, {bodyB, -normal * b.radius, b.radius}, normal);
            // Reported from the dynamic body's side.
            if (a.body != nullptr) {
                report(a.entity, b.entity, a.center + normal * a.radius, normal, impulse);
            } else {
                report(b.entity, a.entity, b.center - normal * b.radius, -normal, impulse);
            }
        }
    }

    // Dynamic boxes, against the same static boxes and spheres.
    std::vector<SphereShape> staticSpheres;
    for (const Sphere& sphere : spheres) {
        if (sphere.body == nullptr) {
            staticSpheres.push_back({sphere.entity, sphere.center, sphere.radius});
        }
    }
    stepBoxes(world, dt, settings, boxes, staticSpheres, contacts);

    // Triggers: which bodies overlap each zone now, compared with before.
    world.view<const Transform, Trigger>().each([&](ecs::Entity zone, const Transform& transform, Trigger& trigger) {
        const auto* box = world.tryGet<BoxCollider>(zone);
        const auto* ball = world.tryGet<SphereCollider>(zone);
        const std::optional<Box> zoneBox = box != nullptr ? std::optional(makeBox(zone, transform, *box)) : std::nullopt;
        const f32 zoneRadius = ball != nullptr ? ball->radius * largestScale(transform) : 0.0f;
        std::vector<ecs::Entity> inside;
        for (const Sphere& sphere : spheres) {
            if (sphere.body == nullptr) {
                continue;
            }
            bool overlaps = false;
            if (zoneBox) {
                overlaps = sphereBoxContact(*zoneBox, sphere.center, sphere.radius).has_value();
            } else if (ball != nullptr) {
                const f32 reach = zoneRadius + sphere.radius;
                overlaps = glm::dot(sphere.center - transform.position, sphere.center - transform.position) < reach * reach;
            }
            if (overlaps) {
                inside.push_back(sphere.entity);
            }
        }
        if (triggers != nullptr) {
            const auto contains = [](const std::vector<ecs::Entity>& list, ecs::Entity entity) {
                return std::find(list.begin(), list.end(), entity) != list.end();
            };
            for (const ecs::Entity entity : inside) {
                if (!contains(trigger.inside, entity)) {
                    triggers->push_back({zone, entity, true});
                }
            }
            for (const ecs::Entity entity : trigger.inside) {
                if (!contains(inside, entity)) {
                    triggers->push_back({zone, entity, false});
                }
            }
        }
        trigger.inside = std::move(inside);
    });

    // Bodies that kept still long enough fall asleep.
    if (sleeping) {
        for (const Sphere& sphere : spheres) {
            RigidBody* body = sphere.body;
            if (body == nullptr || body->asleep) {
                continue;
            }
            const f32 surfaceSpin = glm::length(body->angularVelocity) * sphere.radius;
            if (glm::length(body->velocity) < settings.sleepSpeed && surfaceSpin < settings.sleepSpeed) {
                body->restingTime += dt;
                if (body->restingTime >= settings.sleepDelay) {
                    body->asleep = true;
                    body->velocity = Vec3(0.0f);
                    body->angularVelocity = Vec3(0.0f);
                }
            } else {
                body->restingTime = 0.0f;
            }
        }
    }

    // Write the poses back; spheres turn by their spin.
    for (const Sphere& sphere : spheres) {
        if (sphere.body == nullptr || sphere.body->asleep) {
            continue;
        }
        Transform& transform = *sphere.transform;
        transform.position = sphere.center;
        const Vec3& spin = sphere.body->angularVelocity;
        if (const f32 rate = glm::length(spin); rate > 1e-6f) {
            transform.rotation = glm::normalize(glm::angleAxis(rate * dt, spin / rate) * transform.rotation);
        }
    }
}

std::optional<RayHit> raycast(const ecs::World& world, const Vec3& origin, const Vec3& direction,
                              f32 maxDistance) {
    std::optional<RayHit> nearest;
    const auto consider = [&](ecs::Entity entity, f32 distance, const Vec3& normal) {
        if (distance <= maxDistance && (!nearest || distance < nearest->distance)) {
            nearest = RayHit{entity, distance, origin + direction * distance, normal};
        }
    };

    const auto* spheres = world.storage<SphereCollider>();
    const auto* transforms = world.storage<Transform>();
    if (spheres != nullptr && transforms != nullptr) {
        for (const u32 index : spheres->entities()) {
            const Transform* transform = transforms->tryGet(index);
            if (transform == nullptr) {
                continue;
            }
            const f32 radius = spheres->get(index).radius * largestScale(*transform);
            if (!finite(transform->position) || !std::isfinite(radius)) {
                continue;  // as in step(): broken colliders are left out
            }
            const Vec3 toCenter = transform->position - origin;
            const f32 along = glm::dot(toCenter, direction);
            const f32 missSquared = glm::dot(toCenter, toCenter) - along * along;
            if (glm::dot(toCenter, toCenter) <= radius * radius) {
                consider(world.entityAt(index), 0.0f, -direction);  // starts inside
                continue;
            }
            if (along < 0.0f || missSquared > radius * radius) {
                continue;
            }
            const f32 distance = along - std::sqrt(radius * radius - missSquared);
            const Vec3 point = origin + direction * distance;
            consider(world.entityAt(index), distance, glm::normalize(point - transform->position));
        }
    }

    const auto* boxes = world.storage<BoxCollider>();
    if (boxes != nullptr && transforms != nullptr) {
        for (const u32 index : boxes->entities()) {
            const Transform* transform = transforms->tryGet(index);
            if (transform == nullptr) {
                continue;
            }
            const Box box = makeBox(world.entityAt(index), *transform, boxes->get(index));
            if (!finite(box.boundsMin) || !finite(box.boundsMax)) {
                continue;  // NaN fails every slab comparison: it would be hit at distance 0
            }
            if (const auto hit = rayBox(box, origin, direction, maxDistance)) {
                consider(world.entityAt(index), hit->distance, hit->normal);
            }
        }
    }
    return nearest;
}

MoveResult moveAndSlide(ecs::World& world, ecs::Entity entity, const Vec3& displacement,
                        const MoveSettings& settings) {
    MoveResult result;
    Transform* transform = world.tryGet<Transform>(entity);
    const SphereCollider* body = world.tryGet<SphereCollider>(entity);
    if (transform == nullptr || body == nullptr || !finite(transform->position) || !finite(displacement)) {
        return result;
    }
    const f32 radius = body->radius * largestScale(*transform);
    const f32 walkable = std::cos(settings.maxSlope);

    // What the character can bump into: static boxes, and every other
    // sphere, triggers aside.
    std::vector<Box> boxes;
    world.view<const Transform, const BoxCollider>().exclude<RigidBody, Trigger>().each(
        [&](ecs::Entity other, const Transform& pose, const BoxCollider& collider) {
            Box box = makeBox(other, pose, collider);
            if (finite(box.boundsMin) && finite(box.boundsMax)) {
                boxes.push_back(box);
            }
        });
    struct Obstacle {
        Vec3 center;
        f32 reach;  ///< Both radii.
    };
    std::vector<Obstacle> spheres;
    world.view<const Transform, const SphereCollider>().exclude<Trigger>().each(
        [&](ecs::Entity other, const Transform& pose, const SphereCollider& collider) {
            const f32 reach = radius + collider.radius * largestScale(pose);
            if (other != entity && finite(pose.position) && std::isfinite(reach)) {
                spheres.push_back({pose.position, reach});
            }
        });

    Vec3 position = transform->position;
    const Vec3 start = position;
    const auto touch = [&](const Vec3& normal) {
        if (normal.y >= walkable) {
            result.grounded = true;
            result.groundNormal = normal;
        }
    };

    // Overlapping already (spawned in a wall, a box moved onto us): step out.
    for (const Box& box : boxes) {
        if (const auto contact = sphereBoxContact(box, position, radius)) {
            position -= contact->normal * (contact->depth + settings.skin);
            touch(-contact->normal);
        }
    }

    Vec3 remaining = displacement;
    for (u32 iteration = 0; iteration < settings.iterations; ++iteration) {
        const f32 length = glm::length(remaining);
        if (length < 1e-6f) {
            break;
        }
        const Vec3 direction = remaining / length;
        // The first surface along the way, and its normal (facing us).
        f32 nearest = length;
        Vec3 normal(0.0f);
        for (const Box& box : boxes) {
            const auto hit = rayBox(box, position, direction, nearest, radius);
            if (!hit || hit->distance >= nearest) {
                continue;
            }
            // The grown box reaches too far at edges and corners: count the
            // hit only where the sphere really meets the box.
            const auto contact =
                sphereBoxContact(box, position + direction * hit->distance, radius + 2.0f * settings.skin);
            // Only surfaces moved into: leaving the ground is not hitting it.
            if (contact && glm::dot(direction, contact->normal) > 0.0f) {
                nearest = hit->distance;
                normal = -contact->normal;
            }
        }
        for (const Obstacle& sphere : spheres) {
            const Vec3 toCenter = sphere.center - position;
            const f32 along = glm::dot(toCenter, direction);
            const f32 missSquared = glm::dot(toCenter, toCenter) - along * along;
            if (along <= 0.0f || missSquared >= sphere.reach * sphere.reach) {
                continue;  // behind, or passing by
            }
            const f32 distance = std::max(along - std::sqrt(sphere.reach * sphere.reach - missSquared), 0.0f);
            if (distance < nearest) {
                nearest = distance;
                normal = glm::normalize(position + direction * distance - sphere.center);
            }
        }

        if (normal == Vec3(0.0f)) {
            position += remaining;  // a clear path
            break;
        }
        result.blocked = true;
        touch(normal);
        // Up to the surface, then a skin off it along its normal: backing
        // off along the move instead would shorten every slide.
        position += direction * nearest + normal * settings.skin;
        // Slide: keep the part of the rest of the move along the surface.
        // A steep one is a wall for anything but falling: sliding up it
        // would climb it.
        Vec3 slideNormal = normal;
        if (normal.y < walkable && remaining.y >= 0.0f) {
            const Vec3 flat(normal.x, 0.0f, normal.z);
            if (glm::length(flat) > 1e-4f) {
                slideNormal = glm::normalize(flat);
            }
        }
        remaining = direction * (length - nearest);
        remaining -= slideNormal * glm::dot(remaining, slideNormal);
    }

    result.moved = position - start;
    transform->position = position;
    return result;
}

void interpolate(ecs::World& world, f32 alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    world.view<const RigidBody, const Transform, scene::WorldTransform>().each(
        [alpha](const RigidBody& body, const Transform& transform, scene::WorldTransform& drawn) {
            if (body.hasPrevious) {
                Transform blended = transform;
                blended.position = glm::mix(body.previousPosition, transform.position, alpha);
                blended.rotation = glm::slerp(body.previousRotation, transform.rotation, alpha);
                drawn.matrix = blended.localMatrix();
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
            out["angular_velocity"] = formatField(body.angularVelocity);
            out["mass"] = formatField(body.mass);
            out["restitution"] = formatField(body.restitution);
            out["friction"] = formatField(body.friction);
            out["gravity_scale"] = formatField(body.gravityScale);
            out["linear_damping"] = formatField(body.linearDamping);
            out["angular_damping"] = formatField(body.angularDamping);
        },
        [](RigidBody& body, const Fields& in, const EntityRefs&) -> Result<> {
            for (auto parsed : {parseField(in, "velocity", body.velocity),
                                parseField(in, "angular_velocity", body.angularVelocity),
                                parseField(in, "mass", body.mass),
                                parseField(in, "restitution", body.restitution),
                                parseField(in, "friction", body.friction),
                                parseField(in, "gravity_scale", body.gravityScale),
                                parseField(in, "linear_damping", body.linearDamping),
                                parseField(in, "angular_damping", body.angularDamping)}) {
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
    // Who is inside is runtime state: a loaded trigger starts empty.
    serializers.add<Trigger>(
        "Trigger", [](const Trigger&, Fields&, const EntityRefs&) {},
        [](Trigger&, const Fields&, const EntityRefs&) -> Result<> { return {}; });
    serializers.add<BoxCollider>(
        "BoxCollider",
        [](const BoxCollider& box, Fields& out, const EntityRefs&) { out["half_extents"] = formatField(box.halfExtents); },
        [](BoxCollider& box, const Fields& in, const EntityRefs&) { return parseField(in, "half_extents", box.halfExtents); });
}

}  // namespace bdex::physics
