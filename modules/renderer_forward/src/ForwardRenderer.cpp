#include "ForwardRenderer.hpp"

#include <bdex/render/DebugDraw.hpp>

#include "MeshFeature.hpp"
#include "ParticleFeature.hpp"

#include <bdex/assets/AssetManager.hpp>
#include <bdex/assets/Image.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/render/Shadows.hpp>
#include <bdex/scene/Scene.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace bdex::forward {

ForwardRenderer::ForwardRenderer(Engine& engine, rhi::IDevice& device)
    : engine_(engine), device_(device) {}

ForwardRenderer::~ForwardRenderer() {
    shutdown();
}

// =============================================================================
// Lifecycle
// =============================================================================

Result<> ForwardRenderer::initialize() {
    const Config& config = engine_.config();
    fallbackExtent_ = {
        static_cast<u32>(std::clamp<i64>(config.getInt("renderer.width", 1280), 1, 16384)),
        static_cast<u32>(std::clamp<i64>(config.getInt("renderer.height", 720), 1, 16384)),
    };

    // --- Output: a swapchain when there is a real window, offscreen otherwise --
    auto* windows = engine_.services().tryGet<platform::IWindowSystem>();
    window_ = windows != nullptr ? windows->primaryWindow() : nullptr;
    if (window_ != nullptr && !windows->isHeadless()) {
        const auto swapchain =
            device_.createSwapchain({window_, config.getBool("renderer.vsync", true)});
        if (!swapchain) {
            return fail("cannot create the swapchain: {}", swapchain.error().message);
        }
        swapchain_ = *swapchain;
        colorFormat_ = device_.swapchainFormat(swapchain_);
    } else {
        log::info("renderer", "no presentable window: rendering offscreen");
    }
    initialized_ = true;

    // --- Shadow map -------------------------------------------------------------------
    shadowsEnabled_ = config.getBool("renderer.shadows", true);
    shadowMapSize_ =
        shadowsEnabled_
            ? static_cast<u32>(std::clamp<i64>(config.getInt("renderer.shadow_map_size", 1024), 64, 4096))
            : 1;
    shadowDistance_ =
        static_cast<f32>(std::clamp(config.getFloat("renderer.shadow_distance", 100.0), 1.0, 1000.0));
    rhi::TextureDesc shadowDesc;
    shadowDesc.width = shadowMapSize_ * 4;  // 4 x 2: the sun's cascades, then spot lights'
    shadowDesc.height = shadowMapSize_ * 2;
    shadowDesc.format = shadowFormat_;
    shadowDesc.usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled;
    shadowDesc.debugName = "shadow map";
    shadowMap_ = device_.createTexture(shadowDesc);
    rhi::SamplerDesc shadowSamplerDesc;
    shadowSamplerDesc.addressMode = rhi::AddressMode::ClampToEdge;
    shadowSamplerDesc.compare = true;
    shadowSamplerDesc.compareOp = rhi::CompareOp::LessOrEqual;
    shadowSamplerDesc.debugName = "shadow sampler";
    shadowSampler_ = device_.createSampler(shadowSamplerDesc);

    // --- Frame constants and shadow map (set 0) ------------------------------------
    frameLayout_ = device_.createBindGroupLayout({
        {{0, rhi::BindingType::UniformBuffer,
          rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment},
         {1, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Fragment}},
        "frame layout",
    });
    for (u32 slot = 0; slot < device_.framesInFlight(); ++slot) {
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(render::FrameUniforms);
        bufferDesc.usage = rhi::BufferUsage::Uniform;
        bufferDesc.memory = rhi::MemoryUsage::CpuToGpu;
        bufferDesc.debugName = "frame uniforms";
        frameBuffers_.push_back(device_.createBuffer(bufferDesc));

        rhi::BindGroupDesc groupDesc;
        groupDesc.layout = frameLayout_;
        rhi::BindGroupEntry uniforms;
        uniforms.binding = 0;
        uniforms.buffer = frameBuffers_.back();
        groupDesc.entries.push_back(uniforms);
        rhi::BindGroupEntry shadows;
        shadows.binding = 1;
        shadows.texture = shadowMap_;
        shadows.sampler = shadowSampler_;
        groupDesc.entries.push_back(shadows);
        groupDesc.debugName = "frame bind group";
        frameBindGroups_.push_back(device_.createBindGroup(groupDesc));
    }

    // --- Material resources (set 1) --------------------------------------------------
    materialLayout_ = device_.createBindGroupLayout({
        {{0, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Fragment},
         {1, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Fragment}},
        "material layout",
    });
    rhi::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy =
        static_cast<f32>(std::clamp(config.getFloat("renderer.anisotropy", 8.0), 1.0, 16.0));
    samplerDesc.debugName = "default sampler";
    defaultSampler_ = device_.createSampler(samplerDesc);
    whiteTexture_ = createTexture(assets::makeSolidImage(1, 1, {255, 255, 255, 255}));
    // Tangent-space (0, 0, 1): leaves the surface normal as it is.
    flatNormalTexture_ = createTexture(assets::makeSolidImage(1, 1, {128, 128, 255, 255}),
                                       render::TextureColorSpace::Linear);
    defaultMaterial_ = createMaterial({});

    // --- Post-processing ----------------------------------------------------------------
    hdr_ = config.getBool("renderer.hdr", true);
    if (hdr_) {
        PostProcess::Settings settings;
        settings.exposure =
            static_cast<f32>(std::max(config.getFloat("renderer.exposure", 1.0), 0.0));
        const std::string tonemap = config.getString("renderer.tonemap", "aces");
        if (tonemap != "aces" && tonemap != "none") {
            log::warn("renderer", "unknown renderer.tonemap '{}'; using 'aces'", tonemap);
        }
        settings.aces = tonemap != "none";
        const std::string antialiasing = config.getString("renderer.antialiasing", "fxaa");
        if (antialiasing != "fxaa" && antialiasing != "none") {
            log::warn("renderer", "unknown renderer.antialiasing '{}'; using 'fxaa'", antialiasing);
        }
        settings.antialiasing = antialiasing != "none";
        settings.bloomIntensity = config.getBool("renderer.bloom", true)
                                      ? static_cast<f32>(std::clamp(config.getFloat("renderer.bloom_intensity", 0.04), 0.0, 1.0))
                                      : 0.0f;
        if (const auto created = postProcess_.create(
                device_, engine_.services().get<assets::AssetManager>(), colorFormat_, settings);
            !created) {
            return created;
        }
    }

    if (const auto created = overlay_.create(device_, engine_.services().get<assets::AssetManager>(), colorFormat_);
        !created) {
        return created;
    }

    // --- Built-in features -------------------------------------------------------------
    if (const auto added = addFeature(std::make_unique<MeshFeature>(*this)); !added) {
        return added;
    }
    return addFeature(std::make_unique<ParticleFeature>());
}

