#pragma once

#include <phi/assets/Image.hpp>
#include <phi/assets/Mesh.hpp>
#include <phi/core/Math.hpp>
#include <phi/core/ServiceRegistry.hpp>
#include <phi/core/Time.hpp>
#include <phi/ecs/World.hpp>
#include <phi/render/Components.hpp>
#include <phi/render/Frustum.hpp>
#include <phi/rhi/Device.hpp>

#include <filesystem>
#include <optional>
#include <memory>
#include <string_view>

namespace phi::render {

/// View parameters of the frame being rendered.
struct CameraData {
    Mat4 view{1.0f};
    Mat4 projection{1.0f};
    Mat4 viewProjection{1.0f};
    Vec3 position{0.0f};
};

[[nodiscard]] CameraData makeCameraData(const Mat4& cameraWorld, const Camera& camera,
                                        f32 aspectRatio);

/// Per-frame constants bound as set 0, binding 0 for every render feature
/// (std140 compatible: only vec4 / mat4 members, and arrays of vec4).
/// Mirrored by engine/render/shaders/phi/frame_uniforms.glsl.
struct FrameUniforms {
    /// Point lights beyond this count are dropped, farthest from the camera
    /// first.
    static constexpr u32 kMaxPointLights = 16;
    /// The same for spot lights.
    static constexpr u32 kMaxSpotLights = 8;
    /// Cascades of the sun's shadow map: the first row of a 4 x 2 atlas.
    static constexpr u32 kShadowCascades = 4;
    /// Spot lights casting shadows, the closest first: the atlas's second
    /// row. Shadow views are numbered cascades first, then these.
    static constexpr u32 kShadowedSpots = 4;

    Mat4 view{1.0f};
    Mat4 projection{1.0f};
    Mat4 viewProjection{1.0f};
    Mat4 inverseViewProjection{1.0f};
    Vec4 cameraPosition{0.0f};  ///< xyz
    Vec4 lightDirection{0.0f};  ///< xyz, normalized, direction of travel
    Vec4 lightColor{1.0f};      ///< rgb premultiplied by intensity
    Vec4 ambientColor{0.0f};    ///< rgb
    Vec4 timeAndExtent{0.0f};   ///< elapsed seconds, delta seconds, width, height
    /// World to clip space of each shadow cascade; a shadow pass renders
    /// with one of them, and receivers project into the one covering them.
    Mat4 shadowCascades[kShadowCascades]{};
    /// Far distance from the camera (along its view axis) of each cascade.
    Vec4 cascadeSplits{0.0f};
    /// x: depth bias, y: normal offset in world units, per cascade.
    Vec4 cascadeBiases[kShadowCascades]{};
    /// x: 1 when the shadow map holds the sun's shadows this frame, y and z:
    /// one texel of the atlas in U and V units.
    Vec4 shadowParams{0.0f};
    Vec4 pointLightCount{0.0f};  ///< x: point lights below, y: spot lights
    Vec4 pointLightPositions[kMaxPointLights]{};  ///< xyz: world position, w: range
    Vec4 pointLightColors[kMaxPointLights]{};     ///< rgb, premultiplied by intensity
    Vec4 spotLightPositions[kMaxSpotLights]{};    ///< xyz: world position, w: range
    Vec4 spotLightDirections[kMaxSpotLights]{};   ///< xyz: unit axis, w: cos(outer angle)
    Vec4 spotLightColors[kMaxSpotLights]{};       ///< rgb premultiplied, w: cos(inner angle)
    /// World to clip space of each spot light shadow.
    Mat4 spotShadows[kShadowedSpots]{};
    /// Per spot light: x: its shadow (index into spotShadows) or -1, y: depth
    /// bias, z: normal offset per unit of distance to the light.
    Vec4 spotShadowParams[kMaxSpotLights]{};
};

/// How a texture's values are stored: color images are authored in sRGB and
/// decoded to linear when sampled; data such as normal maps is used as is.
enum class TextureColorSpace : u8 { Srgb, Linear };

struct MaterialDesc {
    Vec4 baseColor{1.0f};
    TextureHandle albedo;  ///< Optional; multiplies baseColor.
    /// Optional tangent-space normal map (Y up, OpenGL convention), created
    /// with TextureColorSpace::Linear.
    TextureHandle normalMap;
    /// Scales the normal map's bumps: 0 flattens them, 1 as authored.
    f32 normalStrength = 1.0f;
    f32 specular = 0.3f;
    f32 shininess = 32.0f;
    /// Light the surface emits by itself, unaffected by lighting and
    /// shadows. With HDR rendering, values above 1 read as glowing.
    Vec3 emissive{0.0f};
    /// Blended over what is behind it by its alpha (baseColor.a times the
    /// texture's): drawn after opaque geometry, back to front, without
    /// writing depth.
    bool transparent = false;
};

/// Everything a render feature needs to create its GPU objects.
struct RenderSetupContext {
    rhi::IDevice& device;
    ServiceRegistry& services;
    /// Formats of the targets features draw into; pipelines must match them.
    rhi::Format colorFormat;
    rhi::Format depthFormat;
    /// Depth format of the shadow map (shadow passes have no color target).
    rhi::Format shadowFormat;
    /// Layout of set 0: FrameUniforms (binding 0) and the shadow map
    /// (binding 1, a comparison sampler).
    rhi::BindGroupLayoutHandle frameLayout;
};

/// Everything a render feature needs to draw. Rendering has already begun:
/// the feature only binds state and issues draws.
struct RenderFrameContext {
    rhi::IDevice& device;
    rhi::ICommandList& commands;
    /// World of the active scene, or nullptr when there is none.
    ecs::World* world;
    CameraData camera;
    Extent2D extent;
    /// Bind group for set 0 (FrameUniforms) of the current frame.
    rhi::BindGroupHandle frameBindGroup;
    FrameTime time;
    /// In renderShadows: the shadow view being drawn, the sun's cascades
    /// first (0 to 3), then spot lights' (4 and up); see FrameUniforms.
    u32 shadowCascade = 0;
    /// In renderShadows: true for the frame's first shadow view, to gather
    /// casters once per frame.
    bool firstShadowView = false;
};

/// A unit of rendering plugged into a renderer: mesh drawing, a sky, debug
/// lines, UI... Features are how applications and modules extend a renderer
/// without modifying it.
class IRenderFeature {
public:
    virtual ~IRenderFeature() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    /// Features draw by ascending order. Built-in scene geometry is at 0;
    /// use negative values for backgrounds, positive for overlays.
    [[nodiscard]] virtual i32 order() const { return 0; }

