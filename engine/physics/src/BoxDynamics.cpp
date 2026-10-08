#include "BoxDynamics.hpp"

#include <array>

namespace bdex::physics::detail {

namespace {

constexpr u32 kIterations = 10;   ///< Solver passes per step.
constexpr f32 kBaumgarte = 0.2f;  ///< Share of the overlap corrected per step.
constexpr f32 kSlop = 0.005f;     ///< Overlap left alone, so resting contacts persist.

/// A moving body as the solver sees it: a dynamic box, or a dynamic sphere
/// it touches. Asleep, it is static (no inverse mass) until woken.
struct SolverBody {
    ecs::Entity entity;
    RigidBody* body;
    Vec3 center;
    f32 inverseMass = 0.0f;
    Mat3 inverseInertia{0.0f};  ///< World space.
    f32 reach = 0.0f;           ///< Farthest surface point from the center.
};

struct BoxShape {
    usize body;  ///< Index into the solver bodies.
    Transform* transform;
    const BoxCollider* collider;
    Vec3 half;
    Vec3 inverseInertiaLocal;  ///< Diagonal, in the box's frame.
    Box shape;
    f32 margin = 0.0f;  ///< Reach of speculative contacts this step.
};

/// Body `a` touched by body `b` (or by a static collider when b is none):
/// `normal` pushes a away from b.
struct ContactPoint {
    usize a;
    std::optional<usize> b;
    ecs::Entity other;
    Vec3 point;
    Vec3 normal;
    f32 depth;  ///< Overlap; negative for a gap (a speculative contact).
    Vec3 armA{0.0f};
    Vec3 armB{0.0f};
    std::array<Vec3, 2> tangents{};
    f32 normalMass = 0.0f;
    std::array<f32, 2> tangentMass{};
    f32 target = 0.0f;  ///< Separating speed aimed for: a bounce, a correction, or minus a gap per second.
    f32 friction = 0.0f;
    f32 normalImpulse = 0.0f;
    std::array<f32, 2> tangentImpulse{};
};

Vec3 velocityAt(const SolverBody& body, const Vec3& arm) {
    return body.body->velocity + glm::cross(body.body->angularVelocity, arm);
}

void applyImpulse(SolverBody& body, const Vec3& arm, const Vec3& impulse) {
    body.body->velocity += impulse * body.inverseMass;
    body.body->angularVelocity += body.inverseInertia * glm::cross(arm, impulse);
}

f32 resistance(const SolverBody& body, const Vec3& arm, const Vec3& direction) {
    const Vec3 spin = body.inverseInertia * glm::cross(arm, direction);
    return body.inverseMass + glm::dot(direction, glm::cross(spin, arm));
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

struct FacePoint {
    Vec3 point;
    f32 depth;  ///< Negative for a gap.
};

/// Face-to-face contact between two boxes, found by separating axes: the
/// face axis along which they overlap least (or are closest, within
/// `margin`) is the reference; the incident box's face most turned
/// against it is clipped to the reference face's rectangle, and the
/// clipped points on the near side are the contacts. All share one normal,
/// which pushes `incidentIsB ? b : a` away from the other: what keeps a
/// stack of crates square. When an edge-to-edge axis overlaps least, the
/// boxes meet edge on edge: one contact between the edges' closest points.
/// Nothing when the boxes are apart (or in the rare degenerate cases where
/// the caller falls back to corners).
struct FaceContact {
    bool found = false;
    bool incidentIsB = true;
    Vec3 normal{0.0f};
    std::vector<FacePoint> points;
};

FaceContact faceContact(const Box& a, const Box& b, f32 margin) {
    FaceContact result;
    const Mat3 axesA = glm::mat3_cast(a.rotation);
    const Mat3 axesB = glm::mat3_cast(b.rotation);
    const Vec3 between = b.center - a.center;
    const auto separation = [&](const Vec3& axis) {
        f32 radius = 0.0f;
        for (int i = 0; i < 3; ++i) {
            radius += std::abs(glm::dot(axis, axesA[i])) * a.half[i] + std::abs(glm::dot(axis, axesB[i])) * b.half[i];
        }
        return std::abs(glm::dot(between, axis)) - radius;
    };
    // The face axis separating most (or overlapping least); A's axes win
    // ties, so the choice does not flicker between frames.
    f32 best = -1e30f;
    int bestAxis = 0;
    for (int i = 0; i < 6; ++i) {
        const Vec3 axis = i < 3 ? axesA[i] : axesB[i - 3];
        const f32 apart = separation(axis);
        if (apart > best + 1e-4f) {
            best = apart;
            bestAxis = i;
        }
    }
    if (best > margin) {
        return result;  // apart, beyond this step's reach
    }
    f32 bestEdge = -1e30f;
    int edgeA = 0;
    int edgeB = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const Vec3 cross = glm::cross(axesA[i], axesB[j]);
            const f32 length = glm::length(cross);
            if (length > 1e-3f) {
                if (const f32 apart = separation(cross / length); apart > bestEdge) {
                    bestEdge = apart;
                    edgeA = i;
                    edgeB = j;
                }
            }
        }
    }
    if (bestEdge > margin) {
        return result;  // apart along an edge axis
    }
    if (bestEdge > best + 0.02f) {
        // Meeting edge to edge: one contact, between the two edges' closest
        // points, along their common perpendicular.
        Vec3 normal = glm::normalize(glm::cross(axesA[edgeA], axesB[edgeB]));
        if (glm::dot(normal, between) < 0.0f) {
            normal = -normal;  // from A toward B
        }
        // Each box's edge along that axis reaching furthest toward the other.
        Vec3 onA = a.center;
        Vec3 onB = b.center;
        for (int k = 0; k < 3; ++k) {
            if (k != edgeA) onA += axesA[k] * (a.half[k] * (glm::dot(axesA[k], normal) > 0.0f ? 1.0f : -1.0f));
            if (k != edgeB) onB += axesB[k] * (b.half[k] * (glm::dot(axesB[k], normal) < 0.0f ? 1.0f : -1.0f));
        }
        const Vec3& da = axesA[edgeA];
        const Vec3& db = axesB[edgeB];
        const Vec3 offset = onA - onB;
        const f32 cosine = glm::dot(da, db);
        const f32 denominator = 1.0f - cosine * cosine;
        if (denominator < 1e-6f) {
            return result;  // parallel edges: the caller falls back to corners
        }
        const f32 c = glm::dot(da, offset);
        const f32 f = glm::dot(db, offset);
        const f32 alongA = std::clamp((cosine * f - c) / denominator, -a.half[edgeA], a.half[edgeA]);
        const f32 alongB = std::clamp((f - cosine * c) / denominator, -b.half[edgeB], b.half[edgeB]);
        const Vec3 point = ((onA + da * alongA) + (onB + db * alongB)) * 0.5f;
        result.found = true;
        result.incidentIsB = true;  // pushes B away from A along the normal
        result.normal = normal;
        result.points.push_back({point, -bestEdge});
        return result;
    }

