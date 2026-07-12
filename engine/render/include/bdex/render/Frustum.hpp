#pragma once

#include <bdex/assets/Mesh.hpp>
#include <bdex/core/Math.hpp>

#include <array>

namespace bdex::render {

struct BoundingSphere {
    Vec3 center{0.0f};
    f32 radius = 0.0f;
};

/// Sphere around the mesh's axis-aligned bounds (empty mesh: radius 0).
[[nodiscard]] BoundingSphere computeBoundingSphere(const assets::MeshData& mesh);

/// The sphere after `transform`. Non-uniform scale grows the radius by the
/// largest axis scale, so the result always encloses the transformed mesh.
[[nodiscard]] BoundingSphere transformSphere(const BoundingSphere& sphere, const Mat4& transform);

/// The six planes of a view volume, for visibility tests.
class Frustum {
public:
    /// Planes of the clip volume of `viewProjection`, with the engine's
    /// [0, 1] depth range.
    explicit Frustum(const Mat4& viewProjection);

    /// False only when the sphere is entirely outside one of the planes
    /// (conservative: a sphere near a corner may be kept).
    [[nodiscard]] bool intersects(const BoundingSphere& sphere) const;

private:
    // Normalized, pointing inward: inside when dot(xyz, p) + w >= 0.
    std::array<Vec4, 6> planes_;
};

}  // namespace bdex::render
