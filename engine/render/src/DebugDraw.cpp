#include <bdex/render/DebugDraw.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace bdex::render {

void DebugDraw::line(const Vec3& from, const Vec3& to, const Vec4& color) {
    vertices_.push_back({from, color});
    vertices_.push_back({to, color});
}

void DebugDraw::box(const Vec3& min, const Vec3& max, const Vec4& color) {
    const auto corner = [&](int i) {
        return Vec3((i & 1) != 0 ? max.x : min.x, (i & 2) != 0 ? max.y : min.y,
                    (i & 4) != 0 ? max.z : min.z);
    };
    // The 12 edges join corners differing in exactly one coordinate.
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if ((i & bit) == 0) {
                line(corner(i), corner(i | bit), color);
            }
        }
    }
}

void DebugDraw::sphere(const Vec3& center, f32 radius, const Vec4& color, u32 segments) {
    segments = std::max(segments, 3u);
    const f32 step = 2.0f * std::numbers::pi_v<f32> / static_cast<f32>(segments);
    const std::array<std::pair<Vec3, Vec3>, 3> planes = {{
        {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)},
        {Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)},
        {Vec3(0.0f, 0.0f, 1.0f), Vec3(1.0f, 0.0f, 0.0f)},
    }};
    for (const auto& [a, b] : planes) {
        for (u32 i = 0; i < segments; ++i) {
            const f32 t0 = step * static_cast<f32>(i);
            const f32 t1 = step * static_cast<f32>(i + 1);
            line(center + (a * std::cos(t0) + b * std::sin(t0)) * radius,
                 center + (a * std::cos(t1) + b * std::sin(t1)) * radius, color);
        }
    }
}

void DebugDraw::axes(const Mat4& transform, f32 length) {
    const Vec3 origin(transform[3]);
    for (int axis = 0; axis < 3; ++axis) {
        Vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
        color[axis] = 1.0f;
        line(origin, origin + Vec3(transform[axis]) * length, color);
    }
}

void DebugDraw::frustum(const Mat4& viewProjection, const Vec4& color) {
    const Mat4 inverse = glm::inverse(viewProjection);
    std::array<Vec3, 8> corners;
    for (int i = 0; i < 8; ++i) {
        const Vec4 clip((i & 1) != 0 ? 1.0f : -1.0f, (i & 2) != 0 ? 1.0f : -1.0f,
                        (i & 4) != 0 ? 1.0f : 0.0f, 1.0f);
        const Vec4 world = inverse * clip;
        corners[static_cast<usize>(i)] = Vec3(world) / world.w;
    }
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if ((i & bit) == 0) {
                line(corners[static_cast<usize>(i)], corners[static_cast<usize>(i | bit)], color);
            }
        }
    }
}

}  // namespace bdex::render
