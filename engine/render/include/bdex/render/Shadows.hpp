#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/render/Renderer.hpp>

#include <vector>

namespace bdex::render {

/// View of a directional light for shadow mapping: an orthographic volume
/// around the part of `camera`'s view frustum within `distance` of it.
///
/// The volume is a sphere's bounding box, so its size does not change as the
/// camera turns (shadows do not swim), and its center is snapped to whole
/// shadow-map texels, so moving the camera does not make shadow edges crawl.
/// It also reaches `distance` further toward the light, for casters standing
/// between the light and the visible area. The result uses the same clip
/// space conventions as makeCameraData (Y down, depth in [0, 1]).
[[nodiscard]] CameraData fitDirectionalShadow(const CameraData& camera, const Vec3& lightDirection,
                                              f32 distance, u32 mapSize);

/// The same, around the slice of the view frustum between `nearDistance`
/// and `farDistance` from the camera (along its view axis): one cascade of
/// a cascaded shadow map.
[[nodiscard]] CameraData fitDirectionalShadow(const CameraData& camera, const Vec3& lightDirection,
                                              f32 nearDistance, f32 farDistance, u32 mapSize);

/// Far distance of each of `count` cascades covering [nearPlane, distance]:
/// a blend (by `lambda`, 0 uniform to 1 logarithmic) of even and
/// geometrically growing slices, so near cascades are small and sharp.
[[nodiscard]] std::vector<f32> cascadeSplits(f32 nearPlane, f32 distance, u32 count, f32 lambda = 0.75f);

}  // namespace bdex::render
