#pragma once

#include "Geometry.hpp"

#include <vector>

namespace bdex::physics::detail {

/// Advances the dynamic boxes (BoxCollider with a RigidBody) by `dt`
/// against the step's static boxes and spheres, appending their collisions
/// to `contacts` when given.
void stepBoxes(ecs::World& world, f32 dt, const Settings& settings, const std::vector<Box>& staticBoxes,
               const std::vector<SphereShape>& staticSpheres, std::vector<Contact>* contacts);

}  // namespace bdex::physics::detail
