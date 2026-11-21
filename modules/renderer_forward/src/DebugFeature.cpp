#include "DebugFeature.hpp"

#include <phi/assets/AssetManager.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>

namespace phi::forward {

Result<> DebugFeature::onAttach(const render::RenderSetupContext& context) {
    auto& assets = context.services.get<assets::AssetManager>();
    const auto vertexBinary = assets.load<assets::ShaderBinary>("engine:/shaders/debug.vert.spv");
    if (!vertexBinary) {
        return std::unexpected(vertexBinary.error());
    }
    const auto fragmentBinary = assets.load<assets::ShaderBinary>("engine:/shaders/debug.frag.spv");
    if (!fragmentBinary) {
        return std::unexpected(fragmentBinary.error());
    }

    rhi::IDevice& device = context.device;
    const rhi::ShaderHandle vertexShader =
        device.createShader({rhi::ShaderStage::Vertex, (*vertexBinary)->words, "main", "debug.vert"});
    const rhi::ShaderHandle fragmentShader = device.createShader(
        {rhi::ShaderStage::Fragment, (*fragmentBinary)->words, "main", "debug.frag"});

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    desc.vertexLayout.stride = sizeof(render::DebugDraw::Vertex);
    desc.vertexLayout.attributes = {
        {0, rhi::VertexFormat::Float3, offsetof(render::DebugDraw::Vertex, position)},
        {1, rhi::VertexFormat::Float4, offsetof(render::DebugDraw::Vertex, color)},
    };
    desc.bindGroupLayouts = {context.frameLayout};
    desc.colorFormats = {context.colorFormat};
    desc.depthFormat = context.depthFormat;
    desc.topology = rhi::PrimitiveTopology::LineList;
    desc.cullMode = rhi::CullMode::None;
    desc.depthWrite = false;  // lines do not hide each other or later overlays
    desc.blend = rhi::BlendMode::AlphaBlend;
    desc.debugName = "forward.debug";
    pipeline_ = device.createGraphicsPipeline(desc);
    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);

    buffers_.assign(device.framesInFlight(), VertexBuffer{});
    return {};
}

void DebugFeature::onDetach(rhi::IDevice& device) {
    for (const VertexBuffer& slot : buffers_) {
        device.destroyBuffer(slot.buffer);
    }
    buffers_.clear();
    device.destroyPipeline(pipeline_);
    pipeline_ = {};
}

void DebugFeature::render(const render::RenderFrameContext& context) {
    const std::span<const render::DebugDraw::Vertex> vertices = lines_->vertices();
    if (vertices.empty()) {
        return;
    }
    VertexBuffer& slot = buffers_[context.device.frameSlot()];
    if (slot.capacity < vertices.size()) {
        context.device.destroyBuffer(slot.buffer);  // deferred by the device
        slot.capacity = std::max<usize>(std::bit_ceil(vertices.size()), 1024);
        rhi::BufferDesc desc;
        desc.size = slot.capacity * sizeof(render::DebugDraw::Vertex);
        desc.usage = rhi::BufferUsage::Vertex;
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.debugName = "forward.debug vertices";
        slot.buffer = context.device.createBuffer(desc);
    }
    context.device.writeBuffer(slot.buffer, rhi::asBytes(vertices));

    rhi::ICommandList& commands = context.commands;
    commands.beginDebugGroup("forward.debug");
    commands.bindPipeline(pipeline_);
    commands.bindBindGroup(0, context.frameBindGroup);
    commands.bindVertexBuffer(slot.buffer);
    commands.draw(static_cast<u32>(vertices.size()));
    commands.endDebugGroup();
}

}  // namespace phi::forward
