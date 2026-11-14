#pragma once

#include "Palette.hpp"

#include <phi/scene/Scene.hpp>

namespace sandbox {

/// Where the gliding platform rests; it swings 3 m either way along Z.
inline constexpr phi::Vec3 kPlatformBase{-7.5f, 0.16f, 1.0f};

/// The entities the sandbox drives after building the level.
struct LevelEntities {
    phi::ecs::Entity camera;
    phi::ecs::Entity platform;
};

/// Builds the sandbox's scene in `level`: the sun, the camera, the ground,
/// the gliding platform, the glass sphere and block, a ring of spinning
/// crates, and two moons carrying colored lamps around a pivot. Drawn with
/// `palette` (whose handles stay invalid without a renderer: the scene
/// still simulates). The lamp posts come separately (Lamps).
LevelEntities buildLevel(phi::scene::Scene& level, const Palette& palette);

}  // namespace sandbox