void ForwardRenderer::shutdown() {
    if (!initialized_) {
        return;
    }
    initialized_ = false;
    device_.waitIdle();

    for (auto& feature : features_) {
        feature->onDetach(device_);
    }
    features_.clear();

    materials_.forEach([this](render::MaterialHandle, GpuMaterial& material) {
        device_.destroyBindGroup(material.bindGroup);
    });
    materials_.clear();
    textures_.forEach([this](render::TextureHandle, rhi::TextureHandle& texture) {
        device_.destroyTexture(texture);
    });
    textures_.clear();
    meshes_.forEach([this](render::MeshHandle, GpuMesh& mesh) {
        device_.destroyBuffer(mesh.vertexBuffer);
        device_.destroyBuffer(mesh.indexBuffer);
    });
    meshes_.clear();

    for (const rhi::BindGroupHandle group : frameBindGroups_) {
        device_.destroyBindGroup(group);
    }
    for (const rhi::BufferHandle buffer : frameBuffers_) {
        device_.destroyBuffer(buffer);
    }
    frameBindGroups_.clear();
    frameBuffers_.clear();

    postProcess_.destroy(device_);
    overlay_.destroy(device_);
    device_.destroySampler(defaultSampler_);
    device_.destroySampler(shadowSampler_);
    device_.destroyTexture(shadowMap_);
    device_.destroyBindGroupLayout(materialLayout_);
    device_.destroyBindGroupLayout(frameLayout_);
    device_.destroyTexture(depthTarget_);
    device_.destroyTexture(captureTarget_);
    device_.destroySwapchain(swapchain_);
    swapchain_ = {};
}

