#include "MeshFeature.hpp"

#include "ForwardRenderer.hpp"

#include <bdex/assets/AssetManager.hpp>
#include <bdex/scene/Components.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>

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
    pipeline_ = device.createGraphicsPipeline(desc);

    // The pipeline keeps what it needs from the shader modules.
    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);
    return {};
}

void MeshFeature::onDetach(rhi::IDevice& device) {
    device.destroyPipeline(pipeline_);
    pipeline_ = {};
}

void MeshFeature::render(const render::RenderFrameContext& context) {
    if (context.world == nullptr) {
        return;
    }

    // Gather what is visible...
    draws_.clear();
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
            if (!frustum.intersects(render::transformSphere(mesh->bounds, transform.matrix))) {
                return;  // outside the view
            }
            const GpuMaterial& material = renderer_->materialOrDefault(renderable.material);
            draws_.push_back({
                material.bindGroup,
                mesh,
                {transform.matrix, material.desc.baseColor,
                 Vec4(material.desc.specular, material.desc.shininess, 0.0f, 0.0f)},
            });
        });
    if (draws_.empty()) {
        return;
    }

    // ...group it by material, then mesh, so each is bound once...
    std::sort(draws_.begin(), draws_.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.material.index != b.material.index) {
            return a.material.index < b.material.index;
        }
        return std::less<const GpuMesh*>{}(a.mesh, b.mesh);
    });

    // ...and draw it.
    rhi::ICommandList& commands = context.commands;
    commands.beginDebugGroup("forward.meshes");
    commands.bindPipeline(pipeline_);
    commands.bindBindGroup(0, context.frameBindGroup);
    rhi::BindGroupHandle boundMaterial;
    const GpuMesh* boundMesh = nullptr;
    for (const DrawItem& draw : draws_) {
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
    commands.endDebugGroup();
}

}  // namespace bdex::forward
