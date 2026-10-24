#pragma once

#include <bdex/core/Result.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/render/ResourceNames.hpp>

#include <array>

namespace sandbox {

/// The sandbox's meshes and materials, made once on the GPU and named for
/// scene files ("cube", "ground", "cube 3"...).
struct Palette {
    static constexpr bdex::u32 kCubeCount = 8;

    bdex::render::MeshHandle cube;
    bdex::render::MeshHandle sphere;
    bdex::render::MeshHandle plane;
    bdex::render::MaterialHandle ground;    ///< Checkered tiles, beveled.
    bdex::render::MaterialHandle polished;  ///< The centerpiece sphere.
    bdex::render::MaterialHandle glass;     ///< Tinted, see-through.
    bdex::render::MaterialHandle ball;      ///< Red rubber, for thrown balls.
    std::array<bdex::render::MaterialHandle, 2> moons{};  ///< Glowing orange, then blue.
    std::array<bdex::render::MaterialHandle, kCubeCount> cubes{};  ///< Painted crates around the hue circle.
    bdex::render::ResourceNames names;

    bdex::Result<> create(bdex::render::IRenderer& gfx);
};

}  // namespace sandbox
