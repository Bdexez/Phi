#pragma once

#include <phi/core/JobSystem.hpp>
#include <phi/ecs/World.hpp>
#include <phi/scene/Scene.hpp>

namespace sandbox {

/// Application-defined component: rotates the entity around an axis.
struct Spin {
    phi::Vec3 axis{0.0f, 1.0f, 0.0f};
    phi::f32 speed = 1.0f;  // radians per second
};

/// Application-defined system. Systems are plain functions over ECS views;
/// this one only touches each entity's own components, so it can run on the
/// job system's threads (a handful of entities stay on the calling thread).
inline void spinSystem(phi::ecs::World& world, phi::JobSystem& jobs, phi::f32 deltaSeconds) {
    world.view<phi::scene::Transform, const Spin>().parallelEach(
        jobs, [deltaSeconds](phi::scene::Transform& transform, const Spin& spin) {
            transform.rotation =
                glm::normalize(glm::angleAxis(spin.speed * deltaSeconds, spin.axis) * transform.rotation);
        });
}

}  // namespace sandbox
