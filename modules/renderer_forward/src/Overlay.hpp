#pragma once

#include <phi/assets/AssetManager.hpp>
#include <phi/render/DebugDraw.hpp>
#include <phi/rhi/Device.hpp>

#include <span>
#include <vector>

namespace phi::forward {

/// Draws render::DebugDraw's overlay (filled rectangles, then lines and
/// text over them) on the final image, after
/// post-processing: crisp, never tone mapped, blurred or hidden.
class Overlay {
public:
    Result<> create(rhi::IDevice& device, assets::AssetManager& assets, rhi::Format format);
    void destroy(rhi::IDevice& device);

    /// Uploads this frame's triangles and lines; call once before draw().
    void upload(rhi::IDevice& device, std::span<const render::DebugDraw::Vertex> triangles,
                std::span<const render::DebugDraw::Vertex> lines);
    /// Draws the uploaded lines over `target`, keeping what it holds.
    void draw(rhi::ICommandList& commands, rhi::TextureHandle target, Extent2D extent) const;

private:
    struct VertexBuffer {
        rhi::BufferHandle buffer;
        usize capacity = 0;
    };
    rhi::PipelineHandle lines_;
    rhi::PipelineHandle triangles_;
    std::vector<VertexBuffer> buffers_;
    rhi::BufferHandle current_;
    u32 triangleVertices_ = 0;  ///< First in the buffer...
    u32 lineVertices_ = 0;      ///< ...then these.
    std::vector<render::DebugDraw::Vertex> staging_;
};

}  // namespace phi::forward
