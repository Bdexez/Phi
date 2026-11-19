#include "ToneMapPass.hpp"

#include <array>

namespace bdex::forward {

Result<> ToneMapPass::create(rhi::IDevice& device, assets::AssetManager& assets,
                             rhi::Format outputFormat, Settings settings) {
    settings_ = settings;
    const auto vertexBinary = assets.load<assets::ShaderBinary>("engine:/shaders/fullscreen.vert.spv");
    if (!vertexBinary) {
        return std::unexpected(vertexBinary.error());
    }
    const auto fragmentBinary = assets.load<assets::ShaderBinary>("engine:/shaders/tonemap.frag.spv");
    if (!fragmentBinary) {
        return std::unexpected(fragmentBinary.error());
    }

    layout_ = device.createBindGroupLayout({
        {{0, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Fragment}},
        "tonemap layout",
    });
    rhi::SamplerDesc samplerDesc;
    samplerDesc.minFilter = rhi::Filter::Nearest;
    samplerDesc.magFilter = rhi::Filter::Nearest;
    samplerDesc.addressMode = rhi::AddressMode::ClampToEdge;
    samplerDesc.debugName = "tonemap sampler";
    sampler_ = device.createSampler(samplerDesc);

    const rhi::ShaderHandle vertexShader = device.createShader(
        {rhi::ShaderStage::Vertex, (*vertexBinary)->words, "main", "fullscreen.vert"});
    const rhi::ShaderHandle fragmentShader = device.createShader(
        {rhi::ShaderStage::Fragment, (*fragmentBinary)->words, "main", "tonemap.frag"});
    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    // No vertex layout: the triangle comes from gl_VertexIndex.
    desc.bindGroupLayouts = {layout_};
    desc.pushConstantSize = sizeof(Vec4);
    desc.pushConstantStages = rhi::ShaderStage::Fragment;
    desc.colorFormats = {outputFormat};
    desc.cullMode = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.debugName = "tonemap";
    pipeline_ = device.createGraphicsPipeline(desc);
    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);
    return {};
}

void ToneMapPass::destroy(rhi::IDevice& device) {
    device.destroyPipeline(pipeline_);
    device.destroyBindGroup(bindGroup_);
    device.destroySampler(sampler_);
    device.destroyBindGroupLayout(layout_);
    device.destroyTexture(scene_);
    *this = ToneMapPass{};
}

rhi::TextureHandle ToneMapPass::sceneTarget(rhi::IDevice& device, Extent2D extent) {
    if (scene_.valid() && sceneExtent_ == extent) {
        return scene_;
    }
    device.destroyBindGroup(bindGroup_);  // releases are deferred by the device
    device.destroyTexture(scene_);
    rhi::TextureDesc desc;
    desc.width = extent.width;
    desc.height = extent.height;
    desc.format = kSceneFormat;
    desc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    desc.debugName = "hdr scene";
    scene_ = device.createTexture(desc);
    sceneExtent_ = extent;

    rhi::BindGroupDesc groupDesc;
    groupDesc.layout = layout_;
    rhi::BindGroupEntry entry;
    entry.binding = 0;
    entry.texture = scene_;
    entry.sampler = sampler_;
    groupDesc.entries.push_back(entry);
    groupDesc.debugName = "tonemap";
    bindGroup_ = device.createBindGroup(groupDesc);
    return scene_;
}

void ToneMapPass::apply(rhi::ICommandList& commands, rhi::TextureHandle output, Extent2D extent) const {
    const std::array<rhi::ColorAttachment, 1> attachments = {{
        {output, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 1.0f}},
    }};
    commands.beginRendering({attachments, nullptr});
    commands.beginDebugGroup("tonemap");
    commands.setViewport({0.0f, 0.0f, static_cast<f32>(extent.width),
                          static_cast<f32>(extent.height), 0.0f, 1.0f});
    commands.setScissor({0, 0, extent.width, extent.height});
    commands.bindPipeline(pipeline_);
    commands.bindBindGroup(0, bindGroup_);
    // Mirrors TonemapConstants in tonemap.frag.
    const Vec4 constants(settings_.exposure, settings_.aces ? 1.0f : 0.0f, 0.0f, 0.0f);
    commands.pushConstants(0, rhi::asBytes(constants));
    commands.draw(3);
    commands.endDebugGroup();
    commands.endRendering();
}

}  // namespace bdex::forward