render::RenderSetupContext ForwardRenderer::setupContext() {
    // With HDR, features draw the scene in 16-bit float; only the post pass
    // writes the output.
    return render::RenderSetupContext{device_,      engine_.services(),
                                      hdr_ ? PostProcess::kSceneFormat : colorFormat_,
                                      depthFormat_, shadowFormat_,      frameLayout_};
}

// =============================================================================
// Resources
// =============================================================================

render::MeshHandle ForwardRenderer::createMesh(const assets::MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        log::warn("renderer", "createMesh called with an empty mesh");
        return {};
    }

    GpuMesh gpuMesh;
    gpuMesh.indexCount = static_cast<u32>(mesh.indices.size());
    gpuMesh.bounds = render::computeBoundingSphere(mesh);

    rhi::BufferDesc vertexDesc;
    vertexDesc.size = mesh.vertices.size() * sizeof(assets::Vertex);
    vertexDesc.usage = rhi::BufferUsage::Vertex;
    vertexDesc.debugName = "mesh vertices";
    gpuMesh.vertexBuffer = device_.createBuffer(vertexDesc);
    device_.writeBuffer(gpuMesh.vertexBuffer, rhi::asBytes(mesh.vertices));

    rhi::BufferDesc indexDesc;
    indexDesc.size = mesh.indices.size() * sizeof(u32);
    indexDesc.usage = rhi::BufferUsage::Index;
    indexDesc.debugName = "mesh indices";
    gpuMesh.indexBuffer = device_.createBuffer(indexDesc);
    device_.writeBuffer(gpuMesh.indexBuffer, rhi::asBytes(mesh.indices));

    return meshes_.insert(gpuMesh);
}

void ForwardRenderer::destroyMesh(render::MeshHandle handle) {
    if (const auto mesh = meshes_.take(handle)) {
        device_.destroyBuffer(mesh->vertexBuffer);
        device_.destroyBuffer(mesh->indexBuffer);
    }
}

render::TextureHandle ForwardRenderer::createTexture(const assets::ImageData& image,
                                                     render::TextureColorSpace colorSpace) {
    if (!image.valid()) {
        log::warn("renderer", "createTexture called with an invalid image");
        return {};
    }
    rhi::TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.format = colorSpace == render::TextureColorSpace::Srgb ? rhi::Format::RGBA8Srgb
                                                                 : rhi::Format::RGBA8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    desc.mipLevels = 0;  // full chain, generated on upload
    desc.debugName = "texture";
    const rhi::TextureHandle texture = device_.createTexture(desc);
    device_.writeTexture(texture, rhi::asBytes(image.pixels));
    return textures_.insert(texture);
}

void ForwardRenderer::destroyTexture(render::TextureHandle handle) {
    if (handle == whiteTexture_ || handle == flatNormalTexture_) {
        return;
    }
    const auto texture = textures_.take(handle);
    if (!texture) {
        return;
    }
    // Materials sampling the texture would keep a dangling descriptor: they
    // fall back to the default texture. Their old bind groups, like the
    // texture itself, are released once in-flight frames are done with them.
    materials_.forEach([&](render::MaterialHandle, GpuMaterial& material) {
        if (material.desc.albedo == handle || material.desc.normalMap == handle) {
            if (material.desc.albedo == handle) {
                material.desc.albedo = {};
            }
            if (material.desc.normalMap == handle) {
                material.desc.normalMap = {};
            }
            device_.destroyBindGroup(material.bindGroup);
            material.bindGroup = createMaterialBindGroup(material.desc);
        }
    });
    device_.destroyTexture(*texture);
}

