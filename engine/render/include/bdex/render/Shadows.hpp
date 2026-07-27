#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/render/Renderer.hpp>

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

}  // namespace bdex::render
