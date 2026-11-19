#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/core/Result.hpp>
#include <bdex/core/Types.hpp>

#include <string_view>
#include <vector>

namespace bdex::assets {

struct Vertex {
    Vec3 position{0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    Vec2 uv{0.0f};
    /// xyz: direction of increasing U on the surface. w (+1 or -1): the
    /// bitangent cross(normal, tangent) * w points up in the texture
    /// (decreasing V), so a normal map with Y up (OpenGL convention) maps
    /// to tangent * x + bitangent * y + normal * z.
    Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

/// CPU-side triangle mesh (counter-clockwise front faces). Renderer-agnostic:
/// a renderer uploads it and hands back its own handle.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<u32> indices;

    [[nodiscard]] usize triangleCount() const { return indices.size() / 3; }
};

/// Fills Vertex::tangent from positions, normals and UVs (per-triangle
/// gradients averaged per vertex, then made orthogonal to the normal).
/// Vertices whose UVs give no direction get any tangent perpendicular to
/// their normal. Primitives and the OBJ loader call it already.
void computeTangents(MeshData& mesh);

// --- Procedural primitives, centered on the origin ----------------------------

/// Axis-aligned cube with per-face normals and UVs.
[[nodiscard]] MeshData makeCube(f32 size = 1.0f);
/// Square in the XZ plane facing +Y. `uvScale` tiles the texture.
[[nodiscard]] MeshData makePlane(f32 size = 1.0f, f32 uvScale = 1.0f);
/// UV sphere.
[[nodiscard]] MeshData makeSphere(f32 radius = 0.5f, u32 segments = 32, u32 rings = 16);

// --- Loading -------------------------------------------------------------------

/// Parses Wavefront OBJ text (positions, normals, UVs, polygons triangulated
/// as fans, negative indices). Missing normals are computed by averaging
/// face normals.
[[nodiscard]] Result<MeshData> parseObj(std::string_view text);

}  // namespace bdex::assets