rhi::BindGroupHandle ForwardRenderer::createMaterialBindGroup(const render::MaterialDesc& desc) {
    const auto textureOr = [&](render::TextureHandle handle, render::TextureHandle fallback) {
        const rhi::TextureHandle* texture = textures_.get(handle);
        return texture != nullptr ? *texture : *textures_.get(fallback);
    };

    rhi::BindGroupDesc groupDesc;
    groupDesc.layout = materialLayout_;
    rhi::BindGroupEntry albedoEntry;
    albedoEntry.binding = 0;
    albedoEntry.texture = textureOr(desc.albedo, whiteTexture_);
    albedoEntry.sampler = defaultSampler_;
    groupDesc.entries.push_back(albedoEntry);
    rhi::BindGroupEntry normalEntry;
    normalEntry.binding = 1;
    normalEntry.texture = textureOr(desc.normalMap, flatNormalTexture_);
    normalEntry.sampler = defaultSampler_;
    groupDesc.entries.push_back(normalEntry);
    groupDesc.debugName = "material";
    return device_.createBindGroup(groupDesc);
}

render::MaterialHandle ForwardRenderer::createMaterial(const render::MaterialDesc& desc) {
    GpuMaterial material;
    material.desc = desc;
    if (!textures_.contains(material.desc.albedo)) {
        material.desc.albedo = {};  // invalid or stale: untextured
    }
    if (!textures_.contains(material.desc.normalMap)) {
        material.desc.normalMap = {};
    }
    material.bindGroup = createMaterialBindGroup(material.desc);
    return materials_.insert(material);
}

void ForwardRenderer::destroyMaterial(render::MaterialHandle handle) {
    if (handle == defaultMaterial_) {
        return;
    }
    if (const auto material = materials_.take(handle)) {
        device_.destroyBindGroup(material->bindGroup);
    }
}

const GpuMaterial& ForwardRenderer::materialOrDefault(render::MaterialHandle handle) const {
    if (const GpuMaterial* material = materials_.get(handle)) {
        return *material;
    }
    return *materials_.get(defaultMaterial_);
}

Result<> ForwardRenderer::addFeature(std::unique_ptr<render::IRenderFeature> feature) {
    if (feature == nullptr) {
        return fail("cannot add a null render feature");
    }
    const std::string_view name = feature->name();
    const bool duplicate = std::any_of(features_.begin(), features_.end(),
                                       [name](const auto& f) { return f->name() == name; });
    if (duplicate) {
        return fail("a render feature named '{}' is already attached", name);
    }
    if (const auto attached = feature->onAttach(setupContext()); !attached) {
        return fail("render feature '{}' failed to attach: {}", name, attached.error().message);
    }

    // Keep features sorted by order; equal orders keep insertion order.
    const auto position = std::upper_bound(
        features_.begin(), features_.end(), feature->order(),
        [](i32 order, const auto& existing) { return order < existing->order(); });
    features_.insert(position, std::move(feature));
    return {};
}

Result<> ForwardRenderer::reloadShaders() {
    device_.waitIdle();  // nothing in flight may use the pipelines
    auto& assets = engine_.services().get<assets::AssetManager>();
    assets.clearCache();  // read the shader files again

    if (hdr_) {
        const PostProcess::Settings settings = postProcess_.settings();
        postProcess_.destroy(device_);  // its targets are recreated next frame
        if (auto created = postProcess_.create(device_, assets, colorFormat_, settings); !created) {
            return fail("post-processing shaders failed to reload: {}", created.error().message);
        }
    }
    overlay_.destroy(device_);
    if (auto created = overlay_.create(device_, assets, colorFormat_); !created) {
        return fail("overlay shaders failed to reload: {}", created.error().message);
    }

    Result<> first;
    for (auto it = features_.begin(); it != features_.end();) {
        (*it)->onDetach(device_);
        if (auto attached = (*it)->onAttach(setupContext()); !attached) {
            log::error("renderer", "render feature '{}' removed: {}", (*it)->name(), attached.error().message);
            if (first) {
                first = fail("render feature '{}' failed to reload: {}", (*it)->name(), attached.error().message);
            }
            it = features_.erase(it);
        } else {
            ++it;
        }
    }
    if (first) {
        log::info("renderer", "shaders reloaded");
    }
    return first;
}

bool ForwardRenderer::removeFeature(std::string_view name) {
    const auto it = std::find_if(features_.begin(), features_.end(),
                                 [name](const auto& f) { return f->name() == name; });
    if (it == features_.end()) {
        return false;
    }
    (*it)->onDetach(device_);
    features_.erase(it);
    return true;
}

// =============================================================================
// Frame
// =============================================================================