    // Reference face: on the reference box, facing the other one.
    const bool referenceIsA = bestAxis < 3;
    const Box& reference = referenceIsA ? a : b;
    const Box& incident = referenceIsA ? b : a;
    const Mat3& referenceAxes = referenceIsA ? axesA : axesB;
    const Mat3& incidentAxes = referenceIsA ? axesB : axesA;
    const int axis = bestAxis % 3;
    Vec3 normal = referenceAxes[axis];
    if (glm::dot(normal, incident.center - reference.center) < 0.0f) {
        normal = -normal;
    }
    // Incident face: the one most turned against the normal.
    int incidentAxis = 0;
    f32 most = -1.0f;
    for (int i = 0; i < 3; ++i) {
        const f32 alignment = std::abs(glm::dot(incidentAxes[i], normal));
        if (alignment > most) {
            most = alignment;
            incidentAxis = i;
        }
    }
    const f32 side = glm::dot(incidentAxes[incidentAxis], normal) > 0.0f ? -1.0f : 1.0f;
    const Vec3 faceCenter = incident.center + incidentAxes[incidentAxis] * (side * incident.half[incidentAxis]);
    const int u = (incidentAxis + 1) % 3;
    const int v = (incidentAxis + 2) % 3;
    const Vec3 du = incidentAxes[u] * incident.half[u];
    const Vec3 dv = incidentAxes[v] * incident.half[v];
    std::vector<Vec3> polygon = {faceCenter + du + dv, faceCenter - du + dv, faceCenter - du - dv, faceCenter + du - dv};

