#pragma once

#include <phi/assets/Mesh.hpp>
#include <phi/core/Math.hpp>
#include <phi/core/Types.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace phi::render {

struct BoundingSphere {
    Vec3 center{0.0f};
    f32 radius = 0.0f;
};

/// Sphere around the mesh's axis-aligned bounds (empty mesh: radius 0).
[[nodiscard]] BoundingSphere computeBoundingSphere(const assets::MeshData& mesh);

/// The sphere after `transform`. Non-uniform scale grows the radius by the
/// largest axis scale, so the result always encloses the transformed mesh.
[[nodiscard]] inline BoundingSphere transformSphere(const BoundingSphere& sphere, const Mat4& transform) {
    const f32 sx = transform[0][0] * transform[0][0] + transform[0][1] * transform[0][1] +
                   transform[0][2] * transform[0][2];
    const f32 sy = transform[1][0] * transform[1][0] + transform[1][1] * transform[1][1] +
                   transform[1][2] * transform[1][2];
    const f32 sz = transform[2][0] * transform[2][0] + transform[2][1] * transform[2][1] +
                   transform[2][2] * transform[2][2];
    const f32 scaleSquared = std::max({sx, sy, sz});
    const Vec3 center(
        transform[0][0] * sphere.center.x + transform[1][0] * sphere.center.y +
            transform[2][0] * sphere.center.z + transform[3][0],
        transform[0][1] * sphere.center.x + transform[1][1] * sphere.center.y +
            transform[2][1] * sphere.center.z + transform[3][1],
        transform[0][2] * sphere.center.x + transform[1][2] * sphere.center.y +
            transform[2][2] * sphere.center.z + transform[3][2]);
    return {center, sphere.radius * std::sqrt(scaleSquared)};
}

struct Ray {
    Vec3 origin{0.0f};
    Vec3 direction{0.0f, 0.0f, -1.0f};  ///< Unit length.
};

/// The ray from the eye through a pixel (top-left origin, pixel centers at
/// +0.5) of a `width` x `height` view rendered with `viewProjection`.
[[nodiscard]] Ray rayThroughPixel(const Mat4& viewProjection, const Vec2& pixel, Extent2D extent);

/// Distance along the ray to the sphere's surface (0 if the origin is
/// inside), or nothing when the ray misses it or it is behind.
[[nodiscard]] std::optional<f32> intersect(const Ray& ray, const BoundingSphere& sphere);

/// The six planes of a view volume, for visibility tests.
class Frustum {
public:
    /// Planes of the clip volume of `viewProjection`, with the engine's
    /// [0, 1] depth range.
    explicit Frustum(const Mat4& viewProjection);

    /// False only when the sphere is entirely outside one of the planes
    /// (conservative: a sphere near a corner may be kept).
    [[nodiscard]] bool intersects(const BoundingSphere& sphere) const {
        const Vec3 c = sphere.center;
        const f32 negR = -sphere.radius;
        for (const Vec4& plane : planes_) {
            if (plane.x * c.x + plane.y * c.y + plane.z * c.z + plane.w < negR) {
                return false;
            }
        }
        return true;
    }

private:
    // Normalized, pointing inward: inside when dot(xyz, p) + w >= 0.
    std::array<Vec4, 6> planes_;
};

}  // namespace phi::render