Extent2D ForwardRenderer::targetExtent() const {
    return window_ != nullptr ? window_->framebufferSize() : fallbackExtent_;
}

void ForwardRenderer::ensureDepthTarget(Extent2D extent) {
    if (depthTarget_.valid() && depthExtent_ == extent) {
        return;
    }
    device_.destroyTexture(depthTarget_);  // release is deferred by the device
    rhi::TextureDesc desc;
    desc.width = extent.width;
    desc.height = extent.height;
    desc.format = depthFormat_;
    desc.usage = rhi::TextureUsage::DepthStencilAttachment;
    desc.debugName = "depth target";
    depthTarget_ = device_.createTexture(desc);
    depthExtent_ = extent;
}

void ForwardRenderer::ensureCaptureTarget(Extent2D extent) {
    if (captureTarget_.valid() && captureExtent_ == extent) {
        return;
    }
    device_.destroyTexture(captureTarget_);
    rhi::TextureDesc desc;
    desc.width = extent.width;
    desc.height = extent.height;
    // Same format as the swapchain so features need a single pipeline variant.
    desc.format = colorFormat_;
    desc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc;
    desc.debugName = "capture target";
    captureTarget_ = device_.createTexture(desc);
    captureExtent_ = extent;
}

render::DirectionalLight ForwardRenderer::findSun() const {
    render::DirectionalLight light;  // defaults when the scene has none
    if (auto* scenes = engine_.services().tryGet<scene::SceneManager>();
        scenes != nullptr && scenes->active() != nullptr) {
        scenes->active()->world().view<const render::DirectionalLight>().each(
            [&light](const render::DirectionalLight& candidate) { light = candidate; });
    }
    return light;
}

void ForwardRenderer::updateShadowView(const render::CameraData& camera) {
    shadowsThisFrame_ = shadowsEnabled_ && sun_.castShadows;
    if (!shadowsThisFrame_) {
        return;
    }
    // Near plane of the camera, recovered from its projection (z = 0 there).
    const f32 nearPlane = camera.projection[3][2] / -camera.projection[2][2];
    cascadeSplits_ = render::cascadeSplits(std::max(nearPlane, 0.01f), shadowDistance_,
                                           render::FrameUniforms::kShadowCascades);
    for (usize cascade = 0; cascade < cascades_.size(); ++cascade) {
        cascades_[cascade] = render::fitDirectionalShadow(
            camera, sun_.direction, cascade == 0 ? 0.0f : cascadeSplits_[cascade - 1],
            cascadeSplits_[cascade], shadowMapSize_);
    }
}

void ForwardRenderer::renderShadowPass(rhi::ICommandList& commands, const FrameTime& time) {
    // Runs even without shadows: clearing keeps the map in a valid state for
    // the frame bind group that references it.
    // The group spans the whole pass, so GPU timings include the clear.
    commands.beginDebugGroup("shadows");
    const rhi::DepthAttachment depth{shadowMap_, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0f};
    commands.beginRendering({{}, &depth});
    auto* scenes = engine_.services().tryGet<scene::SceneManager>();
    scene::Scene* activeScene = scenes != nullptr ? scenes->active() : nullptr;
    bool first = true;
    // Each view draws into its cell of the 4 x 2 atlas: the sun's cascades
    // in the first row, spot lights' in the second.
    const auto renderView = [&](u32 view, const render::CameraData& camera) {
        const u32 x = (view % 4u) * shadowMapSize_;
        const u32 y = (view / 4u) * shadowMapSize_;
        commands.setViewport({static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(shadowMapSize_),
                              static_cast<f32>(shadowMapSize_), 0.0f, 1.0f});
        commands.setScissor({static_cast<i32>(x), static_cast<i32>(y), shadowMapSize_, shadowMapSize_});
        render::RenderFrameContext context{
            device_,
            commands,
            activeScene != nullptr ? &activeScene->world() : nullptr,
            camera,
            {shadowMapSize_, shadowMapSize_},
            frameBindGroups_[device_.frameSlot()],
            time,
            view,
        };
        context.firstShadowView = first;
        first = false;
        for (const auto& feature : features_) {
            feature->renderShadows(context);
        }
    };
    if (shadowsThisFrame_) {
        for (u32 cascade = 0; cascade < cascades_.size(); ++cascade) {
            renderView(cascade, cascades_[cascade]);
        }
    }
    for (u32 spot = 0; spot < spotShadowViews_.size(); ++spot) {
        renderView(render::FrameUniforms::kShadowCascades + spot, spotShadowViews_[spot]);
    }
    commands.endRendering();
    commands.endDebugGroup();
}

