#include "PostProcess.hpp"

#include <algorithm>
#include <array>

namespace bdex::forward {
namespace {

Result<rhi::ShaderHandle> loadShader(rhi::IDevice& device, assets::AssetManager& assets,
                                     rhi::ShaderStage stage, std::string_view name) {
    const auto binary = assets.load<assets::ShaderBinary>(std::string("engine:/shaders/") + std::string(name) + ".spv");
    if (!binary) {
        return std::unexpected(binary.error());
    }
    return device.createShader({stage, (*binary)->words, "main", std::string(name)});
}

}  // namespace

Result<> PostProcess::create(rhi::IDevice& device, assets::AssetManager& assets,
                             rhi::Format outputFormat, Settings settings) {
    settings_ = settings;
    const auto fullscreen = loadShader(device, assets, rhi::ShaderStage::Vertex, "fullscreen.vert");
    const auto toneMap = loadShader(device, assets, rhi::ShaderStage::Fragment, "tonemap.frag");
    const auto down = loadShader(device, assets, rhi::ShaderStage::Fragment, "bloom_down.frag");
    const auto up = loadShader(device, assets, rhi::ShaderStage::Fragment, "bloom_up.frag");
    for (const auto* shader : {&fullscreen, &toneMap, &down, &up}) {
        if (!*shader) {
            for (const auto* other : {&fullscreen, &toneMap, &down, &up}) {
                if (*other) {
                    device.destroyShader(**other);
                }
            }
            return std::unexpected(shader->error());
        }
    }

    sourceLayout_ = device.createBindGroupLayout({
        {{0, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Fragment}},
        "post source layout",
    });
    toneMapLayout_ = device.createBindGroupLayout({
        {{0, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Fragment},
         {1, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Fragment}},
        "tonemap layout",
    });
    rhi::SamplerDesc samplerDesc;
    samplerDesc.addressMode = rhi::AddressMode::ClampToEdge;
    samplerDesc.debugName = "post sampler";
    sampler_ = device.createSampler(samplerDesc);

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = *fullscreen;
    // No vertex layout: the triangle comes from gl_VertexIndex.
    desc.pushConstantSize = sizeof(Vec4);
    desc.pushConstantStages = rhi::ShaderStage::Fragment;
    desc.cullMode = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;

    desc.fragmentShader = *toneMap;
    desc.bindGroupLayouts = {toneMapLayout_};
    desc.colorFormats = {outputFormat};
    desc.debugName = "tonemap";
    toneMapPipeline_ = device.createGraphicsPipeline(desc);

    desc.bindGroupLayouts = {sourceLayout_};
    desc.colorFormats = {kSceneFormat};
    desc.fragmentShader = *down;
    desc.debugName = "bloom down";
    downPipeline_ = device.createGraphicsPipeline(desc);
    desc.fragmentShader = *up;
    desc.blend = rhi::BlendMode::Additive;  // onto the larger level
    desc.debugName = "bloom up";
    upPipeline_ = device.createGraphicsPipeline(desc);

    for (const rhi::ShaderHandle shader : {*fullscreen, *toneMap, *down, *up}) {
        device.destroyShader(shader);
    }
    return {};
}

void PostProcess::releaseTargets(rhi::IDevice& device) {
    // Releases are deferred by the device until in-flight frames are done.
    device.destroyBindGroup(toneMapInputs_);
    device.destroyBindGroup(sceneSource_);
    device.destroyTexture(scene_);
    for (const BloomLevel& level : bloom_) {
        device.destroyBindGroup(level.source);
        device.destroyTexture(level.texture);
    }
    bloom_.clear();
}

void PostProcess::destroy(rhi::IDevice& device) {
    releaseTargets(device);
    device.destroyPipeline(toneMapPipeline_);
    device.destroyPipeline(downPipeline_);
    device.destroyPipeline(upPipeline_);
    device.destroySampler(sampler_);
    device.destroyBindGroupLayout(sourceLayout_);
    device.destroyBindGroupLayout(toneMapLayout_);
    *this = PostProcess{};
}

rhi::BindGroupHandle PostProcess::sampling(rhi::IDevice& device, rhi::TextureHandle texture) const {
    rhi::BindGroupDesc desc;
    desc.layout = sourceLayout_;
    rhi::BindGroupEntry entry;
    entry.binding = 0;
    entry.texture = texture;
    entry.sampler = sampler_;
    desc.entries.push_back(entry);
    desc.debugName = "post source";
    return device.createBindGroup(desc);
}

rhi::TextureHandle PostProcess::sceneTarget(rhi::IDevice& device, Extent2D extent) {
    if (scene_.valid() && sceneExtent_ == extent) {
        return scene_;
    }
    releaseTargets(device);

    rhi::TextureDesc desc;
    desc.format = kSceneFormat;
    desc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    desc.width = extent.width;
    desc.height = extent.height;
    desc.debugName = "hdr scene";
    scene_ = device.createTexture(desc);
    sceneExtent_ = extent;
    sceneSource_ = sampling(device, scene_);

    // Without bloom there is no chain: the tone map's bloom input is the
    // scene itself, weighted 0 (a never-rendered level is not readable).
    const u32 levels = settings_.bloomIntensity > 0.0f ? kBloomLevels : 0;
    Extent2D size = extent;
    for (u32 level = 0; level < levels; ++level) {
        size = {std::max(size.width / 2, 1u), std::max(size.height / 2, 1u)};
        desc.width = size.width;
        desc.height = size.height;
        desc.debugName = "bloom level";
        const rhi::TextureHandle texture = device.createTexture(desc);
        bloom_.push_back({texture, size, sampling(device, texture)});
    }

    rhi::BindGroupDesc inputs;
    inputs.layout = toneMapLayout_;
    rhi::BindGroupEntry sceneEntry;
    sceneEntry.binding = 0;
    sceneEntry.texture = scene_;
    sceneEntry.sampler = sampler_;
    rhi::BindGroupEntry bloomEntry = sceneEntry;
    bloomEntry.binding = 1;
    bloomEntry.texture = bloom_.empty() ? scene_ : bloom_.front().texture;
    inputs.entries = {sceneEntry, bloomEntry};
    inputs.debugName = "tonemap inputs";
    toneMapInputs_ = device.createBindGroup(inputs);
    return scene_;
}

void PostProcess::filter(rhi::ICommandList& commands, rhi::PipelineHandle pipeline,
                         rhi::BindGroupHandle source, Extent2D sourceExtent, const BloomLevel& target,
                         rhi::LoadOp load) const {
    const std::array<rhi::ColorAttachment, 1> attachments = {{
        {target.texture, load, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 1.0f}},
    }};
    commands.beginRendering({attachments, nullptr});
    commands.setViewport({0.0f, 0.0f, static_cast<f32>(target.extent.width),
                          static_cast<f32>(target.extent.height), 0.0f, 1.0f});
    commands.setScissor({0, 0, target.extent.width, target.extent.height});
    commands.bindPipeline(pipeline);
    commands.bindBindGroup(0, source);
    // Mirrors the Constants block of bloom_down.frag / bloom_up.frag.
    const Vec4 sizes(1.0f / static_cast<f32>(sourceExtent.width), 1.0f / static_cast<f32>(sourceExtent.height),
                     static_cast<f32>(target.extent.width), static_cast<f32>(target.extent.height));
    commands.pushConstants(0, rhi::asBytes(sizes));
    commands.draw(3);
    commands.endRendering();
}

