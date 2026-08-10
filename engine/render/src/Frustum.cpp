#include <bdex/render/Frustum.hpp>

#include <algorithm>
#include <cmath>

namespace bdex::render {

BoundingSphere computeBoundingSphere(const assets::MeshData& mesh) {
    if (mesh.vertices.empty()) {
        return {};
    }
    Vec3 low = mesh.vertices.front().position;
    Vec3 high = low;
    for (const assets::Vertex& vertex : mesh.vertices) {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
    }
    BoundingSphere sphere;
    sphere.center = (low + high) * 0.5f;
    f32 radiusSquared = 0.0f;
    for (const assets::Vertex& vertex : mesh.vertices) {
        const Vec3 offset = vertex.position - sphere.center;
        radiusSquared = std::max(radiusSquared, glm::dot(offset, offset));
    }
    sphere.radius = std::sqrt(radiusSquared);
    return sphere;
}

BoundingSphere transformSphere(const BoundingSphere& sphere, const Mat4& transform) {
    const f32 scaleSquared = std::max({glm::dot(Vec3(transform[0]), Vec3(transform[0])),
                                       glm::dot(Vec3(transform[1]), Vec3(transform[1])),
                                       glm::dot(Vec3(transform[2]), Vec3(transform[2]))});
    return {Vec3(transform * Vec4(sphere.center, 1.0f)), sphere.radius * std::sqrt(scaleSquared)};
}

Frustum::Frustum(const Mat4& viewProjection) {
    // Gribb & Hartmann: each plane is a sum or difference of rows of the
    // matrix (GLM stores columns, so row i is m[*][i]).
    const auto row = [&](int i) {
        return Vec4(viewProjection[0][i], viewProjection[1][i], viewProjection[2][i],
                    viewProjection[3][i]);
    };
    planes_ = {
        row(3) + row(0),  // left:   -w <= x
        row(3) - row(0),  // right:   x <= w
        row(3) + row(1),  // y >= -w
        row(3) - row(1),  // y <= w
        row(2),           // near:    0 <= z
        row(3) - row(2),  // far:     z <= w
    };
    for (Vec4& plane : planes_) {
        const f32 length = glm::length(Vec3(plane));
        if (length > 0.0f) {
            plane /= length;
        }
    }
}

bool Frustum::intersects(const BoundingSphere& sphere) const {
    return std::all_of(planes_.begin(), planes_.end(), [&](const Vec4& plane) {
        return glm::dot(Vec3(plane), sphere.center) + plane.w >= -sphere.radius;
    });
}

Ray rayThroughPixel(const Mat4& viewProjection, const Vec2& pixel, Extent2D extent) {
    const Vec2 ndc((pixel.x / static_cast<f32>(std::max(extent.width, 1u))) * 2.0f - 1.0f,
                   (pixel.y / static_cast<f32>(std::max(extent.height, 1u))) * 2.0f - 1.0f);
    const Mat4 inverse = glm::inverse(viewProjection);
    const auto unproject = [&](f32 depth) {
        const Vec4 world = inverse * Vec4(ndc, depth, 1.0f);
        return Vec3(world) / world.w;
    };
    const Vec3 nearPoint = unproject(0.0f);
    return {nearPoint, glm::normalize(unproject(1.0f) - nearPoint)};
}

std::optional<f32> intersect(const Ray& ray, const BoundingSphere& sphere) {
    const Vec3 toCenter = sphere.center - ray.origin;
    const f32 along = glm::dot(toCenter, ray.direction);
    const f32 radiusSquared = sphere.radius * sphere.radius;
    const f32 distanceSquared = glm::dot(toCenter, toCenter);
    if (distanceSquared <= radiusSquared) {
        return 0.0f;  // starts inside
    }
    if (along < 0.0f) {
        return std::nullopt;  // behind
    }
    const f32 missSquared = distanceSquared - along * along;
    if (missSquared > radiusSquared) {
        return std::nullopt;
    }
    return along - std::sqrt(radiusSquared - missSquared);
}

}  // namespace bdex::render
