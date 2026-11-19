#pragma once

#include <bdex/render/Renderer.hpp>

#include <vector>

namespace bdex::forward {

class ForwardRenderer;
struct GpuMesh;

/// Built-in feature drawing every entity with a MeshRenderer and a
/// WorldTransform, lit by the scene's directional light (Blinn-Phong).
///
/// Visible meshes are gathered, culled against the view frustum, then drawn
/// sorted by material and mesh so that each is bound once per frame.
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
    };

    const ForwardRenderer* renderer_;
    rhi::PipelineHandle pipeline_;
    std::vector<DrawItem> draws_;  ///< Reused across frames.
};

}  // namespace bdex::forward
