#pragma once

#include <bdex/assets/AssetManager.hpp>
#include <bdex/render/DebugDraw.hpp>
#include <bdex/rhi/Device.hpp>

#include <span>
#include <vector>

namespace bdex::forward {

/// Draws render::DebugDraw's overlay lines on the final image, after
/// post-processing: crisp, never tone mapped, blurred or hidden.
class Overlay {
public:
    Result<> create(rhi::IDevice& device, assets::AssetManager& assets, rhi::Format format);
    void destroy(rhi::IDevice& device);

    /// Uploads this frame's lines; call once before draw().
    void upload(rhi::IDevice& device, std::span<const render::DebugDraw::Vertex> vertices);
    /// Draws the uploaded lines over `target`, keeping what it holds.
    void draw(rhi::ICommandList& commands, rhi::TextureHandle target, Extent2D extent) const;

private:
    struct VertexBuffer {
        rhi::BufferHandle buffer;
        usize capacity = 0;
    };
    rhi::PipelineHandle pipeline_;
    std::vector<VertexBuffer> buffers_;
    rhi::BufferHandle current_;
    u32 count_ = 0;
};

}  // namespace bdex::forward
