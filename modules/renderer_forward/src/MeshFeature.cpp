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
    desc.bindGroupLayouts = {context.frameLayout, renderer_->materialLayout()};
    desc.pushConstantSize = sizeof(ObjectConstants);
    desc.pushConstantStages = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment;
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
    device.destroyPipeline(opaquePipeline_);
    device.destroyPipeline(transparentPipeline_);
    opaquePipeline_ = {};
    transparentPipeline_ = {};
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

    // ...then draw opaque surfaces grouped by material and mesh, so each is
    // bound once, and transparent ones from back to front so they blend
    // correctly.
    rhi::ICommandList& commands = context.commands;
    commands.beginDebugGroup("forward.meshes");
    drawAll(commands, context, opaquePipeline_, opaque_, opaqueOrder_);
    drawAll(commands, context, transparentPipeline_, transparent_, transparentOrder_);
    commands.endDebugGroup();
}

void MeshFeature::drawAll(rhi::ICommandList& commands, const render::RenderFrameContext& context,
                          rhi::PipelineHandle pipeline, const std::vector<DrawItem>& draws,
                          std::vector<SortKey>& order) {
    if (draws.empty()) {
        return;
    }
    std::sort(order.begin(), order.end(),
              [](const SortKey& a, const SortKey& b) { return a.key < b.key; });

    commands.bindPipeline(pipeline);
    commands.bindBindGroup(0, context.frameBindGroup);
    rhi::BindGroupHandle boundMaterial;
    const GpuMesh* boundMesh = nullptr;
    for (const SortKey& entry : order) {
        const DrawItem& draw = draws[entry.draw];
        if (draw.material != boundMaterial) {
            commands.bindBindGroup(1, draw.material);
            boundMaterial = draw.material;
        }
        if (draw.mesh != boundMesh) {
            commands.bindVertexBuffer(draw.mesh->vertexBuffer);
            commands.bindIndexBuffer(draw.mesh->indexBuffer, rhi::IndexType::UInt32);
            boundMesh = draw.mesh;
        }
        commands.pushConstants(0, rhi::asBytes(draw.constants));
        commands.drawIndexed(draw.mesh->indexCount);
    }
}

}  // namespace bdex::forward