    // Clip it to the reference face's rectangle (Sutherland-Hodgman).
    for (int k = 1; k <= 2; ++k) {
        const int side2 = (axis + k) % 3;
        for (const f32 sign : {1.0f, -1.0f}) {
            const Vec3 plane = referenceAxes[side2] * sign;
            const f32 limit = glm::dot(plane, reference.center) + reference.half[side2];
            std::vector<Vec3> clipped;
            for (usize i = 0; i < polygon.size(); ++i) {
                const Vec3& from = polygon[i];
                const Vec3& to = polygon[(i + 1) % polygon.size()];
                const f32 df = glm::dot(plane, from) - limit;
                const f32 dt = glm::dot(plane, to) - limit;
                if (df <= 0.0f) {
                    clipped.push_back(from);
                }
                if ((df < 0.0f) != (dt < 0.0f)) {
                    clipped.push_back(from + (to - from) * (df / (df - dt)));
                }
            }
            polygon = std::move(clipped);
            if (polygon.empty()) {
                return result;
            }
        }
    }
    // Points within reach of the reference face become contacts.
    const f32 facePlane = glm::dot(normal, reference.center) + reference.half[axis];
    for (const Vec3& point : polygon) {
        const f32 gap = glm::dot(normal, point) - facePlane;
        if (gap < margin) {
            result.points.push_back({point, -gap});
        }
    }
    result.found = !result.points.empty();
    result.incidentIsB = referenceIsA;
    result.normal = normal;
    return result;
}

}  // namespace

