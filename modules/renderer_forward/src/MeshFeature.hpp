#pragma once

#include <bdex/render/Renderer.hpp>

#include <vector>

namespace bdex::forward {

class ForwardRenderer;
struct GpuMesh;

/// Built-in feature drawing every entity with a MeshRenderer and a
/// WorldTransform, lit by the scene's directional light (Blinn-Phong).
///
/// Visible meshes are gathered and culled against the view frustum. Opaque
/// ones are drawn first, sorted by material and mesh so that each is bound
/// once; transparent ones follow, blended, from the farthest to the nearest.
class MeshFeature final : public render::IRenderFeature {
public:
    explicit MeshFeature(const ForwardRenderer& renderer) : renderer_(&renderer) {}

    [[nodiscard]] std::string_view name() const override { return "forward.meshes"; }
    [[nodiscard]] i32 order() const override { return 0; }

    Result<> onAttach(const render::RenderSetupContext& context) override;
    void onDetach(rhi::IDevice& device) override;
    void render(const render::RenderFrameContext& context) override;

private:
    /// Must match ObjectConstants in mesh.vert / mesh.frag.
    struct ObjectConstants {
        Mat4 model;
        Vec4 baseColor;
        Vec4 params;  // x: specular strength, y: shininess
    };
    struct DrawItem {
        rhi::BindGroupHandle material;
        const GpuMesh* mesh;
        ObjectConstants constants;
        f32 distanceSquared;  ///< From the camera to the bounds' center.
    };

    void drawAll(rhi::ICommandList& commands, const render::RenderFrameContext& context,
                 rhi::PipelineHandle pipeline, const std::vector<DrawItem>& draws);

    const ForwardRenderer* renderer_;
    rhi::PipelineHandle opaquePipeline_;
    rhi::PipelineHandle transparentPipeline_;
    // Reused across frames.
    std::vector<DrawItem> opaque_;
    std::vector<DrawItem> transparent_;
};

}  // namespace bdex::forward
