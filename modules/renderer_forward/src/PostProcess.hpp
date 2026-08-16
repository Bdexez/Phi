#pragma once

#include <bdex/assets/AssetManager.hpp>
#include <bdex/core/Math.hpp>
#include <bdex/core/Result.hpp>
#include <bdex/rhi/Device.hpp>

#include <vector>

namespace bdex::forward {

/// Post-processing of the forward renderer: holds the HDR target the scene
/// is drawn into, builds bloom from it, applies exposure and tone mapping,
/// and smooths edges with FXAA, into the display (or capture) target, with
/// full-screen triangles.
///
/// Bloom (Jimenez, Call of Duty: Advanced Warfare): the scene is filtered
/// down a chain of half-resolution levels, then back up, each level added
/// onto the next larger one; the result is mixed into the scene before
/// tone mapping, so bright light spreads into a soft halo.
class PostProcess {
public:
    /// Format of the HDR scene target.
    static constexpr rhi::Format kSceneFormat = rhi::Format::RGBA16Float;

    struct Settings {
        f32 exposure = 1.0f;
        bool aces = true;  ///< ACES filmic curve; clamp otherwise.
        /// Share of the blurred image in the final one; 0 turns bloom off.
        f32 bloomIntensity = 0.04f;
        /// FXAA after tone mapping.
        bool antialiasing = true;
    };

    /// `outputFormat`: that of every target apply() writes to.
    Result<> create(rhi::IDevice& device, assets::AssetManager& assets, rhi::Format outputFormat,
                    Settings settings);
    void destroy(rhi::IDevice& device);

    /// The HDR target to draw the scene into, (re)created at `extent`.
    [[nodiscard]] rhi::TextureHandle sceneTarget(rhi::IDevice& device, Extent2D extent);
    /// Builds the bloom from the scene target; once per frame, after the
    /// scene is drawn.
    void prepare(rhi::ICommandList& commands) const;
    /// Tone maps the scene target into `output`, which has the same extent.
    void apply(rhi::ICommandList& commands, rhi::TextureHandle output, Extent2D extent) const;

private:
    static constexpr u32 kBloomLevels = 6;

    struct BloomLevel {
        rhi::TextureHandle texture;
        Extent2D extent;
        rhi::BindGroupHandle source;  ///< Samples this level.
    };

    [[nodiscard]] rhi::BindGroupHandle sampling(rhi::IDevice& device, rhi::TextureHandle texture) const;
    void filter(rhi::ICommandList& commands, rhi::PipelineHandle pipeline, rhi::BindGroupHandle source,
                Extent2D sourceExtent, const BloomLevel& target, rhi::LoadOp load) const;
    void toneMap(rhi::ICommandList& commands, rhi::TextureHandle output, Extent2D extent) const;
    void releaseTargets(rhi::IDevice& device);

    Settings settings_;
    rhi::TextureHandle scene_;
    Extent2D sceneExtent_;
    rhi::BindGroupHandle sceneSource_;  ///< Samples the scene (bloom input).
    std::vector<BloomLevel> bloom_;
    rhi::BindGroupLayoutHandle sourceLayout_;  ///< One sampled texture.
    rhi::BindGroupLayoutHandle toneMapLayout_;  ///< Scene and bloom.
    rhi::BindGroupHandle toneMapInputs_;
    rhi::Format outputFormat_ = rhi::Format::Undefined;
    rhi::TextureHandle toneMapped_;           ///< FXAA input, in the output format.
    rhi::BindGroupHandle toneMappedSource_;
    rhi::PipelineHandle fxaaPipeline_;
    rhi::SamplerHandle sampler_;  ///< Bilinear, clamped.
    rhi::PipelineHandle downPipeline_;
    rhi::PipelineHandle upPipeline_;
    rhi::PipelineHandle toneMapPipeline_;
};

}  // namespace bdex::forward
