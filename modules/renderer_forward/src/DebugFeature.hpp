#pragma once

#include <bdex/render/DebugDraw.hpp>
#include <bdex/render/Renderer.hpp>

#include <memory>
#include <vector>

namespace bdex::forward {

/// Draws the lines collected in a render::DebugDraw, over the scene but
/// hidden by geometry in front of them.
class DebugFeature final : public render::IRenderFeature {
public:
    explicit DebugFeature(std::shared_ptr<const render::DebugDraw> lines) : lines_(std::move(lines)) {}

    [[nodiscard]] std::string_view name() const override { return "forward.debug"; }
    /// After scene geometry.
    [[nodiscard]] i32 order() const override { return 1000; }

    Result<> onAttach(const render::RenderSetupContext& context) override;
    void onDetach(rhi::IDevice& device) override;
    void render(const render::RenderFrameContext& context) override;

private:
    struct VertexBuffer {
        rhi::BufferHandle buffer;
        usize capacity = 0;  ///< In vertices.
    };

    std::shared_ptr<const render::DebugDraw> lines_;
    rhi::PipelineHandle pipeline_;
    std::vector<VertexBuffer> buffers_;  ///< One per frame in flight.
};

}  // namespace bdex::forward
