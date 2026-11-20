#pragma once

#include <bdex/core/JobSystem.hpp>
#include <bdex/ecs/World.hpp>
#include <bdex/scene/Scene.hpp>

namespace sandbox {

/// Application-defined component: rotates the entity around an axis.
struct Spin {
    bdex::Vec3 axis{0.0f, 1.0f, 0.0f};
    bdex::f32 speed = 1.0f;  // radians per second
};

/// Application-defined system. Systems are plain functions over ECS views;
/// this one only touches each entity's own components, so it can run on the
/// job system's threads (a handful of entities stay on the calling thread).
inline void spinSystem(bdex::ecs::World& world, bdex::JobSystem& jobs, bdex::f32 deltaSeconds) {
    world.view<bdex::scene::Transform, const Spin>().parallelEach(
        jobs, [deltaSeconds](bdex::scene::Transform& transform, const Spin& spin) {
            transform.rotation =
                glm::normalize(glm::angleAxis(spin.speed * deltaSeconds, spin.axis) * transform.rotation);
        });
}

}  // namespace sandbox
