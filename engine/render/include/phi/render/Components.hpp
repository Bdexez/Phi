#pragma once

#include <phi/core/Handle.hpp>
#include <phi/core/Math.hpp>

namespace phi::render {

// Handles to resources owned by the renderer service.
using MeshHandle = Handle<struct MeshTag>;
using TextureHandle = Handle<struct TextureTag>;
using MaterialHandle = Handle<struct MaterialTag>;

// --- ECS components understood by renderers -----------------------------------
// They are plain data: any renderer module can interpret them, and gameplay
// code never depends on a particular renderer.

/// Perspective camera. The view is taken from the entity's WorldTransform
/// (looking down -Z). Among cameras flagged `primary`, the first one created
/// is used (strictly: the one in the lowest entity slot).
struct Camera {
    f32 verticalFov = glm::radians(60.0f);
    f32 nearPlane = 0.1f;
    f32 farPlane = 500.0f;
    bool primary = true;
};

/// Draws a mesh with a material at the entity's WorldTransform.
struct MeshRenderer {
    MeshHandle mesh;
    MaterialHandle material;
    bool visible = true;
};

/// Omnidirectional light at the entity's WorldTransform position. It fades
/// with the inverse square of the distance and reaches zero at `range`.
struct PointLight {
    Vec3 color{1.0f};
    f32 intensity = 1.0f;
    f32 range = 10.0f;
};

/// Cone of light from the entity's WorldTransform position, shining down
/// its local -Z axis (like a camera). Full strength inside `innerAngle` of
/// the axis, fading to nothing at `outerAngle` (half-angles, radians); it
/// fades with distance like a PointLight.
struct SpotLight {
    Vec3 color{1.0f};
    f32 intensity = 1.0f;
    f32 range = 15.0f;
    f32 innerAngle = glm::radians(20.0f);
    f32 outerAngle = glm::radians(30.0f);
    /// Opaque meshes block it, from up to FrameUniforms::kShadowedSpots
    /// spot lights (the closest): each costs one more pass over the casters.
    bool castShadows = false;
};

/// Sun-like light. `direction` is the direction the light travels in.
struct DirectionalLight {
    Vec3 direction{-0.4f, -1.0f, -0.3f};
    Vec3 color{1.0f};
    f32 intensity = 1.0f;
    Vec3 ambient{0.08f};
    /// Opaque meshes near the camera block this light (when the renderer
    /// has shadows enabled).
    bool castShadows = true;
};

}  // namespace phi::render
