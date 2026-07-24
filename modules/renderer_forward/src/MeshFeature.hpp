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
/// Per-object data goes to a storage buffer, and each run of objects sharing
/// a material and a mesh is a single instanced draw.
class MeshFeature final : public render::IRenderFeature {
public:
    explicit MeshFeature(const ForwardRenderer& renderer) : renderer_(&renderer) {}

    [[nodiscard]] std::string_view name() const override { return "forward.meshes"; }
    [[nodiscard]] i32 order() const override { return 0; }

    Result<> onAttach(const render::RenderSetupContext& context) override;
    void onDetach(rhi::IDevice& device) override;
    void render(const render::RenderFrameContext& context) override;

private:
    /// One per instance, in the object storage buffer (set 2). Mirrors
    /// ObjectData in mesh.vert (std430).
    struct ObjectData {
        Mat4 model;
        Vec4 baseColor;
        Vec4 params;  // x: specular strength, y: shininess
    };
    struct DrawItem {
        rhi::BindGroupHandle material;
        const GpuMesh* mesh;
        ObjectData object;
    };
    /// Draws are ordered through these small keys rather than by moving the
    /// draw items themselves. Equal keys share a material and a mesh.
    struct SortKey {
        u64 key;
        u32 draw;  ///< Index into the draw items.
    };
    /// Per frame in flight, so the CPU never rewrites what the GPU reads.
    struct ObjectBuffer {
        rhi::BufferHandle buffer;
        rhi::BindGroupHandle bindGroup;
        usize capacity = 0;  ///< In objects.
    };

    void ensureObjectCapacity(rhi::IDevice& device, ObjectBuffer& slot, usize count);
    /// Draws one batch: consecutive items with the same key become a single
    /// instanced draw. `firstInstance` is the batch's offset in the object buffer.
    void drawBatch(rhi::ICommandList& commands, const render::RenderFrameContext& context,
                   rhi::PipelineHandle pipeline, const std::vector<DrawItem>& draws,
                   const std::vector<SortKey>& order, u32 firstInstance,
                   rhi::BindGroupHandle objects);

    const ForwardRenderer* renderer_;
    rhi::PipelineHandle opaquePipeline_;
    rhi::PipelineHandle transparentPipeline_;
    rhi::BindGroupLayoutHandle objectLayout_;
    std::vector<ObjectBuffer> objectBuffers_;
    // Reused across frames.
    std::vector<DrawItem> opaque_;
    std::vector<DrawItem> transparent_;
    std::vector<SortKey> opaqueOrder_;
    std::vector<SortKey> transparentOrder_;
    std::vector<ObjectData> objects_;
};

}  // namespace bdex::forward
