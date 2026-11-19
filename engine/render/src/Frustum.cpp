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

}  // namespace bdex::render