void ForwardRenderer::updateFrameUniforms(const render::CameraData& camera, Extent2D extent,
                                          const FrameTime& time) {
    const render::DirectionalLight& light = sun_;

    render::FrameUniforms uniforms;
    uniforms.view = camera.view;
    uniforms.projection = camera.projection;
    uniforms.viewProjection = camera.viewProjection;
    uniforms.inverseViewProjection = glm::inverse(camera.viewProjection);
    uniforms.cameraPosition = Vec4(camera.position, 1.0f);
    const f32 directionLength = glm::length(light.direction);
    uniforms.lightDirection = Vec4(
        directionLength > 0.0f ? light.direction / directionLength : Vec3(0.0f, -1.0f, 0.0f), 0.0f);
    uniforms.lightColor = Vec4(light.color * light.intensity, 1.0f);
    uniforms.ambientColor = Vec4(light.ambient, 1.0f);
    if (shadowsThisFrame_) {
        for (usize cascade = 0; cascade < cascades_.size(); ++cascade) {
            const render::CameraData& view = cascades_[cascade];
            uniforms.shadowCascades[cascade] = view.viewProjection;
            uniforms.cascadeSplits[static_cast<glm::length_t>(cascade)] = cascadeSplits_[cascade];
            // Biases scale with the size of the cascade's texels in the world.
            const f32 texelWorld = 2.0f / (view.projection[0][0] * static_cast<f32>(shadowMapSize_));
            const f32 depthRange = 1.0f / std::abs(view.projection[2][2]);
            uniforms.cascadeBiases[cascade] = Vec4(texelWorld / depthRange, 1.5f * texelWorld, 0.0f, 0.0f);
        }
    }
    uniforms.shadowParams = Vec4(shadowsThisFrame_ ? 1.0f : 0.0f, 1.0f / static_cast<f32>(shadowMapSize_ * 4),
                                 1.0f / static_cast<f32>(shadowMapSize_ * 2), 0.0f);
    uniforms.timeAndExtent =
        Vec4(static_cast<f32>(time.elapsed), static_cast<f32>(time.delta),
             static_cast<f32>(extent.width), static_cast<f32>(extent.height));

    // Point lights: all of them if they fit, otherwise the closest to the
    // camera (the light's range counts as closeness).
    pointLights_.clear();
    if (auto* scenes = engine_.services().tryGet<scene::SceneManager>();
        scenes != nullptr && scenes->active() != nullptr) {
        scenes->active()->world().view<const render::PointLight, const scene::WorldTransform>().each(
            [&](const render::PointLight& pointLight, const scene::WorldTransform& transform) {
                const Vec3 position(transform.matrix[3]);
                pointLights_.push_back({position, &pointLight,
                                        glm::length(position - camera.position) - pointLight.range});
            });
    }
    constexpr usize kMaxLights = render::FrameUniforms::kMaxPointLights;
    if (pointLights_.size() > kMaxLights) {
        std::ranges::nth_element(pointLights_, pointLights_.begin() + kMaxLights, {},
                                 &PointLightCandidate::distance);
        pointLights_.resize(kMaxLights);
    }
    for (usize i = 0; i < pointLights_.size(); ++i) {
        const PointLightCandidate& candidate = pointLights_[i];
        uniforms.pointLightPositions[i] =
            Vec4(candidate.position, std::max(candidate.light->range, 1e-3f));
        uniforms.pointLightColors[i] = Vec4(candidate.light->color * candidate.light->intensity, 1.0f);
    }
    uniforms.pointLightCount.x = static_cast<f32>(pointLights_.size());

    // Spot lights, chosen the same way.
    spotLights_.clear();
    spotShadowViews_.clear();
    if (auto* scenes = engine_.services().tryGet<scene::SceneManager>();
        scenes != nullptr && scenes->active() != nullptr) {
        scenes->active()->world().view<const render::SpotLight, const scene::WorldTransform>().each(
            [&](const render::SpotLight& spot, const scene::WorldTransform& transform) {
                const Vec3 position(transform.matrix[3]);
                spotLights_.push_back({position, glm::normalize(-Vec3(transform.matrix[2])), &spot,
                                       glm::length(position - camera.position) - spot.range});
            });
    }
    constexpr usize kMaxSpots = render::FrameUniforms::kMaxSpotLights;
    if (spotLights_.size() > kMaxSpots) {
        std::ranges::nth_element(spotLights_, spotLights_.begin() + kMaxSpots, {},
                                 &SpotLightCandidate::distance);
        spotLights_.resize(kMaxSpots);
    }
    for (usize i = 0; i < spotLights_.size(); ++i) {
        const SpotLightCandidate& candidate = spotLights_[i];
        const render::SpotLight& spot = *candidate.light;
        const f32 outer = std::clamp(spot.outerAngle, 1e-3f, glm::radians(89.0f));
        const f32 inner = std::clamp(spot.innerAngle, 0.0f, outer - 1e-3f);
        uniforms.spotLightPositions[i] = Vec4(candidate.position, std::max(spot.range, 1e-3f));
        uniforms.spotLightDirections[i] = Vec4(candidate.direction, std::cos(outer));
        uniforms.spotLightColors[i] = Vec4(spot.color * spot.intensity, std::cos(inner));

        // A shadow for the closest few that ask: a perspective view down
        // the cone, as wide as its outer edge.
        uniforms.spotShadowParams[i] = Vec4(-1.0f, 0.0f, 0.0f, 0.0f);
        if (!shadowsEnabled_ || !spot.castShadows || spotShadowViews_.size() >= render::FrameUniforms::kShadowedSpots) {
            continue;
        }
        const Vec3 up = std::abs(candidate.direction.y) > 0.99f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
        const Mat4 world = glm::inverse(glm::lookAt(candidate.position, candidate.position + candidate.direction, up));
        render::Camera lens;
        lens.verticalFov = std::min(2.0f * outer + glm::radians(2.0f), glm::radians(170.0f));
        lens.nearPlane = std::clamp(spot.range * 0.005f, 0.02f, 0.5f);
        lens.farPlane = std::max(spot.range, lens.nearPlane * 2.0f);
        const render::CameraData view = render::makeCameraData(world, lens, 1.0f);
        const auto index = static_cast<u32>(spotShadowViews_.size());
        uniforms.spotShadows[index] = view.viewProjection;
        // One texel spans 2 tan(fov / 2) / size per unit of distance.
        const f32 texelPerMeter = 2.0f * std::tan(lens.verticalFov * 0.5f) / static_cast<f32>(shadowMapSize_);
        uniforms.spotShadowParams[i] = Vec4(static_cast<f32>(index), 2e-4f, 1.5f * texelPerMeter, 0.0f);
        spotShadowViews_.push_back(view);
    }
    uniforms.pointLightCount.y = static_cast<f32>(spotLights_.size());

    device_.writeBuffer(frameBuffers_[device_.frameSlot()], rhi::asBytes(uniforms));
}