    /// Create pipelines and resources.
    virtual Result<> onAttach(const RenderSetupContext& context) = 0;
    /// Destroy everything created in onAttach.
    virtual void onDetach(rhi::IDevice& device) = 0;
    virtual void render(const RenderFrameContext& context) = 0;
    /// Called inside the shadow pass, before render(), once per shadow view
    /// (sun cascade or spot light) when the frame has shadows: draw shadow
    /// casters, depth only, transformed by phiShadowView(context.shadowCascade)
    /// in GLSL (the renderer has set the viewport). `context.camera`
    /// describes that view, for culling. The default casts no shadow.
    virtual void renderShadows(const RenderFrameContext& context) { (void)context; }
};

/// Service provided by a renderer module ("renderer" capability).
///
/// A renderer draws the active scene (see scene::SceneManager) every frame
/// during Phase::Render. Scene content refers to renderer resources through
/// the handles returned here.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // --- Resources -----------------------------------------------------------
    [[nodiscard]] virtual MeshHandle createMesh(const assets::MeshData& mesh) = 0;
    virtual void destroyMesh(MeshHandle mesh) = 0;
    /// Bounding sphere of a mesh, in mesh space (for picking or gameplay
    /// queries); nothing for an invalid handle.
    [[nodiscard]] virtual std::optional<BoundingSphere> meshBounds(MeshHandle mesh) const = 0;

    [[nodiscard]] virtual TextureHandle createTexture(
        const assets::ImageData& image, TextureColorSpace colorSpace = TextureColorSpace::Srgb) = 0;
    /// Materials still using the texture fall back to an untextured look.
    virtual void destroyTexture(TextureHandle texture) = 0;

    [[nodiscard]] virtual MaterialHandle createMaterial(const MaterialDesc& desc) = 0;
    virtual void destroyMaterial(MaterialHandle material) = 0;

    // --- Extension -------------------------------------------------------------
    virtual Result<> addFeature(std::unique_ptr<IRenderFeature> feature) = 0;
    /// Rebuilds every pipeline from the shader files on disk (development:
    /// edit a shader, recompile it, reload). Features are detached and
    /// attached again, in order; one that fails to attach is removed and
    /// the first failure is returned.
    virtual Result<> reloadShaders() = 0;
    /// Returns false if no feature has that name.
    virtual bool removeFeature(std::string_view name) = 0;

    // --- Output ----------------------------------------------------------------
    virtual void setClearColor(const Vec4& color) = 0;
    [[nodiscard]] virtual Extent2D renderExtent() const = 0;
    /// Saves the next rendered frame as a PNG file.
    virtual void requestScreenshot(const std::filesystem::path& path) = 0;
};

/// Published once a requested screenshot has been written (or has failed).
struct ScreenshotSaved {
    std::filesystem::path path;
    bool success = false;
};

}  // namespace phi::render
