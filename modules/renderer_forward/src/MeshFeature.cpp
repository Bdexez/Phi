#include "MeshFeature.hpp"

#include "ForwardRenderer.hpp"

#include <phi/assets/AssetManager.hpp>
#include <phi/scene/Components.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>

namespace phi::forward {

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
    const auto shadowVertexBinary = assets.load<assets::ShaderBinary>("engine:/shaders/shadow.vert.spv");
    if (!shadowVertexBinary) {
        return std::unexpected(shadowVertexBinary.error());
    }
    const auto shadowFragmentBinary =
        assets.load<assets::ShaderBinary>("engine:/shaders/shadow.frag.spv");
    if (!shadowFragmentBinary) {
        return std::unexpected(shadowFragmentBinary.error());
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
        {3, rhi::VertexFormat::Float4, offsetof(assets::Vertex, tangent)},
    };
    objectLayout_ = device.createBindGroupLayout({
        {{0, rhi::BindingType::StorageBuffer, rhi::ShaderStage::Vertex}},
        "forward.objects layout",
    });
    objectBuffers_.assign(device.framesInFlight(), ObjectBuffer{});
    shadowObjectBuffers_.assign(device.framesInFlight(), ObjectBuffer{});
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

    // Shadow casters: depth only, into the shadow map. Back faces (as seen
    // from the light) are drawn so lit surfaces do not shadow themselves.
    const rhi::ShaderHandle shadowVertexShader = device.createShader(
        {rhi::ShaderStage::Vertex, (*shadowVertexBinary)->words, "main", "shadow.vert"});
    const rhi::ShaderHandle shadowFragmentShader = device.createShader(
        {rhi::ShaderStage::Fragment, (*shadowFragmentBinary)->words, "main", "shadow.frag"});
    rhi::GraphicsPipelineDesc shadowDesc = desc;
    shadowDesc.vertexShader = shadowVertexShader;
    shadowDesc.fragmentShader = shadowFragmentShader;
    shadowDesc.vertexLayout.attributes = {
        {0, rhi::VertexFormat::Float3, offsetof(assets::Vertex, position)},
    };
    shadowDesc.colorFormats = {};
    shadowDesc.depthFormat = context.shadowFormat;
    shadowDesc.blend = rhi::BlendMode::Opaque;
    shadowDesc.depthWrite = true;
    shadowDesc.cullMode = rhi::CullMode::Front;
    shadowDesc.pushConstantSize = sizeof(u32) * 4;  // the cascade index (shadow.vert)
    shadowDesc.pushConstantStages = rhi::ShaderStage::Vertex;
    shadowDesc.debugName = "forward.meshes.shadows";
    shadowPipeline_ = device.createGraphicsPipeline(shadowDesc);

    // The pipelines keep what they need from the shader modules.
    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);
    device.destroyShader(shadowVertexShader);
    device.destroyShader(shadowFragmentShader);
    return {};
}

