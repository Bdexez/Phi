#pragma once

#include <phi/core/Result.hpp>
#include <phi/render/Renderer.hpp>
#include <phi/render/ResourceNames.hpp>

#include <array>

namespace sandbox {

/// The sandbox's meshes and materials, made once on the GPU and named for
/// scene files ("cube", "ground", "cube 3"...).
struct Palette {
    static constexpr phi::u32 kCubeCount = 8;

    phi::render::MeshHandle cube;
    phi::render::MeshHandle sphere;
    phi::render::MeshHandle plane;
    phi::render::MaterialHandle ground;    ///< Checkered tiles, beveled.
    phi::render::MaterialHandle polished;  ///< The centerpiece sphere.
    phi::render::MaterialHandle glass;     ///< Tinted, see-through.
    phi::render::MaterialHandle ball;      ///< Red rubber, for thrown balls.
    std::array<phi::render::MaterialHandle, 2> moons{};  ///< Glowing orange, then blue.
    std::array<phi::render::MaterialHandle, kCubeCount> cubes{};  ///< Painted crates around the hue circle.
    phi::render::ResourceNames names;

    phi::Result<> create(phi::render::IRenderer& gfx);
};

}  // namespace sandbox