void ForwardRenderer::renderScene(rhi::ICommandList& commands, rhi::TextureHandle target,
                                  Extent2D extent, const render::CameraData& camera,
                                  const FrameTime& time) {
    const std::array<rhi::ColorAttachment, 1> colorAttachments = {{
        {target, rhi::LoadOp::Clear, rhi::StoreOp::Store,
         {clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a}},
    }};
    const rhi::DepthAttachment depthAttachment{depthTarget_, rhi::LoadOp::Clear,
                                               rhi::StoreOp::DontCare, 1.0f};

    commands.beginDebugGroup("scene");  // around the pass: timings include clears
    commands.beginRendering({colorAttachments, &depthAttachment});
    commands.setViewport({0.0f, 0.0f, static_cast<f32>(extent.width),
                          static_cast<f32>(extent.height), 0.0f, 1.0f});
    commands.setScissor({0, 0, extent.width, extent.height});

    auto* scenes = engine_.services().tryGet<scene::SceneManager>();
    scene::Scene* activeScene = scenes != nullptr ? scenes->active() : nullptr;
    const render::RenderFrameContext context{
        device_,
        commands,
        activeScene != nullptr ? &activeScene->world() : nullptr,
        camera,
        extent,
        frameBindGroups_[device_.frameSlot()],
        time,
    };
    for (const auto& feature : features_) {
        feature->render(context);
    }
    commands.endRendering();  // leaves a sampled (HDR) target readable
    commands.endDebugGroup();
}

