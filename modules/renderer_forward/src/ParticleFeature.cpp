#include "ParticleFeature.hpp"

#include <bdex/assets/AssetManager.hpp>
#include <bdex/particles/Particles.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>

namespace bdex::forward {

Result<> ParticleFeature::onAttach(const render::RenderSetupContext& context) {
    auto& assets = context.services.get<assets::AssetManager>();
    const auto vertexBinary = assets.load<assets::ShaderBinary>("engine:/shaders/particle.vert.spv");
    if (!vertexBinary) {
        return std::unexpected(vertexBinary.error());
    }
    const auto fragmentBinary = assets.load<assets::ShaderBinary>("engine:/shaders/particle.frag.spv");
    if (!fragmentBinary) {
        return std::unexpected(fragmentBinary.error());
    }
    rhi::IDevice& device = context.device;
    const rhi::ShaderHandle vertexShader =
        device.createShader({rhi::ShaderStage::Vertex, (*vertexBinary)->words, "main", "particle.vert"});
    const rhi::ShaderHandle fragmentShader =
        device.createShader({rhi::ShaderStage::Fragment, (*fragmentBinary)->words, "main", "particle.frag"});

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    desc.vertexLayout.stride = sizeof(Vertex);
    desc.vertexLayout.attributes = {
        {0, rhi::VertexFormat::Float3, offsetof(Vertex, position)},
        {1, rhi::VertexFormat::Float2, offsetof(Vertex, uv)},
        {2, rhi::VertexFormat::Float4, offsetof(Vertex, color)},
    };
    desc.bindGroupLayouts = {context.frameLayout};
    desc.colorFormats = {context.colorFormat};
    desc.depthFormat = context.depthFormat;
    desc.cullMode = rhi::CullMode::None;
    desc.depthWrite = false;  // behind geometry, but never hiding each other
    desc.pushConstantSize = sizeof(Vec4);
    desc.pushConstantStages = rhi::ShaderStage::Fragment;
    desc.blend = rhi::BlendMode::AlphaBlend;
    desc.debugName = "forward.particles (blended)";
    blended_ = device.createGraphicsPipeline(desc);
    desc.blend = rhi::BlendMode::Additive;
    desc.debugName = "forward.particles (additive)";
    additive_ = device.createGraphicsPipeline(desc);
    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);
    buffers_.assign(device.framesInFlight(), VertexBuffer{});
    return {};
}

void ParticleFeature::onDetach(rhi::IDevice& device) {
    for (const VertexBuffer& slot : buffers_) {
        device.destroyBuffer(slot.buffer);
    }
    buffers_.clear();
    device.destroyPipeline(blended_);
    device.destroyPipeline(additive_);
    blended_ = {};
    additive_ = {};
}

void ParticleFeature::render(const render::RenderFrameContext& context) {
    if (context.world == nullptr) {
        return;
    }
    // Camera axes, from the view matrix's rows: quads face the camera.
    const Mat4& view = context.camera.view;
    const Vec3 right(view[0][0], view[1][0], view[2][0]);
    const Vec3 up(view[0][1], view[1][1], view[2][1]);

    struct Sprite {
        Vec3 position;
        f32 size;
        Vec4 color;
        f32 distance;
    };
    std::vector<Sprite> blended;
    std::vector<Sprite> additive;
    context.world->view<const particles::ParticleEmitter>().each([&](const particles::ParticleEmitter& emitter) {
        for (const particles::Particle& particle : emitter.particles) {
            const Sprite sprite{particle.position, particles::sizeAt(emitter, particle), particles::colorAt(emitter, particle),
                                glm::dot(particle.position - context.camera.position, particle.position - context.camera.position)};
            (emitter.additive ? additive : blended).push_back(sprite);
        }
    });
    if (blended.empty() && additive.empty()) {
        return;
    }
    // Blended ones back to front, so nearer ones cover farther ones.
    std::sort(blended.begin(), blended.end(), [](const Sprite& a, const Sprite& b) { return a.distance > b.distance; });

    vertices_.clear();
    const auto quad = [&](const Sprite& sprite) {
        const Vec3 x = right * (sprite.size * 0.5f);
        const Vec3 y = up * (sprite.size * 0.5f);
        const Vertex corners[4] = {{sprite.position - x - y, Vec2(0.0f, 1.0f), sprite.color},
                                   {sprite.position + x - y, Vec2(1.0f, 1.0f), sprite.color},
                                   {sprite.position + x + y, Vec2(1.0f, 0.0f), sprite.color},
                                   {sprite.position - x + y, Vec2(0.0f, 0.0f), sprite.color}};
        for (const int i : {0, 1, 2, 0, 2, 3}) {
            vertices_.push_back(corners[i]);
        }
    };
    for (const Sprite& sprite : blended) {
        quad(sprite);
    }
    const auto blendedCount = static_cast<u32>(vertices_.size());
    for (const Sprite& sprite : additive) {
        quad(sprite);
    }

    VertexBuffer& slot = buffers_[context.device.frameSlot()];
    if (slot.capacity < vertices_.size()) {
        context.device.destroyBuffer(slot.buffer);  // deferred by the device
        slot.capacity = std::max<usize>(std::bit_ceil(vertices_.size()), 6 * 256);
        rhi::BufferDesc desc;
        desc.size = slot.capacity * sizeof(Vertex);
        desc.usage = rhi::BufferUsage::Vertex;
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.debugName = "forward.particles vertices";
        slot.buffer = context.device.createBuffer(desc);
    }
    context.device.writeBuffer(slot.buffer, rhi::asBytes(std::span<const Vertex>(vertices_)));

    rhi::ICommandList& commands = context.commands;
    commands.beginDebugGroup("forward.particles");
    commands.bindVertexBuffer(slot.buffer);
    const auto draw = [&](rhi::PipelineHandle pipeline, f32 mode, u32 first, u32 count) {
        if (count == 0) {
            return;
        }
        commands.bindPipeline(pipeline);
        commands.bindBindGroup(0, context.frameBindGroup);
        const Vec4 constants(mode, 0.0f, 0.0f, 0.0f);
        commands.pushConstants(0, rhi::asBytes(std::span(&constants, 1)));
        commands.draw(count, 1, first, 0);
    };
    draw(blended_, 0.0f, 0, blendedCount);
    draw(additive_, 1.0f, blendedCount, static_cast<u32>(vertices_.size()) - blendedCount);
    commands.endDebugGroup();
}

}  // namespace bdex::forward
