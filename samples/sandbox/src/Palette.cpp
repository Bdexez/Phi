#include "Palette.hpp"

#include <phi/assets/Image.hpp>
#include <phi/assets/Mesh.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace sandbox {

using namespace phi;

namespace {

/// Normal map of `cells` x `cells` square tiles whose edges slope down
/// over `bevel` texels.
assets::ImageData beveledTiles(u32 size, u32 cells, f32 bevel, f32 strength) {
    const f32 cell = static_cast<f32>(size) / static_cast<f32>(cells);
    std::vector<f32> heights(static_cast<usize>(size) * size);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const f32 u = std::fmod(static_cast<f32>(x) + 0.5f, cell);
            const f32 v = std::fmod(static_cast<f32>(y) + 0.5f, cell);
            const f32 edge = std::min({u, v, cell - u, cell - v});
            heights[static_cast<usize>(y) * size + x] = std::min(edge, bevel);
        }
    }
    return assets::makeNormalMap(heights, size, size, strength / bevel);
}

Vec3 hsvToRgb(f32 hue, f32 saturation, f32 value) {
    const auto channel = [&](f32 offset) {
        const f32 k = std::fmod(offset + hue * 6.0f, 6.0f);
        return value - value * saturation * std::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f);
    };
    return {channel(5.0f), channel(3.0f), channel(1.0f)};
}

}  // namespace

Result<> Palette::create(render::IRenderer& gfx) {
    cube = gfx.createMesh(assets::makeCube(1.0f));
    sphere = gfx.createMesh(assets::makeSphere(0.5f, 48, 24));
    plane = gfx.createMesh(assets::makePlane(40.0f, 20.0f));

    const render::TextureHandle checker = gfx.createTexture(
        assets::makeCheckerboard(256, 8, {235, 235, 235, 255}, {120, 124, 130, 255}));
    const render::TextureHandle crate = gfx.createTexture(
        assets::makeCheckerboard(64, 4, {255, 255, 255, 255}, {200, 200, 200, 255}));

    // Beveled tiles, matching the checker cells, give the floor and the
    // crates some relief through normal maps.
    const render::TextureHandle tiles = gfx.createTexture(beveledTiles(256, 8, 6.0f, 3.0f),
                                                          render::TextureColorSpace::Linear);
    const render::TextureHandle panels = gfx.createTexture(beveledTiles(64, 4, 3.0f, 2.0f),
                                                           render::TextureColorSpace::Linear);

    render::MaterialDesc groundDesc;
    groundDesc.baseColor = Vec4(0.55f, 0.6f, 0.55f, 1.0f);
    groundDesc.albedo = checker;
    groundDesc.normalMap = tiles;
    groundDesc.specular = 0.05f;
    ground = gfx.createMaterial(groundDesc);

    render::MaterialDesc polishedDesc;  // no texture: plain base color
    polishedDesc.baseColor = Vec4(0.95f, 0.95f, 1.0f, 1.0f);
    polishedDesc.specular = 0.9f;
    polishedDesc.shininess = 96.0f;
    polished = gfx.createMaterial(polishedDesc);

    // The moons glow with the color of the lamp they carry.
    for (u32 i = 0; i < 2; ++i) {
        render::MaterialDesc moon;
        moon.baseColor = Vec4(0.05f, 0.05f, 0.05f, 1.0f);
        moon.emissive = i == 0 ? Vec3(1.0f, 0.35f, 0.05f) : Vec3(0.05f, 0.4f, 1.0f);
        moons[i] = gfx.createMaterial(moon);
    }

    render::MaterialDesc rubber;
    rubber.baseColor = Vec4(0.95f, 0.3f, 0.2f, 1.0f);
    rubber.specular = 0.5f;
    ball = gfx.createMaterial(rubber);

    render::MaterialDesc glassDesc;  // tinted and see-through
    glassDesc.baseColor = Vec4(0.3f, 0.7f, 1.0f, 0.45f);
    glassDesc.specular = 1.0f;
    glassDesc.shininess = 128.0f;
    glassDesc.transparent = true;
    glass = gfx.createMaterial(glassDesc);

    for (u32 i = 0; i < kCubeCount; ++i) {
        render::MaterialDesc painted;
        painted.baseColor = Vec4(hsvToRgb(static_cast<f32>(i) / kCubeCount, 0.65f, 0.95f), 1.0f);
        painted.albedo = crate;
        painted.normalMap = panels;
        painted.specular = 0.4f;
        cubes[i] = gfx.createMaterial(painted);
    }

    // Names for scene files.
    names.name(plane, "plane");
    names.name(cube, "cube");
    names.name(sphere, "sphere");
    names.name(this->ground, "ground");
    names.name(ball, "ball");
    names.name(this->polished, "sphere");
    names.name(this->glass, "glass");
    names.name(moons[0], "moon 0");
    names.name(moons[1], "moon 1");
    for (u32 i = 0; i < kCubeCount; ++i) {
        names.name(cubes[i], std::format("cube {}", i));
    }
    return {};
}

}  // namespace sandbox