void ForwardRenderer::renderFrame(const FrameTime& time) {
    rhi::ICommandList& commands = device_.beginFrame();

    // Where this frame goes: the window's backbuffer, or the offscreen target.
    rhi::TextureHandle target;
    if (swapchain_.valid()) {
        target = device_.acquireSwapchainTexture(swapchain_);
    } else if (const Extent2D size = targetExtent(); size.width > 0 && size.height > 0) {
        ensureCaptureTarget(size);
        target = captureTarget_;
    }

    const bool capture = !screenshotPath_.empty();
    bool captured = false;
    if (target.valid()) {
        const rhi::TextureDesc targetDesc = device_.textureDesc(target);
        extent_ = {targetDesc.width, targetDesc.height};
        ensureDepthTarget(extent_);

        // Camera: the scene's primary camera, or a default vantage point.
        const f32 aspect = static_cast<f32>(extent_.width) / static_cast<f32>(extent_.height);
        render::CameraData camera = render::makeCameraData(
            glm::inverse(glm::lookAt(Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f))),
            render::Camera{}, aspect);
        if (auto* scenes = engine_.services().tryGet<scene::SceneManager>();
            scenes != nullptr && scenes->active() != nullptr) {
            // Several primary cameras: the lowest entity slot wins, whatever
            // order the components are stored in.
            u32 chosen = ecs::Entity::kInvalidIndex;
            scenes->active()->world().view<const render::Camera, const scene::WorldTransform>().each(
                [&](ecs::Entity entity, const render::Camera& candidate,
                    const scene::WorldTransform& transform) {
                    if (candidate.primary && entity.index < chosen) {
                        camera = render::makeCameraData(transform.matrix, candidate, aspect);
                        chosen = entity.index;
                    }
                });
        }

        sun_ = findSun();
        updateShadowView(camera);
        updateFrameUniforms(camera, extent_, time);
        renderShadowPass(commands, time);
        // Swapchain images cannot be read back once presented: when rendering
        // to a window, a capture also goes to the capture target, through the
        // post pass with HDR, or by drawing the scene a second time without.
        const bool extraCapture = capture && swapchain_.valid();
        if (extraCapture) {
            ensureCaptureTarget(extent_);
        }
        if (hdr_) {
            renderScene(commands, postProcess_.sceneTarget(device_, extent_), extent_, camera, time);
            postProcess_.prepare(commands);
            postProcess_.apply(commands, target, extent_);
            if (extraCapture) {
                postProcess_.apply(commands, captureTarget_, extent_);
            }
        } else {
            renderScene(commands, target, extent_, camera, time);
            if (extraCapture) {
                renderScene(commands, captureTarget_, extent_, camera, time);
            }
        }
        // Overlay lines last, on the finished image.
        if (const auto* debug = engine_.services().tryGet<render::DebugDraw>()) {
            overlay_.upload(device_, debug->overlayTriangles(), debug->overlayVertices());
            overlay_.draw(commands, target, extent_);
            if (extraCapture) {
                overlay_.draw(commands, captureTarget_, extent_);
            }
        }
        captured = capture;
    }

    device_.endFrame();

    if (captured) {
        saveCapture(screenshotPath_);
        screenshotPath_.clear();
    }
}

void ForwardRenderer::saveCapture(const std::filesystem::path& path) {
    assets::ImageData image;
    image.width = captureExtent_.width;
    image.height = captureExtent_.height;
    image.pixels = device_.readTexture(captureTarget_);  // waits for the GPU

    const auto written = assets::writePng(path, image);
    if (written) {
        log::info("renderer", "screenshot saved to {}", path.string());
    } else {
        log::error("renderer", "screenshot failed: {}", written.error().message);
    }
    engine_.events().publish(render::ScreenshotSaved{path, written.has_value()});
}

}  // namespace bdex::forward
