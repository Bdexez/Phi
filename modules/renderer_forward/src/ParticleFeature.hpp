#pragma once

#include <bdex/render/Renderer.hpp>

#include <vector>

namespace bdex::forward {

/// Draws the particles of every particles::ParticleEmitter in the scene as
/// soft camera-facing discs, after scene geometry (they pass behind it but
/// never hide each other): blended ones back to front, then additive ones.
class ParticleFeature final : public render::IRenderFeature {
public:
    [[nodiscard]] std::string_view name() const override { return "forward.particles"; }
    /// After meshes, transparent ones included; before debug lines.
    [[nodiscard]] i32 order() const override { return 500; }

    Result<> onAttach(const render::RenderSetupContext& context) override;
    void onDetach(rhi::IDevice& device) override;
    void render(const render::RenderFrameContext& context) override;

    struct Vertex {
        Vec3 position;
        Vec2 uv;
        Vec4 color;
    };

private:
    struct VertexBuffer {
        rhi::BufferHandle buffer;
        usize capacity = 0;  ///< In vertices.
    };

    rhi::PipelineHandle blended_;
    rhi::PipelineHandle additive_;
    std::vector<VertexBuffer> buffers_;  ///< One per frame in flight.
    std::vector<Vertex> vertices_;       ///< Reused across frames.
};

}  // namespace bdex::forward
