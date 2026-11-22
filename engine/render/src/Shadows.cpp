#include <phi/render/Shadows.hpp>

#include <algorithm>
#include <cmath>

namespace phi::render {

CameraData fitDirectionalShadow(const CameraData& camera, const Vec3& lightDirection, f32 distance,
                                u32 mapSize) {
    return fitDirectionalShadow(camera, lightDirection, 0.0f, distance, mapSize);
}

CameraData fitDirectionalShadow(const CameraData& camera, const Vec3& lightDirection, f32 nearDistance,
                                f32 farDistance, u32 mapSize) {
    const f32 directionLength = glm::length(lightDirection);
    const Vec3 direction =
        directionLength > 0.0f ? lightDirection / directionLength : Vec3(0.0f, -1.0f, 0.0f);
    const Vec3 upHint = std::abs(direction.y) > 0.99f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f);

    // Sphere around the slice of the view frustum: centered halfway along
    // the view axis, reaching the far corners (the near ones are closer).
    // Row 2 of an orthonormal view matrix is the camera's +Z (back) axis.
    const Vec3 back = glm::normalize(Vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]));
    const f32 tanX = 1.0f / std::abs(camera.projection[0][0]);
    const f32 tanY = 1.0f / std::abs(camera.projection[1][1]);
    const f32 halfDepth = (farDistance - nearDistance) * 0.5f;
    Vec3 center = camera.position - back * (nearDistance + halfDepth);
    const f32 farSpread = farDistance * farDistance * (tanX * tanX + tanY * tanY);
    const f32 radius = std::ceil(std::sqrt(halfDepth * halfDepth + farSpread) * 16.0f) / 16.0f;
    const f32 distance = farDistance;

    // Snap the center to whole texels in the light's plane (orthonormal rotation:
    // its inverse is its transpose).
    const Mat3 rotation(glm::lookAt(Vec3(0.0f), direction, upHint));
    const f32 texel = 2.0f * radius / static_cast<f32>(mapSize);
    Vec3 snapped = rotation * center;
    snapped.x = std::floor(snapped.x / texel) * texel;
    snapped.y = std::floor(snapped.y / texel) * texel;
    center = glm::transpose(rotation) * snapped;

    const f32 reach = distance;
    const Vec3 eye = center - direction * (radius + reach);
    CameraData light;
    light.view = glm::lookAt(eye, center, upHint);
    light.projection = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + reach);
    light.projection[1][1] *= -1.0f;
    light.viewProjection = light.projection * light.view;
    light.position = eye;
    return light;
}

std::vector<f32> cascadeSplits(f32 nearPlane, f32 distance, u32 count, f32 lambda) {
    std::vector<f32> splits;
    splits.reserve(count);
    nearPlane = std::max(nearPlane, 1e-3f);
    for (u32 i = 1; i <= count; ++i) {
        const f32 fraction = static_cast<f32>(i) / static_cast<f32>(count);
        const f32 logarithmic = nearPlane * std::pow(distance / nearPlane, fraction);
        const f32 uniform = nearPlane + (distance - nearPlane) * fraction;
        splits.push_back(lambda * logarithmic + (1.0f - lambda) * uniform);
    }
    return splits;
}

}  // namespace phi::render
