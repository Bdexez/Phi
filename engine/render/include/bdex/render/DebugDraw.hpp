#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/render/Frustum.hpp>

#include <span>
#include <vector>

namespace bdex::render {

/// Immediate-mode debug lines: anything can add shapes during a frame
/// (bounds, paths, light ranges...), the renderer draws them that frame and
/// starts the next one empty. Colors are linear RGBA; lines are hidden by
/// geometry in front of them.
///
///   if (auto* debug = services.tryGet<render::DebugDraw>()) {
///       debug->sphere(enemy.position, enemy.alertRadius, {1, 0, 0, 1});
///   }
///
/// Main-thread only, like the rest of the frame.
class DebugDraw {
public:
    struct Vertex {
        Vec3 position;
        Vec4 color;
    };

    void line(const Vec3& from, const Vec3& to, const Vec4& color = Vec4(1.0f));
    /// Axis-aligned box.
    void box(const Vec3& min, const Vec3& max, const Vec4& color = Vec4(1.0f));
    /// Box of the given half extents, placed by `transform` (rotation and
    /// scale included).
    void box(const Mat4& transform, const Vec3& halfExtents, const Vec4& color = Vec4(1.0f));
    /// Three great circles, one per axis plane.
    void sphere(const Vec3& center, f32 radius, const Vec4& color = Vec4(1.0f), u32 segments = 32);
    void sphere(const BoundingSphere& bounds, const Vec4& color = Vec4(1.0f)) {
        sphere(bounds.center, bounds.radius, color);
    }
    /// The transform's X, Y and Z axes in red, green and blue.
    void axes(const Mat4& transform, f32 length = 1.0f);
    /// The edges of the volume a view-projection matrix sees (engine depth
    /// range [0, 1]).
    void frustum(const Mat4& viewProjection, const Vec4& color = Vec4(1.0f));

    /// Line list: two vertices per line.
    [[nodiscard]] std::span<const Vertex> vertices() const { return vertices_; }
    [[nodiscard]] usize lineCount() const { return vertices_.size() / 2; }
    void clear() { vertices_.clear(); }

private:
    std::vector<Vertex> vertices_;
};

}  // namespace bdex::render
