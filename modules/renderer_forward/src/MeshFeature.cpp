#include "MeshFeature.hpp"

#include "ForwardRenderer.hpp"

#include <bdex/assets/AssetManager.hpp>
#include <bdex/scene/Components.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>

namespace bdex::forward {

Result<> MeshFeature::onAttach(const render::RenderSetupContext& context) {
    auto& assets = context.services.get<assets::AssetManager>();
    const auto vertexBinary = assets.load<assets::ShaderBinary>("engine:/shaders/mesh.vert.spv");
    if (!vertexBinary) {
        return std::unexpected(vertexBinary.error());
    }
    const auto fragmentBinary = assets.load<assets::ShaderBinary>("engine:/shaders/mesh.frag.spv");
    if (!fragmentBinary) {
        return std::unexpected(fragmentBinary.error());
    }

    rhi::IDevice& device = context.device;
    const rhi::ShaderHandle vertexShader =
        device.createShader({rhi::ShaderStage::Vertex, (*vertexBinary)->words, "main", "mesh.vert"});
    const rhi::ShaderHandle fragmentShader = device.createShader(
        {rhi::ShaderStage::Fragment, (*fragmentBinary)->words, "main", "mesh.frag"});

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    desc.vertexLayout.stride = sizeof(assets::Vertex);
    desc.vertexLayout.attributes = {
        {0, rhi::VertexFormat::Float3, offsetof(assets::Vertex, position)},
        {1, rhi::VertexFormat::Float3, offsetof(assets::Vertex, normal)},
        {2, rhi::VertexFormat::Float2, offsetof(assets::Vertex, uv)},
    };
    objectLayout_ = device.createBindGroupLayout({
        {{0, rhi::BindingType::StorageBuffer, rhi::ShaderStage::Vertex}},
        "forward.objects layout",
    });
    objectBuffers_.assign(device.framesInFlight(), ObjectBuffer{});
    desc.bindGroupLayouts = {context.frameLayout, renderer_->materialLayout(), objectLayout_};
    desc.colorFormats = {context.colorFormat};
    desc.depthFormat = context.depthFormat;
    desc.cullMode = rhi::CullMode::Back;
    desc.frontFace = rhi::FrontFace::CounterClockwise;
    desc.debugName = "forward.meshes";
    opaquePipeline_ = device.createGraphicsPipeline(desc);

    // Transparent surfaces blend over what is behind them and must not hide
    // the transparent surfaces drawn after them.
    desc.blend = rhi::BlendMode::AlphaBlend;
    desc.depthWrite = false;
    desc.debugName = "forward.meshes.transparent";
    transparentPipeline_ = device.createGraphicsPipeline(desc);

    // The pipeline keeps what it needs from the shader modules.
    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);
    return {};
}

void MeshFeature::onDetach(rhi::IDevice& device) {
    for (const ObjectBuffer& slot : objectBuffers_) {
        device.destroyBindGroup(slot.bindGroup);
        device.destroyBuffer(slot.buffer);
    }
    objectBuffers_.clear();
    device.destroyBindGroupLayout(objectLayout_);
    device.destroyPipeline(opaquePipeline_);
    device.destroyPipeline(transparentPipeline_);
    objectLayout_ = {};
    opaquePipeline_ = {};
    transparentPipeline_ = {};
}

void MeshFeature::ensureObjectCapacity(rhi::IDevice& device, ObjectBuffer& slot, usize count) {
    if (slot.capacity >= count) {
        return;
    }
    // Released once the frames using them are done (deferred by the device).
    device.destroyBindGroup(slot.bindGroup);
    device.destroyBuffer(slot.buffer);

    slot.capacity = std::max<usize>(std::bit_ceil(count), 256);
    rhi::BufferDesc bufferDesc;
    bufferDesc.size = slot.capacity * sizeof(ObjectData);
    bufferDesc.usage = rhi::BufferUsage::Storage;
    bufferDesc.memory = rhi::MemoryUsage::CpuToGpu;
    bufferDesc.debugName = "forward.objects";
    slot.buffer = device.createBuffer(bufferDesc);

    rhi::BindGroupDesc groupDesc;
    groupDesc.layout = objectLayout_;
    rhi::BindGroupEntry entry;
    entry.binding = 0;
    entry.buffer = slot.buffer;
    groupDesc.entries.push_back(entry);
    groupDesc.debugName = "forward.objects";
    slot.bindGroup = device.createBindGroup(groupDesc);
}

