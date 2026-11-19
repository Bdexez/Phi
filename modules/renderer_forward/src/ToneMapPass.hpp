#pragma once

#include <bdex/assets/AssetManager.hpp>
#include <bdex/core/Math.hpp>
#include <bdex/core/Result.hpp>
#include <bdex/rhi/Device.hpp>

namespace bdex::forward {

/// Post pass of the forward renderer: holds the HDR target the scene is
/// drawn into, then applies exposure and tone mapping from it into the
/// display (or capture) target with a full-screen triangle.
class ToneMapPass {
public:
    /// Format of the HDR scene target.
    static constexpr rhi::Format kSceneFormat = rhi::Format::RGBA16Float;

    struct Settings {
        f32 exposure = 1.0f;
        bool aces = true;  ///< ACES filmic curve; clamp otherwise.
    };

    Result<> create(rhi::IDevice& device, assets::AssetManager& assets, rhi::Format outputFormat,
                    Settings settings);
    void destroy(rhi::IDevice& device);

    /// The HDR target to draw the scene into, (re)created at `extent`.
    [[nodiscard]] rhi::TextureHandle sceneTarget(rhi::IDevice& device, Extent2D extent);
    /// Tone maps the scene target into `output`, which has the same extent.
    void apply(rhi::ICommandList& commands, rhi::TextureHandle output, Extent2D extent) const;

private:
    Settings settings_;
    rhi::TextureHandle scene_;
    Extent2D sceneExtent_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::BindGroupHandle bindGroup_;  ///< The scene target; follows its size.
    rhi::SamplerHandle sampler_;
    rhi::PipelineHandle pipeline_;
};

}  // namespace bdex::forward
