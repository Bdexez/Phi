#pragma once

#include "Geometry.hpp"

#include <vector>

namespace phi::physics::detail {

/// Advances the dynamic boxes (BoxCollider with a RigidBody) by `dt`: they
/// meet the step's static boxes, its spheres (static, or dynamic ones
/// already moved this step, whose velocities the contacts change), and
/// each other. Collisions are appended to `contacts` when given.
void stepBoxes(ecs::World& world, f32 dt, const Settings& settings, const std::vector<Box>& staticBoxes,
               const std::vector<SphereShape>& spheres, std::vector<Contact>* contacts);

}  // namespace phi::physics::detail
