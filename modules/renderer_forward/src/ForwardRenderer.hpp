#pragma once

#include "ToneMapPass.hpp"

#include <bdex/core/Engine.hpp>
#include <bdex/core/Handle.hpp>
#include <bdex/platform/Window.hpp>
#include <bdex/render/Frustum.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/rhi/Device.hpp>

#include <filesystem>
#include <memory>
#include <vector>

namespace bdex::forward {

struct GpuMesh {
    rhi::BufferHandle vertexBuffer;
    rhi::BufferHandle indexBuffer;
    u32 indexCount = 0;
    render::BoundingSphere bounds;  ///< In mesh space, for culling.
};

struct GpuMaterial {
    render::MaterialDesc desc;
    rhi::BindGroupHandle bindGroup;  ///< Set 1: albedo (0) and normal map (1).
};

/// Single-pass forward renderer written purely against the RHI: it runs
/// unchanged on any backend module, with or without a window.
///
/// Frame: a depth-only shadow pass for the sun, then one color + depth pass
/// in which every render feature draws, in order, into an HDR (16-bit float)
/// target, and finally a full-screen pass that applies exposure and tone
/// mapping into the output. The renderer owns the frame constants and shadow
/// map (set 0) and the mesh, texture and material resources; what gets
/// drawn is decided by features.
///
/// Configuration:
///   renderer.hdr                HDR target and post pass (default true); when
///                               off, features draw straight into the output
///   renderer.exposure           scales the scene's light (default 1)
///   renderer.tonemap            "aces" (default) or "none" (clamp)
///   renderer.shadows            directional shadows (default true)
///   renderer.shadow_map_size    texels per side (default 2048)
///   renderer.shadow_distance    shadows are drawn up to this distance from
///                               the camera (default 40)
class ForwardRenderer final : public render::IRenderer {
public:
    ForwardRenderer(Engine& engine, rhi::IDevice& device);
    ~ForwardRenderer() override;

    Result<> initialize();
    void shutdown();
    void renderFrame(const FrameTime& time);

    // --- render::IRenderer -----------------------------------------------------
    [[nodiscard]] render::MeshHandle createMesh(const assets::MeshData& mesh) override;
    void destroyMesh(render::MeshHandle mesh) override;
    [[nodiscard]] render::TextureHandle createTexture(
        const assets::ImageData& image,
        render::TextureColorSpace colorSpace = render::TextureColorSpace::Srgb) override;
    void destroyTexture(render::TextureHandle texture) override;
    [[nodiscard]] render::MaterialHandle createMaterial(const render::MaterialDesc& desc) override;
    void destroyMaterial(render::MaterialHandle material) override;
    Result<> addFeature(std::unique_ptr<render::IRenderFeature> feature) override;
    bool removeFeature(std::string_view name) override;
    void setClearColor(const Vec4& color) override { clearColor_ = color; }
    [[nodiscard]] Extent2D renderExtent() const override { return extent_; }
    void requestScreenshot(const std::filesystem::path& path) override { screenshotPath_ = path; }

    // --- Used by the built-in mesh feature ----------------------------------------
    [[nodiscard]] const GpuMesh* mesh(render::MeshHandle handle) const {
        return meshes_.get(handle);
    }
    /// Falls back to the default (white) material for invalid handles.
    [[nodiscard]] const GpuMaterial& materialOrDefault(render::MaterialHandle handle) const;
    [[nodiscard]] rhi::BindGroupLayoutHandle materialLayout() const { return materialLayout_; }

private:
    [[nodiscard]] Extent2D targetExtent() const;
    void ensureDepthTarget(Extent2D extent);
    void ensureCaptureTarget(Extent2D extent);
    void updateFrameUniforms(const render::CameraData& camera, Extent2D extent,
                             const FrameTime& time);
    [[nodiscard]] render::DirectionalLight findSun() const;
    /// Fits the sun's shadow volume around the camera's nearby view.
    void updateShadowView(const render::CameraData& camera);
    void renderShadowPass(rhi::ICommandList& commands, const FrameTime& time);
    /// Features draw the scene into `target` (the HDR target, or the output
    /// without HDR).
    void renderScene(rhi::ICommandList& commands, rhi::TextureHandle target, Extent2D extent,
                     const render::CameraData& camera, const FrameTime& time);
    void saveCapture(const std::filesystem::path& path);
    [[nodiscard]] rhi::BindGroupHandle createMaterialBindGroup(const render::MaterialDesc& desc);
    [[nodiscard]] render::RenderSetupContext setupContext();

    Engine& engine_;
    rhi::IDevice& device_;
    platform::IWindow* window_ = nullptr;  ///< Null when no window system is loaded.

    // Output
    rhi::SwapchainHandle swapchain_;  ///< Invalid when rendering offscreen.
    rhi::TextureHandle depthTarget_;
    rhi::TextureHandle captureTarget_;  ///< Offscreen color target (headless, screenshots).
    Extent2D depthExtent_;
    Extent2D captureExtent_;
    Extent2D extent_;
    Extent2D fallbackExtent_{1280, 720};
    rhi::Format colorFormat_ = rhi::Format::RGBA8Srgb;  ///< Of the output.
    rhi::Format depthFormat_ = rhi::Format::D32Float;
    Vec4 clearColor_{0.02f, 0.02f, 0.03f, 1.0f};
    std::filesystem::path screenshotPath_;

    // Post-processing
    ToneMapPass toneMap_;
    bool hdr_ = true;

    // Shadows. The map always exists (1x1 when shadows are off) because the
    // frame bind groups reference it.
    rhi::TextureHandle shadowMap_;
    rhi::SamplerHandle shadowSampler_;
    rhi::Format shadowFormat_ = rhi::Format::D32Float;
    u32 shadowMapSize_ = 2048;
    f32 shadowDistance_ = 40.0f;
    bool shadowsEnabled_ = true;
    render::DirectionalLight sun_;    ///< This frame's.
    render::CameraData shadowView_;   ///< The sun's view of this frame.
    bool shadowsThisFrame_ = false;

    // Frame constants: one buffer + bind group per frame in flight, so the CPU
    // never rewrites data the GPU is still reading.
    rhi::BindGroupLayoutHandle frameLayout_;
    std::vector<rhi::BufferHandle> frameBuffers_;
    std::vector<rhi::BindGroupHandle> frameBindGroups_;

    // Scene resources
    rhi::BindGroupLayoutHandle materialLayout_;
    rhi::SamplerHandle defaultSampler_;
    render::TextureHandle whiteTexture_;
    render::TextureHandle flatNormalTexture_;  ///< For materials without a normal map.
    render::MaterialHandle defaultMaterial_;
    HandlePool<GpuMesh, render::MeshTag> meshes_;
    HandlePool<rhi::TextureHandle, render::TextureTag> textures_;
    HandlePool<GpuMaterial, render::MaterialTag> materials_;

    std::vector<std::unique_ptr<render::IRenderFeature>> features_;
    bool initialized_ = false;

    struct PointLightCandidate {
        Vec3 position;
        const render::PointLight* light;
        f32 distance;  ///< From the camera, minus the light's range.
    };
    std::vector<PointLightCandidate> pointLights_;  ///< Reused across frames.
};

}  // namespace bdex::forward