void PostProcess::prepare(rhi::ICommandList& commands) const {
    if (settings_.bloomIntensity <= 0.0f || bloom_.empty()) {
        return;
    }
    commands.beginDebugGroup("bloom");
    // Down the chain, from the scene...
    filter(commands, downPipeline_, sceneSource_, sceneExtent_, bloom_[0], rhi::LoadOp::DontCare);
    for (usize level = 1; level < bloom_.size(); ++level) {
        filter(commands, downPipeline_, bloom_[level - 1].source, bloom_[level - 1].extent, bloom_[level],
               rhi::LoadOp::DontCare);
    }
    // ...then back up, each level blurred onto the next larger one.
    for (usize level = bloom_.size() - 1; level > 0; --level) {
        filter(commands, upPipeline_, bloom_[level].source, bloom_[level].extent, bloom_[level - 1],
               rhi::LoadOp::Load);
    }
    commands.endDebugGroup();
}

void PostProcess::apply(rhi::ICommandList& commands, rhi::TextureHandle output, Extent2D extent) const {
    const std::array<rhi::ColorAttachment, 1> attachments = {{
        {output, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 1.0f}},
    }};
    commands.beginRendering({attachments, nullptr});
    commands.beginDebugGroup("tonemap");
    commands.setViewport({0.0f, 0.0f, static_cast<f32>(extent.width),
                          static_cast<f32>(extent.height), 0.0f, 1.0f});
    commands.setScissor({0, 0, extent.width, extent.height});
    commands.bindPipeline(toneMapPipeline_);
    commands.bindBindGroup(0, toneMapInputs_);
    // Mirrors TonemapConstants in tonemap.frag.
    const Vec4 constants(settings_.exposure, settings_.aces ? 1.0f : 0.0f,
                         bloom_.empty() ? 0.0f : std::max(settings_.bloomIntensity, 0.0f), 0.0f);
    commands.pushConstants(0, rhi::asBytes(constants));
    commands.draw(3);
    commands.endDebugGroup();
    commands.endRendering();
}

}  // namespace bdex::forward
