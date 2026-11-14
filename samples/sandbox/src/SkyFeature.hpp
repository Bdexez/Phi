#pragma once

#include <phi/render/Renderer.hpp>

namespace sandbox {

/// A procedural sky drawn behind the scene.
///
/// This is application code extending the engine's renderer: it uses nothing
/// but the public RHI and render interfaces, has its own shaders and pipeline,
/// and is attached with IRenderer::addFeature(). The renderer module is not
/// modified - and does not even know this feature exists.
class SkyFeature final : public phi::render::IRenderFeature {
public:
    [[nodiscard]] std::string_view name() const override { return "sandbox.sky"; }
    /// Before scene geometry (order 0).
    [[nodiscard]] phi::i32 order() const override { return -100; }

    phi::Result<> onAttach(const phi::render::RenderSetupContext& context) override;
    void onDetach(phi::rhi::IDevice& device) override;
    void render(const phi::render::RenderFrameContext& context) override;

private:
    phi::rhi::PipelineHandle pipeline_;
};

}  // namespace sandbox