void stepBoxes(ecs::World& world, f32 dt, const Settings& settings, const std::vector<Box>& staticBoxes,
               const std::vector<SphereShape>& spheres, std::vector<Contact>* contacts) {
    if (dt <= 0.0f) {
        return;
    }
    std::vector<SolverBody> bodies;
    std::vector<BoxShape> boxes;
    world.view<Transform, const BoxCollider, RigidBody>().exclude<Trigger>().each(
        [&](ecs::Entity entity, Transform& transform, const BoxCollider& collider, RigidBody& body) {
            const Vec3 half = collider.halfExtents * glm::abs(transform.scale);
            // Broken bodies (NaN or infinite state, from a bad file or a
            // blown-up simulation) are left out, as in the sphere step:
            // their bounds would poison the broad phase's sort.
            const bool brokenRotation = !std::isfinite(glm::dot(transform.rotation, transform.rotation));
            if (!finite(transform.position) || !finite(half) || !(body.mass > 0.0f) || !std::isfinite(body.mass) ||
                !finite(body.velocity) || !finite(body.angularVelocity) || brokenRotation) {
                return;
            }
            // A solid box: I = m / 12 (h^2 + d^2) about the x axis, and so on.
            const Vec3 squared = (half * 2.0f) * (half * 2.0f);
            const Vec3 inertia = body.mass / 12.0f *
                                 Vec3(squared.y + squared.z, squared.x + squared.z, squared.x + squared.y);
            boxes.push_back({bodies.size(), &transform, &collider, half, Vec3(1.0f) / glm::max(inertia, Vec3(1e-9f)), {}});
            bodies.push_back({entity, &body, transform.position, 0.0f, Mat3(0.0f), glm::length(half)});
        });
    if (boxes.empty()) {
        return;
    }

    const bool sleeping = settings.sleepSpeed > 0.0f;
    const f32 wakeSpeed = 2.0f * glm::length(settings.gravity) * dt + settings.sleepSpeed;
    // Gives a body its mass and inertia: it takes part in the solve.
    const auto activate = [&](SolverBody& solver, const Vec3& inverseInertiaLocal, const Quat& rotation) {
        solver.inverseMass = 1.0f / solver.body->mass;
        const Mat3 axes = glm::mat3_cast(glm::normalize(rotation));
        Mat3 local(0.0f);
        local[0][0] = inverseInertiaLocal.x;
        local[1][1] = inverseInertiaLocal.y;
        local[2][2] = inverseInertiaLocal.z;
        solver.inverseInertia = axes * local * glm::transpose(axes);
    };

    for (BoxShape& box : boxes) {
        SolverBody& solver = bodies[box.body];
        RigidBody& body = *solver.body;
        if (body.asleep && (!sleeping || body.velocity != Vec3(0.0f) || body.angularVelocity != Vec3(0.0f))) {
            wake(body);
        }
        body.previousPosition = box.transform->position;
        body.previousRotation = box.transform->rotation;
        body.hasPrevious = true;
        box.shape = makeBox(solver.entity, *box.transform, *box.collider);
        if (body.asleep) {
            continue;
        }
        body.velocity += settings.gravity * (body.gravityScale * dt);
        if (body.linearDamping > 0.0f) body.velocity *= std::exp(-body.linearDamping * dt);
        if (body.angularDamping > 0.0f) body.angularVelocity *= std::exp(-body.angularDamping * dt);
        activate(solver, box.inverseInertiaLocal, box.transform->rotation);
        box.margin = std::max(kSlop * 2.0f, 1.5f * dt * (glm::length(body.velocity) +
                                                         glm::length(body.angularVelocity) * solver.reach));
    }

    // Dynamic spheres join as bodies (solid-sphere inertia) when a box
    // comes near them.
    std::vector<std::optional<usize>> sphereBody(spheres.size());
    const auto sphereSolver = [&](usize s) -> std::optional<usize> {
        const SphereShape& sphere = spheres[s];
        if (sphere.body == nullptr || !(sphere.body->mass > 0.0f) || !finite(sphere.body->velocity) ||
            !finite(sphere.body->angularVelocity)) {
            return std::nullopt;  // static, or broken: met as static
        }
        if (!sphereBody[s]) {
            SolverBody solver{sphere.entity, sphere.body, sphere.center, 0.0f, Mat3(0.0f), sphere.radius};
            if (!sphere.body->asleep) {
                activate(solver, Vec3(1.0f / (0.4f * sphere.body->mass * sphere.radius * sphere.radius)),
                         Quat(1.0f, 0.0f, 0.0f, 0.0f));
            }
            sphereBody[s] = bodies.size();
            bodies.push_back(solver);
        }
        return sphereBody[s];
    };

    // Contacts. Corners near enough to reach a surface during this step
    // count too, as speculative contacts with a negative depth (the gap):
    // the solver lets them close the gap but not cross it, so a fast box
    // cannot pass through a thin floor or another box between two steps.
    std::vector<ContactPoint> found;
    // Corners of `inner` inside (or near) `outer`: contacts pushing the
    // inner one out.
    const auto cornersInto = [&](const Box& inner, usize innerBody, std::optional<usize> outerBody, const Box& outer,
                                 f32 margin, bool flip) {
        for (const Vec3& corner : corners(inner)) {
            if (const auto near = sphereBoxContact(outer, corner, margin)) {
                // From inner's side the push is -near.normal; seen from the
                // other body (a static box's corner in a dynamic one), the
                // dynamic one is pushed the other way.
                if (flip) {
                    found.push_back({*outerBody, std::nullopt, inner.entity, corner, near->normal, near->depth - margin});
                } else {
                    found.push_back({innerBody, outerBody, outer.entity, corner, -near->normal, near->depth - margin});
                }
            }
        }
    };
    // A dynamic box against another box: faces where they can be found,
    // corners otherwise (edge to edge).
    const auto meet = [&](const BoxShape& box, const Box& other, std::optional<usize> otherBody, f32 margin) {
        const FaceContact face = faceContact(box.shape, other, margin);
        if (face.found) {
            // Contacts push `box` (the A side) away from `other`.
            const Vec3 normal = face.incidentIsB ? -face.normal : face.normal;
            for (const FacePoint& point : face.points) {
                found.push_back({box.body, otherBody, other.entity, point.point, normal, point.depth});
            }
        } else if (otherBody) {
            cornersInto(box.shape, box.body, otherBody, other, margin, false);
            cornersInto(other, *otherBody, box.body, box.shape, margin, false);
        } else {
            cornersInto(box.shape, box.body, std::nullopt, other, margin, false);
            cornersInto(other, 0, box.body, box.shape, 0.0f, true);
        }
    };

    // Broad phase: every box and sphere as a world-space box (grown by its
    // reach this step), sorted along X and swept; only pairs overlapping on
    // all three axes and involving a dynamic box go on.
    enum class Kind : u8 { Crate, StaticBox, Sphere };
    struct Proxy {
        Vec3 low;
        Vec3 high;
        Kind kind;
        usize index;
    };
    std::vector<Proxy> proxies;
    proxies.reserve(boxes.size() + staticBoxes.size() + spheres.size());
    for (usize i = 0; i < boxes.size(); ++i) {
        proxies.push_back({boxes[i].shape.boundsMin - Vec3(boxes[i].margin), boxes[i].shape.boundsMax + Vec3(boxes[i].margin),
                           Kind::Crate, i});
    }
    for (usize i = 0; i < staticBoxes.size(); ++i) {
        proxies.push_back({staticBoxes[i].boundsMin, staticBoxes[i].boundsMax, Kind::StaticBox, i});
    }
    for (usize i = 0; i < spheres.size(); ++i) {
        const SphereShape& sphere = spheres[i];
        const f32 speed = sphere.body != nullptr ? glm::length(sphere.body->velocity) : 0.0f;
        const f32 reach = sphere.radius + (std::isfinite(speed) ? 1.5f * dt * speed : 0.0f);
        proxies.push_back({sphere.center - Vec3(reach), sphere.center + Vec3(reach), Kind::Sphere, i});
    }
    std::sort(proxies.begin(), proxies.end(), [](const Proxy& a, const Proxy& b) {
        return a.low.x != b.low.x ? a.low.x < b.low.x
                                  : (a.kind != b.kind ? a.kind < b.kind : a.index < b.index);  // deterministic
    });
    const auto asleep = [&](const BoxShape& box) { return bodies[box.body].body->asleep; };
    const auto handle = [&](const Proxy& first, const Proxy& second) {
        // Order the pair: a crate first.
        const Proxy& p = first.kind == Kind::Crate ? first : second;
        const Proxy& q = first.kind == Kind::Crate ? second : first;
        if (p.kind != Kind::Crate) {
            return;  // no dynamic box: handled elsewhere, or static
        }
        const BoxShape& box = boxes[p.index];
        switch (q.kind) {
            case Kind::Crate: {
                const BoxShape& other = boxes[q.index];
                if (!asleep(box) || !asleep(other)) {
                    meet(box, other.shape, other.body, std::max(box.margin, other.margin));
                }
                break;
            }
            case Kind::StaticBox:
                if (!asleep(box)) {
                    meet(box, staticBoxes[q.index], std::nullopt, box.margin);
                }
                break;
            case Kind::Sphere: {
                const SphereShape& sphere = spheres[q.index];
                const bool sphereMoves = sphere.body != nullptr && !sphere.body->asleep;
                if (asleep(box) && !sphereMoves) {
                    break;
                }
                const f32 sphereSpeed = sphere.body != nullptr ? glm::length(sphere.body->velocity) : 0.0f;
                const f32 margin = box.margin + (std::isfinite(sphereSpeed) ? 1.5f * dt * sphereSpeed : 0.0f);
                if (const auto touch = sphereBoxContact(box.shape, sphere.center, sphere.radius + margin)) {
                    found.push_back({box.body, sphereSolver(q.index), sphere.entity,
                                     sphere.center + touch->normal * sphere.radius, touch->normal, touch->depth - margin});
                }
                break;
            }
        }
    };
    for (usize i = 0; i < proxies.size(); ++i) {
        for (usize j = i + 1; j < proxies.size() && proxies[j].low.x <= proxies[i].high.x; ++j) {
            const Proxy& a = proxies[i];
            const Proxy& b = proxies[j];
            if (a.low.y <= b.high.y && b.low.y <= a.high.y && a.low.z <= b.high.z && b.low.z <= a.high.z) {
                handle(a, b);
            }
        }
    }

    // A sleeper hit harder than resting contact wakes and joins the solve;
    // merely resting on it leaves it asleep, and static, so piles can sleep.
    for (const ContactPoint& contact : found) {
        for (const auto& [sleeper, mover] : {std::pair{contact.b, std::optional(contact.a)},
                                             std::pair{std::optional(contact.a), contact.b}}) {
            if (!sleeper || !mover || !bodies[*sleeper].body->asleep || bodies[*mover].body->asleep) {
                continue;
            }
            if (std::abs(glm::dot(bodies[*mover].body->velocity, contact.normal)) > wakeSpeed) {
                SolverBody& woken = bodies[*sleeper];
                wake(*woken.body);
                const auto box = std::find_if(boxes.begin(), boxes.end(), [&](const BoxShape& b) { return b.body == *sleeper; });
                if (box != boxes.end()) {
                    activate(woken, box->inverseInertiaLocal, box->transform->rotation);
                } else {
                    activate(woken, Vec3(1.0f / (0.4f * woken.body->mass * woken.reach * woken.reach)),
                             Quat(1.0f, 0.0f, 0.0f, 0.0f));
                }
            }
        }
    }

    // Solve: sequential impulses, accumulated and clamped, so a box resting
    // on several corners settles instead of rocking.
    const auto bodyOf = [&](const std::optional<usize>& index) -> SolverBody* {
        return index ? &bodies[*index] : nullptr;
    };
    const auto relativeVelocity = [&](const ContactPoint& contact) {
        Vec3 velocity = velocityAt(bodies[contact.a], contact.armA);
        if (const SolverBody* b = bodyOf(contact.b)) {
            velocity -= velocityAt(*b, contact.armB);
        }
        return velocity;
    };
    const auto push = [&](const ContactPoint& contact, const Vec3& impulse) {
        applyImpulse(bodies[contact.a], contact.armA, impulse);
        if (SolverBody* b = bodyOf(contact.b)) {
            applyImpulse(*b, contact.armB, -impulse);
        }
    };
    for (ContactPoint& contact : found) {
        const SolverBody& a = bodies[contact.a];
        const SolverBody* b = bodyOf(contact.b);
        contact.armA = contact.point - a.center;
        contact.armB = b != nullptr ? contact.point - b->center : Vec3(0.0f);
        const Vec3 helper = std::abs(contact.normal.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
        contact.tangents[0] = glm::normalize(glm::cross(contact.normal, helper));
        contact.tangents[1] = glm::cross(contact.normal, contact.tangents[0]);
        const auto mass = [&](const Vec3& direction) {
            const f32 k = resistance(a, contact.armA, direction) + (b != nullptr ? resistance(*b, contact.armB, direction) : 0.0f);
            return k > 1e-12f ? 1.0f / k : 0.0f;
        };
        contact.normalMass = mass(contact.normal);
        contact.tangentMass = {mass(contact.tangents[0]), mass(contact.tangents[1])};
        // Static colliders bounce fully and grip at 0.5, as for spheres.
        const f32 restitution = std::min(a.body->restitution, b != nullptr ? b->body->restitution : 1.0f);
        contact.friction = std::sqrt(std::max(a.body->friction, 0.0f) * (b != nullptr ? std::max(b->body->friction, 0.0f) : 0.5f));
        const f32 approaching = glm::dot(relativeVelocity(contact), contact.normal);
        const f32 bounce = approaching < -wakeSpeed ? -std::min(restitution, 1.0f) * approaching : 0.0f;
        if (contact.depth < 0.0f) {
            // Not touching yet. Hit during this step, it bounces now (a
            // gap early at most), or it would arrive with no speed left to
            // bounce; otherwise it may close the gap but not cross it.
            const bool hitThisStep = approaching * dt < contact.depth;
            contact.target = hitThisStep && bounce > 0.0f ? bounce : contact.depth / dt;
        } else {
            contact.target = std::max(bounce, kBaumgarte / dt * std::max(contact.depth - kSlop, 0.0f));
        }
    }
    for (u32 iteration = 0; iteration < kIterations; ++iteration) {
        for (ContactPoint& contact : found) {
            const f32 normalSpeed = glm::dot(relativeVelocity(contact), contact.normal);
            const f32 previous = contact.normalImpulse;
            contact.normalImpulse = std::max(previous + (contact.target - normalSpeed) * contact.normalMass, 0.0f);
            push(contact, contact.normal * (contact.normalImpulse - previous));

            const f32 limit = contact.friction * contact.normalImpulse;
            for (usize t = 0; t < 2; ++t) {
                const f32 slide = glm::dot(relativeVelocity(contact), contact.tangents[t]);
                const f32 before = contact.tangentImpulse[t];
                contact.tangentImpulse[t] = std::clamp(before - slide * contact.tangentMass[t], -limit, limit);
                push(contact, contact.tangents[t] * (contact.tangentImpulse[t] - before));
            }
        }
    }

    // Move, turn, and maybe sleep: the boxes. Spheres move in their own
    // step, with the velocities the contacts gave them.
    for (BoxShape& box : boxes) {
        RigidBody& body = *bodies[box.body].body;
        if (body.asleep) {
            continue;
        }
        box.transform->position += body.velocity * dt;
        if (const f32 rate = glm::length(body.angularVelocity); rate > 1e-6f) {
            box.transform->rotation =
                glm::normalize(glm::angleAxis(rate * dt, body.angularVelocity / rate) * box.transform->rotation);
        }
        if (sleeping) {
            if (glm::length(body.velocity) < settings.sleepSpeed &&
                glm::length(body.angularVelocity) * bodies[box.body].reach < settings.sleepSpeed) {
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

    // One collision per body and collider touched, as hard as all its
    // contact points together.
    if (contacts != nullptr) {
        for (usize i = 0; i < found.size();) {
            const ContactPoint& first = found[i];
            f32 impulse = 0.0f;
            Vec3 point(0.0f);
            Vec3 normal(0.0f);
            usize count = 0;
            for (; i < found.size() && found[i].a == first.a && found[i].other == first.other; ++i) {
                impulse += found[i].normalImpulse;
                point += found[i].point;
                normal += found[i].normal;
                ++count;
            }
            if (impulse >= settings.minimumContactImpulse) {
                const f32 length = glm::length(normal);
                contacts->push_back({bodies[first.a].entity, first.other, point / static_cast<f32>(count),
                                     length > 0.0f ? -normal / length : Vec3(0.0f, -1.0f, 0.0f), impulse});
            }
        }
    }
}

}  // namespace bdex::physics::detail
