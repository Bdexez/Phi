#include <bdex/render/Shadows.hpp>

#include <cmath>

namespace bdex::render {

CameraData fitDirectionalShadow(const CameraData& camera, const Vec3& lightDirection, f32 distance,
                                u32 mapSize) {
    const f32 directionLength = glm::length(lightDirection);
    const Vec3 direction =
        directionLength > 0.0f ? lightDirection / directionLength : Vec3(0.0f, -1.0f, 0.0f);
    const Vec3 upHint = std::abs(direction.y) > 0.99f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f);

    // Sphere around the view frustum up to `distance`: centered halfway along
    // the view axis, reaching the far corners.
    const Mat4 cameraWorld = glm::inverse(camera.view);
    const Vec3 back = glm::normalize(Vec3(cameraWorld[2]));
    const f32 tanX = 1.0f / std::abs(camera.projection[0][0]);
    const f32 tanY = 1.0f / std::abs(camera.projection[1][1]);
    Vec3 center = camera.position - back * (distance * 0.5f);
    const f32 radius = std::ceil(distance * std::sqrt(0.25f + tanX * tanX + tanY * tanY));

    // Snap the center to whole texels in the light's plane.
    const Mat4 rotation = glm::lookAt(Vec3(0.0f), direction, upHint);
    const f32 texel = 2.0f * radius / static_cast<f32>(mapSize);
    Vec3 snapped = Vec3(rotation * Vec4(center, 1.0f));
    snapped.x = std::floor(snapped.x / texel) * texel;
    snapped.y = std::floor(snapped.y / texel) * texel;
    center = Vec3(glm::inverse(rotation) * Vec4(snapped, 1.0f));

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

}  // namespace bdex::render