void MeshFeature::onDetach(rhi::IDevice& device) {
    for (const auto* buffers : {&objectBuffers_, &shadowObjectBuffers_}) {
        for (const ObjectBuffer& slot : *buffers) {
            device.destroyBindGroup(slot.bindGroup);
            device.destroyBuffer(slot.buffer);
        }
    }
    objectBuffers_.clear();
    shadowObjectBuffers_.clear();
    device.destroyBindGroupLayout(objectLayout_);
    device.destroyPipeline(opaquePipeline_);
    device.destroyPipeline(transparentPipeline_);
    device.destroyPipeline(shadowPipeline_);
    shadowPipeline_ = {};
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
    const auto* renderers = context.world->storage<render::MeshRenderer>();
    const auto* transforms = context.world->storage<scene::WorldTransform>();
    if (renderers == nullptr || transforms == nullptr || renderers->size() == 0) {
        return;
    }

    // Gather visible meshes: opaque ones bucketed directly by (material, mesh)
    // in O(N) without sorting every instance; transparent ones kept for back-to-front sort.
    opaqueBucketLookup_.clear();
    opaqueBucketOrder_.clear();
    transparent_.clear();
    u32 activeBuckets = 0;
    usize opaqueCount = 0;

    u64 lastKey = ~u64{0};
    u32 lastBucket = 0;
    const render::Frustum frustum(context.camera.viewProjection);
    const std::span<const u32> entities = renderers->entities();
    const std::span<const render::MeshRenderer> items = renderers->values();

    for (usize i = 0; i < entities.size(); ++i) {
        const render::MeshRenderer& renderable = items[i];
        if (!renderable.visible) {
            continue;
        }
        const scene::WorldTransform* transform = transforms->tryGet(entities[i]);
        if (transform == nullptr) {
            continue;
        }
        const GpuMesh* mesh = renderer_->mesh(renderable.mesh);
        if (mesh == nullptr) {
            continue;  // destroyed or never assigned
        }
        const render::BoundingSphere bounds = render::transformSphere(mesh->bounds, transform->matrix);
        if (!frustum.intersects(bounds)) {
            continue;  // outside the view
        }
        const GpuMaterial& material = renderer_->materialOrDefault(renderable.material);
        if (material.desc.transparent) {
            // Back to front: larger distances first. A non-negative float
            // orders like its bit pattern, so the complement sorts it
            // descending.
            const Vec3 offset = bounds.center - context.camera.position;
            const f32 distanceSquared = glm::dot(offset, offset);
            transparent_.push_back({~u64{std::bit_cast<u32>(distanceSquared)}, material.bindGroup, mesh,
                                    &material, &transform->matrix});
        } else {
            const u64 key = (u64{material.bindGroup.index} << 32) | renderable.mesh.index;
            u32 bucketIndex = lastBucket;
            if (key != lastKey) {
                const auto [it, inserted] = opaqueBucketLookup_.emplace(key, activeBuckets);
                if (inserted) {
                    if (activeBuckets == opaqueBuckets_.size()) {
                        opaqueBuckets_.emplace_back();
                    }
                    OpaqueBucket& bucket = opaqueBuckets_[activeBuckets];
                    bucket.key = key;
                    bucket.material = material.bindGroup;
                    bucket.mesh = mesh;
                    bucket.baseColor = material.desc.baseColor;
                    // Without a normal map the strength is 0, which keeps the
                    // surface normal exact (the flat texel is only ~(0, 0, 1)).
                    bucket.params = Vec4(material.desc.specular, material.desc.shininess,
                                         material.desc.normalMap.valid() ? material.desc.normalStrength : 0.0f, 0.0f);
                    bucket.emissive = Vec4(material.desc.emissive, 0.0f);
                    bucket.models.clear();
                    opaqueBucketOrder_.push_back(activeBuckets);
                    ++activeBuckets;
                }
                bucketIndex = it->second;
                lastKey = key;
                lastBucket = bucketIndex;
            }
            opaqueBuckets_[bucketIndex].models.push_back(&transform->matrix);
            ++opaqueCount;
        }
    }

    if (opaqueCount == 0 && transparent_.empty()) {
        return;
    }

    // Sort only the distinct opaque (material, mesh) buckets and the transparent instances.
    std::sort(opaqueBucketOrder_.begin(), opaqueBucketOrder_.end(),
              [this](u32 a, u32 b) { return opaqueBuckets_[a].key < opaqueBuckets_[b].key; });
    std::sort(transparent_.begin(), transparent_.end(),
              [](const TransparentItem& a, const TransparentItem& b) { return a.key < b.key; });

    // Lay out per-instance data once in final drawing order.
    objects_.clear();
    objects_.reserve(opaqueCount + transparent_.size());
    for (const u32 bucketIndex : opaqueBucketOrder_) {
        const OpaqueBucket& bucket = opaqueBuckets_[bucketIndex];
        for (const Mat4* model : bucket.models) {
            objects_.push_back({*model, bucket.baseColor, bucket.params, bucket.emissive});
        }
    }
    for (const TransparentItem& item : transparent_) {
        const render::MaterialDesc& desc = item.gpuMaterial->desc;
        objects_.push_back({*item.model, desc.baseColor,
                            Vec4(desc.specular, desc.shininess,
                                 desc.normalMap.valid() ? desc.normalStrength : 0.0f, 0.0f),
                            Vec4(desc.emissive, 0.0f)});
    }

    ObjectBuffer& slot = objectBuffers_[context.device.frameSlot()];
    ensureObjectCapacity(context.device, slot, objects_.size());
    context.device.writeBuffer(slot.buffer, rhi::asBytes(std::span<const ObjectData>(objects_)));

    // Draw opaque buckets (each bucket is one instanced draw)...
    rhi::ICommandList& commands = context.commands;
    commands.beginDebugGroup("forward.meshes");
    if (!opaqueBucketOrder_.empty()) {
        commands.bindPipeline(opaquePipeline_);
        commands.bindBindGroup(0, context.frameBindGroup);
        commands.bindBindGroup(2, slot.bindGroup);
        rhi::BindGroupHandle boundMaterial;
        const GpuMesh* boundMesh = nullptr;
        u32 instanceOffset = 0;
        for (const u32 bucketIndex : opaqueBucketOrder_) {
            const OpaqueBucket& bucket = opaqueBuckets_[bucketIndex];
            const u32 count = static_cast<u32>(bucket.models.size());
            if (bucket.material != boundMaterial) {
                commands.bindBindGroup(1, bucket.material);
                boundMaterial = bucket.material;
            }
            if (bucket.mesh != boundMesh) {
                commands.bindVertexBuffer(bucket.mesh->vertexBuffer);
                commands.bindIndexBuffer(bucket.mesh->indexBuffer, rhi::IndexType::UInt32);
                boundMesh = bucket.mesh;
            }
            commands.drawIndexed(bucket.mesh->indexCount, count, 0, 0, instanceOffset);
            instanceOffset += count;
        }
    }

    // ...and transparent instances (merging consecutive runs sharing material and mesh).
    if (!transparent_.empty()) {
        commands.bindPipeline(transparentPipeline_);
        commands.bindBindGroup(0, context.frameBindGroup);
        commands.bindBindGroup(2, slot.bindGroup);
        rhi::BindGroupHandle boundMaterial;
        const GpuMesh* boundMesh = nullptr;
        const u32 baseOffset = static_cast<u32>(opaqueCount);
        for (usize begin = 0; begin < transparent_.size();) {
            const TransparentItem& first = transparent_[begin];
            usize end = begin + 1;
            while (end < transparent_.size() && transparent_[end].material == first.material &&
                   transparent_[end].mesh == first.mesh) {
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
                                 baseOffset + static_cast<u32>(begin));
            begin = end;
        }
    }
    commands.endDebugGroup();
}

