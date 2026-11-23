#pragma once

#include <phi/core/JobSystem.hpp>
#include <phi/render/Frustum.hpp>
#include <phi/render/Renderer.hpp>

#include <unordered_map>
#include <vector>

namespace phi::forward {

class ForwardRenderer;
struct GpuMesh;
struct GpuMaterial;

/// Built-in feature drawing every entity with a MeshRenderer and a
/// WorldTransform, lit by the scene's directional light (Blinn-Phong).
///
/// Visible meshes are gathered and culled against the view frustum (in parallel
/// on the job system for large scenes). Opaque ones are drawn first, grouped by
/// material and mesh so that each is bound once; transparent ones follow,
/// blended, from the farthest to the nearest. Per-object data goes to a storage
/// buffer, and each run of objects sharing a material and a mesh is a single
/// instanced draw.
class MeshFeature final : public render::IRenderFeature {
public:
    explicit MeshFeature(const ForwardRenderer& renderer) : renderer_(&renderer) {}

    [[nodiscard]] std::string_view name() const override { return "forward.meshes"; }
    [[nodiscard]] i32 order() const override { return 0; }

    Result<> onAttach(const render::RenderSetupContext& context) override;
    void onDetach(rhi::IDevice& device) override;
    void render(const render::RenderFrameContext& context) override;
    /// Opaque meshes cast shadows; transparent ones do not.
    void renderShadows(const render::RenderFrameContext& context) override;

private:
    /// One per instance, in the object storage buffer (set 2). Mirrors
    /// ObjectData in mesh.vert (std430).
    struct ObjectData {
        Mat4 model;
        Vec4 baseColor;
        Vec4 params;    // x: specular strength, y: shininess, z: normal map strength
        Vec4 emissive;  // rgb
    };
    /// Opaque instances sharing a material and a mesh, drawn in one instanced call.
    struct OpaqueBucket {
        u64 key = 0;
        rhi::BindGroupHandle material;
        const GpuMesh* mesh = nullptr;
        Vec4 baseColor{1.0f};
        Vec4 params{0.0f};
        Vec4 emissive{0.0f};
        u32 instanceOffset = 0;
        std::vector<const Mat4*> models;
    };
    struct OpaqueRef {
        u64 key = 0;
        const GpuMesh* mesh = nullptr;
        const GpuMaterial* material = nullptr;
        const Mat4* model = nullptr;
    };
    struct TransparentItem {
        u64 key = 0;
        rhi::BindGroupHandle material;
        const GpuMesh* mesh = nullptr;
        const GpuMaterial* gpuMaterial = nullptr;
        const Mat4* model = nullptr;
    };
    struct CullChunk {
        std::vector<OpaqueRef> opaque;
        std::vector<TransparentItem> transparent;
    };
    /// Shadow-casting instances sharing a mesh, stored SoA so per-cascade
    /// frustum culling scans only 16-byte bounding spheres.
    struct ShadowMeshBucket {
        u32 meshKey = 0;
        const GpuMesh* mesh = nullptr;
        std::vector<render::BoundingSphere> bounds;
        std::vector<const Mat4*> models;
    };
    struct ShadowCandidate {
        u32 meshKey = 0;
        const GpuMesh* mesh = nullptr;
        const Mat4* model = nullptr;
        render::BoundingSphere bounds;
    };
    struct ShadowBatch {
        const GpuMesh* mesh = nullptr;
        u32 firstInstance = 0;
        u32 instanceCount = 0;
    };
    /// Per frame in flight, so the CPU never rewrites what the GPU reads.
    struct ObjectBuffer {
        rhi::BufferHandle buffer;
        rhi::BindGroupHandle bindGroup;
        usize capacity = 0;  ///< In objects.
    };

    void ensureObjectCapacity(rhi::IDevice& device, ObjectBuffer& slot, usize count);

    const ForwardRenderer* renderer_;
    JobSystem* jobs_ = nullptr;
    rhi::PipelineHandle opaquePipeline_;
    rhi::PipelineHandle transparentPipeline_;
    rhi::PipelineHandle shadowPipeline_;
    rhi::BindGroupLayoutHandle objectLayout_;
    std::vector<ObjectBuffer> objectBuffers_;
    std::vector<ObjectBuffer> shadowObjectBuffers_;  ///< The shadow pass's own.

    // Reused across frames to avoid per-frame allocations.
    std::vector<CullChunk> cullChunks_;
    std::vector<OpaqueBucket> opaqueBuckets_;
    std::unordered_map<u64, u32> opaqueBucketLookup_;
    std::vector<u32> opaqueBucketOrder_;
    std::vector<TransparentItem> transparent_;

    std::vector<ShadowCandidate> shadowGather_;
    std::vector<ShadowMeshBucket> shadowBuckets_;
    std::unordered_map<u32, u32> shadowBucketLookup_;
    std::vector<u32> shadowBucketOrder_;
    std::vector<ShadowBatch> shadowBatches_;

    std::vector<ObjectData> objects_;
    usize shadowObjectsUsed_ = 0;  ///< This frame's, across shadow cascades.
};

}  // namespace phi::forward