void MeshFeature::render(const render::RenderFrameContext& context) {
    if (context.world == nullptr) {
        return;
    }

    // Gather what is visible...
    opaque_.clear();
    transparent_.clear();
    opaqueOrder_.clear();
    transparentOrder_.clear();
    const render::Frustum frustum(context.camera.viewProjection);
    context.world->view<const render::MeshRenderer, const scene::WorldTransform>().each(
        [&](const render::MeshRenderer& renderable, const scene::WorldTransform& transform) {
            if (!renderable.visible) {
                return;
            }
            const GpuMesh* mesh = renderer_->mesh(renderable.mesh);
            if (mesh == nullptr) {
                return;  // destroyed or never assigned
            }
            const render::BoundingSphere bounds = render::transformSphere(mesh->bounds, transform.matrix);
            if (!frustum.intersects(bounds)) {
                return;  // outside the view
            }
            const GpuMaterial& material = renderer_->materialOrDefault(renderable.material);
            const DrawItem item{
                material.bindGroup,
                mesh,
                {transform.matrix, material.desc.baseColor,
                 Vec4(material.desc.specular, material.desc.shininess, 0.0f, 0.0f)},
            };
            if (material.desc.transparent) {
                // Back to front: larger distances first. A non-negative float
                // orders like its bit pattern, so the complement sorts it
                // descending.
                const Vec3 offset = bounds.center - context.camera.position;
                const f32 distanceSquared = glm::dot(offset, offset);
                transparentOrder_.push_back(
                    {~u64{std::bit_cast<u32>(distanceSquared)}, static_cast<u32>(transparent_.size())});
                transparent_.push_back(item);
            } else {
                // Grouped by material, then mesh.
                opaqueOrder_.push_back({(u64{material.bindGroup.index} << 32) | renderable.mesh.index,
                                        static_cast<u32>(opaque_.size())});
                opaque_.push_back(item);
            }
        });
    if (opaque_.empty() && transparent_.empty()) {
        return;
    }

    // ...order them: opaque surfaces grouped by material and mesh, so each
    // is bound once, transparent ones from back to front so they blend
    // correctly...
    const auto byKey = [](const SortKey& a, const SortKey& b) { return a.key < b.key; };
    std::sort(opaqueOrder_.begin(), opaqueOrder_.end(), byKey);
    std::sort(transparentOrder_.begin(), transparentOrder_.end(), byKey);

    // ...lay their data out in drawing order...
    objects_.clear();
    for (const SortKey& entry : opaqueOrder_) {
        objects_.push_back(opaque_[entry.draw].object);
    }
    for (const SortKey& entry : transparentOrder_) {
        objects_.push_back(transparent_[entry.draw].object);
    }
    ObjectBuffer& slot = objectBuffers_[context.device.frameSlot()];
    ensureObjectCapacity(context.device, slot, objects_.size());
    context.device.writeBuffer(slot.buffer, rhi::asBytes(std::span<const ObjectData>(objects_)));

    // ...and draw them.
    rhi::ICommandList& commands = context.commands;
    commands.beginDebugGroup("forward.meshes");
    drawBatch(commands, context, opaquePipeline_, opaque_, opaqueOrder_, 0, slot.bindGroup);
    drawBatch(commands, context, transparentPipeline_, transparent_, transparentOrder_,
              static_cast<u32>(opaqueOrder_.size()), slot.bindGroup);
    commands.endDebugGroup();
}

void MeshFeature::drawBatch(rhi::ICommandList& commands, const render::RenderFrameContext& context,
                            rhi::PipelineHandle pipeline, const std::vector<DrawItem>& draws,
                            const std::vector<SortKey>& order, u32 firstInstance,
                            rhi::BindGroupHandle objects) {
    if (order.empty()) {
        return;
    }
    commands.bindPipeline(pipeline);
    commands.bindBindGroup(0, context.frameBindGroup);
    commands.bindBindGroup(2, objects);
    rhi::BindGroupHandle boundMaterial;
    const GpuMesh* boundMesh = nullptr;
    for (usize begin = 0; begin < order.size();) {
        // A run of draws sharing the material and the mesh (transparent keys
        // are distances: equal ones are merged only if they share both too).
        const DrawItem& first = draws[order[begin].draw];
        usize end = begin + 1;
        while (end < order.size()) {
            const DrawItem& next = draws[order[end].draw];
            if (next.material != first.material || next.mesh != first.mesh) {
                break;
            }
            ++end;
        }

        if (first.material != boundMaterial) {
            commands.bindBindGroup(1, first.material);
            boundMaterial = first.material;
        }
        if (first.mesh != boundMesh) {
            commands.bindVertexBuffer(first.mesh->vertexBuffer);
            commands.bindIndexBuffer(first.mesh->indexBuffer, rhi::IndexType::UInt32);
            boundMesh = first.mesh;
        }
        commands.drawIndexed(first.mesh->indexCount, static_cast<u32>(end - begin), 0, 0,
                             firstInstance + static_cast<u32>(begin));
        begin = end;
    }
}

}  // namespace bdex::forward