void MeshFeature::renderShadows(const render::RenderFrameContext& context) {
    if (context.world == nullptr) {
        return;
    }
    // Opaque meshes, gathered once per frame (at the first shadow view) and
    // bucketed by mesh (materials do not matter for depth)...
    if (context.firstShadowView) {
        shadowObjectsUsed_ = 0;
        shadowBucketLookup_.clear();
        shadowBucketOrder_.clear();
        u32 activeBuckets = 0;

        const auto* renderers = context.world->storage<render::MeshRenderer>();
        const auto* transforms = context.world->storage<scene::WorldTransform>();
        if (renderers != nullptr && transforms != nullptr) {
            u32 lastMeshKey = ~u32{0};
            u32 lastBucket = 0;
            const std::span<const u32> entities = renderers->entities();
            const std::span<const render::MeshRenderer> items = renderers->values();
            for (usize i = 0; i < entities.size(); ++i) {
                const render::MeshRenderer& renderable = items[i];
                if (!renderable.visible) {
                    continue;
                }
                const scene::WorldTransform* transform = transforms->tryGet(entities[i]);
                if (transform == nullptr) {
                    continue;
                }
                const GpuMesh* mesh = renderer_->mesh(renderable.mesh);
                if (mesh == nullptr || renderer_->materialOrDefault(renderable.material).desc.transparent) {
                    continue;
                }
                const u32 meshKey = renderable.mesh.index;
                u32 bucketIndex = lastBucket;
                if (meshKey != lastMeshKey) {
                    const auto [it, inserted] = shadowBucketLookup_.emplace(meshKey, activeBuckets);
                    if (inserted) {
                        if (activeBuckets == shadowBuckets_.size()) {
                            shadowBuckets_.emplace_back();
                        }
                        ShadowMeshBucket& bucket = shadowBuckets_[activeBuckets];
                        bucket.meshKey = meshKey;
                        bucket.mesh = mesh;
                        bucket.bounds.clear();
                        bucket.models.clear();
                        shadowBucketOrder_.push_back(activeBuckets);
                        ++activeBuckets;
                    }
                    bucketIndex = it->second;
                    lastMeshKey = meshKey;
                    lastBucket = bucketIndex;
                }
                ShadowMeshBucket& bucket = shadowBuckets_[bucketIndex];
                bucket.bounds.push_back(render::transformSphere(mesh->bounds, transform->matrix));
                bucket.models.push_back(&transform->matrix);
            }
            std::sort(shadowBucketOrder_.begin(), shadowBucketOrder_.end(),
                      [this](u32 a, u32 b) { return shadowBuckets_[a].meshKey < shadowBuckets_[b].meshKey; });
        }
    }

    if (shadowBucketOrder_.empty()) {
        return;
    }

    // ...then each cascade culls the compact bounding-sphere arrays directly
    // into the upload buffer, already grouped by mesh.
    objects_.clear();
    shadowBatches_.clear();
    const render::Frustum lightVolume(context.camera.viewProjection);
    for (const u32 bucketIndex : shadowBucketOrder_) {
        const ShadowMeshBucket& bucket = shadowBuckets_[bucketIndex];
        const usize batchStart = objects_.size();
        const std::span<const render::BoundingSphere> bounds = bucket.bounds;
        const std::span<const Mat4* const> models = bucket.models;
        for (usize i = 0; i < bounds.size(); ++i) {
            if (lightVolume.intersects(bounds[i])) {
                objects_.push_back({*models[i], Vec4(0.0f), Vec4(0.0f), Vec4(0.0f)});
            }
        }
        if (objects_.size() > batchStart) {
            shadowBatches_.push_back(
                {bucket.mesh, static_cast<u32>(batchStart), static_cast<u32>(objects_.size() - batchStart)});
        }
    }
    if (shadowBatches_.empty()) {
        return;
    }

    // The shadow views of a frame share the buffer: each writes after the
    // previous one, so none overwrites what an earlier draw still reads.
    const usize first = shadowObjectsUsed_;
    shadowObjectsUsed_ += objects_.size();
    ObjectBuffer& slot = shadowObjectBuffers_[context.device.frameSlot()];
    ensureObjectCapacity(context.device, slot, shadowObjectsUsed_);
    context.device.writeBuffer(slot.buffer, rhi::asBytes(std::span<const ObjectData>(objects_)),
                               first * sizeof(ObjectData));

    rhi::ICommandList& commands = context.commands;
    const std::array<u32, 4> cascade = {context.shadowCascade, 0, 0, 0};
    commands.bindPipeline(shadowPipeline_);
    commands.bindBindGroup(0, context.frameBindGroup);
    commands.bindBindGroup(2, slot.bindGroup);
    commands.pushConstants(0, rhi::asBytes(cascade));
    for (const ShadowBatch& batch : shadowBatches_) {
        commands.bindVertexBuffer(batch.mesh->vertexBuffer);
        commands.bindIndexBuffer(batch.mesh->indexBuffer, rhi::IndexType::UInt32);
        commands.drawIndexed(batch.mesh->indexCount, batch.instanceCount, 0, 0,
                             static_cast<u32>(first) + batch.firstInstance);
    }
}

}  // namespace phi::forward
