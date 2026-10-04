#include "BoxDynamics.hpp"

#include <array>

namespace bdex::physics::detail {

namespace {

constexpr u32 kIterations = 10;      ///< Solver passes per step.
constexpr f32 kBaumgarte = 0.2f;     ///< Share of the overlap corrected per step.
constexpr f32 kSlop = 0.005f;        ///< Overlap left alone, so resting contacts persist.

struct BoxBody {
    ecs::Entity entity;
    Transform* transform;
    RigidBody* body;
    const BoxCollider* collider;
    Vec3 half;
    f32 inverseMass;
    Vec3 inverseInertiaLocal;  ///< Diagonal, in the box's frame.
    Mat3 inverseInertiaWorld{1.0f};
};

struct BoxContact {
    usize body;
    ecs::Entity other;
    Vec3 point;
    Vec3 normal;  ///< Pushes the body out of the other collider.
    f32 depth;    ///< Overlap; negative for a gap (a speculative contact).
    Vec3 arm{0.0f};
    std::array<Vec3, 2> tangents{};
    f32 normalMass = 0.0f;
    std::array<f32, 2> tangentMass{};
    f32 target = 0.0f;  ///< Normal velocity aimed for: the bounce, or the overlap's correction.
    f32 friction = 0.0f;
    f32 normalImpulse = 0.0f;
    std::array<f32, 2> tangentImpulse{};
};

Vec3 velocityAt(const BoxBody& box, const Vec3& arm) {
    return box.body->velocity + glm::cross(box.body->angularVelocity, arm);
}

void applyImpulse(BoxBody& box, const Vec3& arm, const Vec3& impulse) {
    box.body->velocity += impulse * box.inverseMass;
    box.body->angularVelocity += box.inverseInertiaWorld * glm::cross(arm, impulse);
}

f32 effectiveMass(const BoxBody& box, const Vec3& arm, const Vec3& direction) {
    const Vec3 spin = box.inverseInertiaWorld * glm::cross(arm, direction);
    const f32 k = box.inverseMass + glm::dot(direction, glm::cross(spin, arm));
    return k > 1e-12f ? 1.0f / k : 0.0f;
}

std::array<Vec3, 8> corners(const Box& box) {
    std::array<Vec3, 8> points;
    for (int i = 0; i < 8; ++i) {
        const Vec3 local((i & 1) != 0 ? box.half.x : -box.half.x, (i & 2) != 0 ? box.half.y : -box.half.y,
                         (i & 4) != 0 ? box.half.z : -box.half.z);
        points[static_cast<usize>(i)] = box.center + box.rotation * local;
    }
    return points;
}

bool overlaps(const Box& a, const Box& b) {
    return glm::all(glm::lessThanEqual(a.boundsMin, b.boundsMax)) && glm::all(glm::lessThanEqual(b.boundsMin, a.boundsMax));
}

}  // namespace

void stepBoxes(ecs::World& world, f32 dt, const Settings& settings, const std::vector<Box>& staticBoxes,
               const std::vector<SphereShape>& staticSpheres, std::vector<Contact>* contacts) {
    if (dt <= 0.0f) {
        return;
    }
    std::vector<BoxBody> bodies;
    world.view<Transform, const BoxCollider, RigidBody>().exclude<Trigger>().each(
        [&](ecs::Entity entity, Transform& transform, const BoxCollider& collider, RigidBody& body) {
            const Vec3 half = collider.halfExtents * glm::abs(transform.scale);
            if (!finite(transform.position) || !finite(half) || !(body.mass > 0.0f) || !std::isfinite(body.mass)) {
                return;
            }
            // A solid box: I = m / 12 (h^2 + d^2) about the x axis, and so on.
            const Vec3 size = half * 2.0f;
            const Vec3 squared = size * size;
            const Vec3 inertia = body.mass / 12.0f *
                                 Vec3(squared.y + squared.z, squared.x + squared.z, squared.x + squared.y);
            bodies.push_back({entity, &transform, &body, &collider, half, 1.0f / body.mass,
                              glm::max(Vec3(1.0f) / glm::max(inertia, Vec3(1e-9f)), Vec3(0.0f))});
        });
    if (bodies.empty()) {
        return;
    }

    const bool sleeping = settings.sleepSpeed > 0.0f;
    for (BoxBody& box : bodies) {
        RigidBody& body = *box.body;
        if (body.asleep &&
            (!sleeping || body.velocity != Vec3(0.0f) || body.angularVelocity != Vec3(0.0f))) {
            wake(body);
        }
        body.previousPosition = box.transform->position;
        body.previousRotation = box.transform->rotation;
        body.hasPrevious = true;
        if (body.asleep) {
            continue;
        }
        body.velocity += settings.gravity * (body.gravityScale * dt);
        if (body.linearDamping > 0.0f) body.velocity *= std::exp(-body.linearDamping * dt);
        if (body.angularDamping > 0.0f) body.angularVelocity *= std::exp(-body.angularDamping * dt);
        const Mat3 axes = glm::mat3_cast(glm::normalize(box.transform->rotation));
        Mat3 local(0.0f);
        local[0][0] = box.inverseInertiaLocal.x;
        local[1][1] = box.inverseInertiaLocal.y;
        local[2][2] = box.inverseInertiaLocal.z;
        box.inverseInertiaWorld = axes * local * glm::transpose(axes);
    }

    // Contacts with the static boxes: corners of each inside the other.
    // Corners near enough to reach a surface during this step count too,
    // as speculative contacts with a negative depth (the gap): the solver
    // lets them close the gap but not cross it, so a fast box cannot pass
    // through a thin floor between two steps.
    std::vector<BoxContact> found;
    for (usize index = 0; index < bodies.size(); ++index) {
        BoxBody& box = bodies[index];
        if (box.body->asleep) {
            continue;
        }
        const Box shape = makeBox(box.entity, *box.transform, *box.collider);
        const f32 reach = std::max({box.half.x, box.half.y, box.half.z});
        const f32 margin =
            std::max(kSlop * 2.0f,
                     1.5f * dt * (glm::length(box.body->velocity) + glm::length(box.body->angularVelocity) * reach));
        Box grown = shape;
        grown.boundsMin -= Vec3(margin);
        grown.boundsMax += Vec3(margin);
        for (const Box& other : staticBoxes) {
            if (!overlaps(grown, other)) {
                continue;
            }
            for (const Vec3& corner : corners(shape)) {
                if (const auto near = sphereBoxContact(other, corner, margin)) {
                    found.push_back({index, other.entity, corner, -near->normal, near->depth - margin});
                }
            }
            for (const Vec3& corner : corners(other)) {
                if (const auto inside = sphereBoxContact(shape, corner, 0.0f)) {
                    found.push_back({index, other.entity, corner, inside->normal, inside->depth});
                }
            }
        }
        // A static sphere touches the box at one point, pushing it away.
        for (const SphereShape& sphere : staticSpheres) {
            if (const auto touch = sphereBoxContact(shape, sphere.center, sphere.radius + margin)) {
                found.push_back({index, sphere.entity, sphere.center + touch->normal * sphere.radius, touch->normal,
                                 touch->depth - margin});
            }
        }
    }

    // Solve: sequential impulses, accumulated and clamped, so a box resting
    // on several corners settles instead of rocking.
    for (BoxContact& contact : found) {
        BoxBody& box = bodies[contact.body];
        contact.arm = contact.point - box.transform->position;
        const Vec3 helper = std::abs(contact.normal.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
        contact.tangents[0] = glm::normalize(glm::cross(contact.normal, helper));
        contact.tangents[1] = glm::cross(contact.normal, contact.tangents[0]);
        contact.normalMass = effectiveMass(box, contact.arm, contact.normal);
        contact.tangentMass = {effectiveMass(box, contact.arm, contact.tangents[0]),
                               effectiveMass(box, contact.arm, contact.tangents[1])};
        const f32 approaching = glm::dot(velocityAt(box, contact.arm), contact.normal);
        // Static colliders bounce fully and grip at 0.5, as for spheres.
        const f32 bounce = approaching < -2.0f * glm::length(settings.gravity) * dt
                               ? -std::min(box.body->restitution, 1.0f) * approaching
                               : 0.0f;
        if (contact.depth < 0.0f) {
            // Not touching yet. Hit during this step, it bounces now (a
            // gap early at most), or would arrive with no speed left to
            // bounce; otherwise it may close the gap but not cross it.
            const bool hitThisStep = approaching * dt < contact.depth;
            contact.target = hitThisStep && bounce > 0.0f ? bounce : contact.depth / dt;
        } else {
            const f32 correction = kBaumgarte / dt * std::max(contact.depth - kSlop, 0.0f);
            contact.target = std::max(bounce, correction);
        }
        contact.friction = std::sqrt(std::max(box.body->friction, 0.0f) * 0.5f);
    }
    for (u32 iteration = 0; iteration < kIterations; ++iteration) {
        for (BoxContact& contact : found) {
            BoxBody& box = bodies[contact.body];
            const f32 normalSpeed = glm::dot(velocityAt(box, contact.arm), contact.normal);
            const f32 previous = contact.normalImpulse;
            contact.normalImpulse = std::max(previous + (contact.target - normalSpeed) * contact.normalMass, 0.0f);
            applyImpulse(box, contact.arm, contact.normal * (contact.normalImpulse - previous));

            const f32 limit = contact.friction * contact.normalImpulse;
            for (usize t = 0; t < 2; ++t) {
                const f32 slide = glm::dot(velocityAt(box, contact.arm), contact.tangents[t]);
                const f32 before = contact.tangentImpulse[t];
                contact.tangentImpulse[t] = std::clamp(before - slide * contact.tangentMass[t], -limit, limit);
                applyImpulse(box, contact.arm, contact.tangents[t] * (contact.tangentImpulse[t] - before));
            }
        }
    }

    // Move, turn, and maybe sleep.
    for (BoxBody& box : bodies) {
        RigidBody& body = *box.body;
        if (body.asleep) {
            continue;
        }
        box.transform->position += body.velocity * dt;
        if (const f32 rate = glm::length(body.angularVelocity); rate > 1e-6f) {
            box.transform->rotation =
                glm::normalize(glm::angleAxis(rate * dt, body.angularVelocity / rate) * box.transform->rotation);
        }
        if (sleeping) {
            const f32 reach = std::max({box.half.x, box.half.y, box.half.z});
            if (glm::length(body.velocity) < settings.sleepSpeed &&
                glm::length(body.angularVelocity) * reach < settings.sleepSpeed) {
                body.restingTime += dt;
                if (body.restingTime >= settings.sleepDelay) {
                    body.asleep = true;
                    body.velocity = Vec3(0.0f);
                    body.angularVelocity = Vec3(0.0f);
                }
            } else {
                body.restingTime = 0.0f;
            }
        }
    }

    // One collision per box and collider touched, as hard as all its
    // contact points together.
    if (contacts != nullptr) {
        for (usize i = 0; i < found.size();) {
            const BoxContact& first = found[i];
            f32 impulse = 0.0f;
            Vec3 point(0.0f);
            Vec3 normal(0.0f);
            usize count = 0;
            for (; i < found.size() && found[i].body == first.body && found[i].other == first.other; ++i) {
                impulse += found[i].normalImpulse;
                point += found[i].point;
                normal += found[i].normal;
                ++count;
            }
            if (impulse >= settings.minimumContactImpulse) {
                const f32 length = glm::length(normal);
                contacts->push_back({bodies[first.body].entity, first.other, point / static_cast<f32>(count),
                                     length > 0.0f ? -normal / length : Vec3(0.0f, -1.0f, 0.0f), impulse});
            }
        }
    }
}

}  // namespace bdex::physics::detail
