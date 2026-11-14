#include "Overlay.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>

namespace phi::forward {

Result<> Overlay::create(rhi::IDevice& device, assets::AssetManager& assets, rhi::Format format) {
    const auto vertexBinary = assets.load<assets::ShaderBinary>("engine:/shaders/overlay.vert.spv");
    if (!vertexBinary) {
        return std::unexpected(vertexBinary.error());
    }
    const auto fragmentBinary = assets.load<assets::ShaderBinary>("engine:/shaders/debug.frag.spv");
    if (!fragmentBinary) {
        return std::unexpected(fragmentBinary.error());
    }
    const rhi::ShaderHandle vertexShader =
        device.createShader({rhi::ShaderStage::Vertex, (*vertexBinary)->words, "main", "overlay.vert"});
    const rhi::ShaderHandle fragmentShader =
        device.createShader({rhi::ShaderStage::Fragment, (*fragmentBinary)->words, "main", "debug.frag"});

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    desc.vertexLayout.stride = sizeof(render::DebugDraw::Vertex);
    desc.vertexLayout.attributes = {
        {0, rhi::VertexFormat::Float3, offsetof(render::DebugDraw::Vertex, position)},
        {1, rhi::VertexFormat::Float4, offsetof(render::DebugDraw::Vertex, color)},
    };
    desc.colorFormats = {format};
    desc.topology = rhi::PrimitiveTopology::LineList;
    desc.cullMode = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.blend = rhi::BlendMode::AlphaBlend;
    desc.pushConstantSize = sizeof(Vec2);
    desc.pushConstantStages = rhi::ShaderStage::Vertex;
    desc.debugName = "forward.overlay lines";
    lines_ = device.createGraphicsPipeline(desc);
    desc.topology = rhi::PrimitiveTopology::TriangleList;
    desc.debugName = "forward.overlay triangles";
    triangles_ = device.createGraphicsPipeline(desc);
    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);
    buffers_.assign(device.framesInFlight(), VertexBuffer{});
    return {};
}

void Overlay::destroy(rhi::IDevice& device) {
    for (const VertexBuffer& slot : buffers_) {
        device.destroyBuffer(slot.buffer);
    }
    buffers_.clear();
    device.destroyPipeline(lines_);
    device.destroyPipeline(triangles_);
    lines_ = {};
    triangles_ = {};
    current_ = {};
    triangleVertices_ = 0;
    lineVertices_ = 0;
}

void Overlay::upload(rhi::IDevice& device, std::span<const render::DebugDraw::Vertex> triangles,
                     std::span<const render::DebugDraw::Vertex> lines) {
    triangleVertices_ = 0;
    lineVertices_ = 0;
    if ((triangles.empty() && lines.empty()) || buffers_.empty()) {
        return;
    }
    staging_.assign(triangles.begin(), triangles.end());
    staging_.insert(staging_.end(), lines.begin(), lines.end());
    const std::span<const render::DebugDraw::Vertex> vertices(staging_);
    VertexBuffer& slot = buffers_[device.frameSlot()];
    if (slot.capacity < vertices.size()) {
        device.destroyBuffer(slot.buffer);  // deferred by the device
        slot.capacity = std::max<usize>(std::bit_ceil(vertices.size()), 1024);
        rhi::BufferDesc desc;
        desc.size = slot.capacity * sizeof(render::DebugDraw::Vertex);
        desc.usage = rhi::BufferUsage::Vertex;
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.debugName = "forward.overlay vertices";
        slot.buffer = device.createBuffer(desc);
    }
    device.writeBuffer(slot.buffer, rhi::asBytes(vertices));
    current_ = slot.buffer;
    triangleVertices_ = static_cast<u32>(triangles.size());
    lineVertices_ = static_cast<u32>(lines.size());
}

void Overlay::draw(rhi::ICommandList& commands, rhi::TextureHandle target, Extent2D extent) const {
    if (triangleVertices_ + lineVertices_ == 0) {
        return;
    }
    const std::array<rhi::ColorAttachment, 1> attachments = {{
        {target, rhi::LoadOp::Load, rhi::StoreOp::Store, {0.0f, 0.0f, 0.0f, 1.0f}},
    }};
    commands.beginDebugGroup("overlay");
    commands.beginRendering({attachments, nullptr});
    commands.setViewport({0.0f, 0.0f, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0.0f, 1.0f});
    commands.setScissor({0, 0, extent.width, extent.height});
    const Vec2 size(static_cast<f32>(extent.width), static_cast<f32>(extent.height));
    commands.bindVertexBuffer(current_);
    // Filled shapes first, so lines and text land on top of them.
    if (triangleVertices_ > 0) {
        commands.bindPipeline(triangles_);
        commands.pushConstants(0, rhi::asBytes(std::span(&size, 1)));
        commands.draw(triangleVertices_);
    }
    if (lineVertices_ > 0) {
        commands.bindPipeline(lines_);
        commands.pushConstants(0, rhi::asBytes(std::span(&size, 1)));
        commands.draw(lineVertices_, 1, triangleVertices_, 0);
    }
    commands.endRendering();
    commands.endDebugGroup();
}

}  // namespace phi::forward
