// End-to-end tests: the real engine loop with real modules, selected by name
// exactly like an application would. The window and RHI backends are the
// "null" ones, so these run anywhere; the last test repeats the exercise on a
// real GPU when one is available.

#include <bdex/assets/AssetManager.hpp>
#include <bdex/assets/Image.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/core/FileSystem.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/platform/Events.hpp>
#include <bdex/platform/Input.hpp>
#include <bdex/platform/Window.hpp>
#include <bdex/render/DebugDraw.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/rhi/Device.hpp>
#include <bdex/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace bdex;

namespace {

/// An engine wired like app::Application does it, minus the main loop.
struct TestEngine {
    explicit TestEngine(const std::vector<std::string>& modules, Config config = {}) {
        config.setDefault("engine.forced_delta", "0.016");
        config.setDefault("window.width", "320");
        config.setDefault("window.height", "200");
        // Frame statistics then count the scene pass only, and colors come
        // out unchanged; the shadow and HDR tests turn these back on.
        config.setDefault("renderer.shadows", "false");
        config.setDefault("renderer.hdr", "false");
        engine = std::make_unique<Engine>(config);

        auto vfs = std::make_shared<VirtualFileSystem>();
        vfs->mount("engine", fs::executableDirectory() / "assets" / "engine");
        engine->services().provide<VirtualFileSystem>(vfs);
        engine->services().provide<assets::AssetManager>(std::make_shared<assets::AssetManager>(*vfs));
        auto sceneManager = std::make_shared<scene::SceneManager>();
        scenes = sceneManager.get();
        scenes->create("main");
        engine->services().provide<scene::SceneManager>(sceneManager);

        engine->scheduler().add(Phase::PostUpdate, "world_transforms", [this](const FrameTime&) {
            scene::updateWorldTransforms(world());
        });

        engine->modules().addSearchPath(fs::executableDirectory());
        for (const std::string& module : modules) {
            if (const auto loaded = engine->modules().load(module); !loaded) {
                loadError = loaded.error().message;
                return;
            }
        }
        if (const auto initialized = engine->initialize(); !initialized) {
            initError = initialized.error().message;
        }
    }

    [[nodiscard]] bool ready() const { return loadError.empty() && initError.empty(); }
    [[nodiscard]] ecs::World& world() { return scenes->active()->world(); }
    [[nodiscard]] scene::Scene& scene() { return *scenes->active(); }
    [[nodiscard]] rhi::IDevice& device() { return engine->services().get<rhi::IDevice>(); }
    [[nodiscard]] render::IRenderer& renderer() {
        return engine->services().get<render::IRenderer>();
    }

    /// Adds a camera looking at the origin and `count` cubes in front of it.
    std::vector<ecs::Entity> populate(u32 count) {
        const ecs::Entity camera = scene().createEntity("camera");
        world().emplace<render::Camera>(camera);
        world().get<scene::Transform>(camera).position = Vec3(0.0f, 0.0f, 6.0f);

        const render::MeshHandle cube = renderer().createMesh(assets::makeCube());
        render::MaterialDesc red;
        red.baseColor = Vec4(1.0f, 0.2f, 0.1f, 1.0f);
        const render::MaterialHandle material = renderer().createMaterial(red);
        std::vector<ecs::Entity> entities;
        for (u32 i = 0; i < count; ++i) {
            const ecs::Entity entity = scene().createEntity("cube");
            world().get<scene::Transform>(entity).position =
                Vec3(static_cast<f32>(i) * 1.5f - 1.5f, 0.0f, 0.0f);
            world().emplace<render::MeshRenderer>(entity, cube, material);
            entities.push_back(entity);
        }
        return entities;
    }

    std::unique_ptr<Engine> engine;
    scene::SceneManager* scenes = nullptr;
    std::string loadError;
    std::string initError;
};

/// Render feature recording when it is invoked.
class ProbeFeature final : public render::IRenderFeature {
public:
    ProbeFeature(std::string name, i32 order, std::vector<std::string>& trace)
        : name_(std::move(name)), order_(order), trace_(&trace) {}

    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] i32 order() const override { return order_; }
    Result<> onAttach(const render::RenderSetupContext&) override {
        trace_->push_back("attach:" + name_);
        return {};
    }
    void onDetach(rhi::IDevice&) override { trace_->push_back("detach:" + name_); }
    void render(const render::RenderFrameContext& context) override {
        trace_->push_back("render:" + name_);
        lastExtent = context.extent;
        lastCameraPosition = context.camera.position;
    }

    Extent2D lastExtent;
    Vec3 lastCameraPosition{0.0f};

private:
    std::string name_;
    i32 order_;
    std::vector<std::string>* trace_;
};

const std::vector<std::string> kNullStack = {"renderer_forward", "rhi_null", "window_null"};

}  // namespace

TEST(HeadlessEngine, ModulesAreOrderedByCapabilitiesNotByLoadOrder) {
    TestEngine test(kNullStack);  // deliberately listed backwards
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;

    EXPECT_EQ(test.engine->modules().initializationOrder(),
              (std::vector<std::string>{"window_null", "rhi_null", "renderer_forward"}));
    EXPECT_EQ(test.device().info().backend, "null");
    EXPECT_TRUE(test.engine->services().has<render::IRenderer>());
    EXPECT_TRUE(test.engine->services().has<platform::InputState>());
}

TEST(HeadlessEngine, RendererRefusesToStartWithoutAnRhi) {
    TestEngine test({"window_null", "renderer_forward"});
    EXPECT_TRUE(test.loadError.empty());
    EXPECT_NE(test.initError.find("'rhi'"), std::string::npos) << test.initError;
}

TEST(HeadlessEngine, RendererWorksWithoutAnyWindowModule) {
    TestEngine test({"rhi_null", "renderer_forward"});
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(2);
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 2u);
    EXPECT_EQ(test.renderer().renderExtent(), (Extent2D{1280, 720}));  // renderer.* defaults
}

TEST(HeadlessEngine, DrawsEveryVisibleMeshEntity) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    const std::vector<ecs::Entity> cubes = test.populate(3);

    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 3u);
    EXPECT_EQ(test.device().lastFrameStats().triangles, 36u);
    EXPECT_EQ(test.renderer().renderExtent(), (Extent2D{320, 200}));
    // One mesh and one material: a single instanced draw, everything bound once.
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, 1u);
    EXPECT_EQ(test.device().lastFrameStats().pipelineBinds, 1u);
    EXPECT_EQ(test.device().lastFrameStats().bindGroupBinds, 3u);  // frame, objects, material
    EXPECT_EQ(test.device().lastFrameStats().bufferBinds, 2u);     // vertices + indices

    test.world().get<render::MeshRenderer>(cubes[0]).visible = false;
    test.world().destroy(cubes[1]);
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 1u);
}

TEST(HeadlessEngine, DestroyedRenderResourcesAreSkippedNotDereferenced) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    const std::vector<ecs::Entity> cubes = test.populate(2);

    const render::MeshRenderer renderable = test.world().get<render::MeshRenderer>(cubes[0]);
    test.renderer().destroyMaterial(renderable.material);  // falls back to the default material
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 2u);

    test.renderer().destroyMesh(renderable.mesh);  // stale mesh handles are ignored
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 0u);
}

TEST(HeadlessEngine, ApplicationsExtendTheRendererWithFeatures) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(1);

    std::vector<std::string> trace;
    auto overlay = std::make_unique<ProbeFeature>("overlay", 50, trace);
    ProbeFeature* overlayPtr = overlay.get();
    ASSERT_TRUE(test.renderer().addFeature(std::move(overlay)).has_value());
    ASSERT_TRUE(
        test.renderer().addFeature(std::make_unique<ProbeFeature>("background", -50, trace)).has_value());
    // Names are unique.
    EXPECT_FALSE(
        test.renderer().addFeature(std::make_unique<ProbeFeature>("overlay", 0, trace)).has_value());

    trace.clear();
    test.engine->tick();
    EXPECT_EQ(trace, (std::vector<std::string>{"render:background", "render:overlay"}));
    EXPECT_EQ(overlayPtr->lastExtent, (Extent2D{320, 200}));

    EXPECT_TRUE(test.renderer().removeFeature("background"));
    EXPECT_FALSE(test.renderer().removeFeature("background"));
    trace.clear();
    test.engine->tick();
    EXPECT_EQ(trace, (std::vector<std::string>{"render:overlay"}));

    // Remaining features are detached when the renderer module shuts down.
    trace.clear();
    test.engine->shutdown();
    EXPECT_EQ(trace, (std::vector<std::string>{"detach:overlay"}));
}

TEST(HeadlessEngine, InputStateIsDrivenByEvents) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    auto& input = test.engine->services().get<platform::InputState>();
    using platform::Key;

    // Events arriving during a frame are visible to that frame's Update.
    std::vector<std::string> seen;
    test.engine->scheduler().add(Phase::Update, "probe", [&](const FrameTime&) {
        seen.push_back(std::string(input.keyPressed(Key::W) ? "pressed " : "") +
                       (input.keyDown(Key::W) ? "down" : "up"));
    });
    test.engine->scheduler().add(
        Phase::Input, "inject",
        [&](const FrameTime& time) {
            if (time.frame == 0) {
                test.engine->events().publish(platform::KeyEvent{Key::W, platform::KeyAction::Press});
                test.engine->events().publish(platform::MouseMoved{10.0, 10.0});
                test.engine->events().publish(platform::MouseMoved{14.0, 7.0});
            } else if (time.frame == 2) {
                test.engine->events().publish(
                    platform::KeyEvent{Key::W, platform::KeyAction::Release});
            }
        },
        100);  // after the window module's own Input task

    test.engine->tick();
    EXPECT_EQ(input.mouseDelta(), Vec2(4.0f, -3.0f));
    test.engine->tick();
    EXPECT_EQ(input.mouseDelta(), Vec2(0.0f));
    test.engine->tick();
    EXPECT_EQ(seen, (std::vector<std::string>{"pressed down", "down", "up"}));
}

TEST(HeadlessEngine, WindowCloseRequestReachesSubscribers) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;

    const Subscription subscription =
        test.engine->events().subscribe<platform::WindowCloseRequested>(
            [&](const platform::WindowCloseRequested&) { test.engine->requestExit(); });

    auto* window = test.engine->services().get<platform::IWindowSystem>().primaryWindow();
    ASSERT_NE(window, nullptr);
    window->requestClose();
    EXPECT_FALSE(test.engine->tick());  // the queued event is delivered during the frame
}

TEST(HeadlessEngine, ScreenshotIsWrittenAndAnnounced) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(1);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "bdex_tests_null_capture.png";
    std::filesystem::remove(path);

    bool announced = false;
    const Subscription subscription = test.engine->events().subscribe<render::ScreenshotSaved>(
        [&](const render::ScreenshotSaved& event) { announced = event.success; });

    test.renderer().requestScreenshot(path);
    test.engine->tick();

    EXPECT_TRUE(announced);
    const auto bytes = fs::readBinaryFile(path);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GT(bytes->size(), 8u);
    EXPECT_EQ(std::string(bytes->begin() + 1, bytes->begin() + 4), "PNG");
    std::filesystem::remove(path);
}

TEST(HeadlessEngine, DrawsAreGroupedByMaterialAndMesh) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(0);  // camera only

    // Two materials and two meshes, interleaved in creation order.
    const render::MeshHandle cube = test.renderer().createMesh(assets::makeCube());
    const render::MeshHandle sphere = test.renderer().createMesh(assets::makeSphere());
    render::MaterialDesc red;
    red.baseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    render::MaterialDesc blue;
    blue.baseColor = Vec4(0.0f, 0.0f, 1.0f, 1.0f);
    const render::MaterialHandle materials[] = {test.renderer().createMaterial(red),
                                                test.renderer().createMaterial(blue)};
    for (int i = 0; i < 8; ++i) {
        const ecs::Entity entity = test.scene().createEntity("item");
        test.world().get<scene::Transform>(entity).position =
            Vec3(static_cast<f32>(i % 4) - 1.5f, static_cast<f32>(i / 4) - 0.5f, 0.0f);
        test.world().emplace<render::MeshRenderer>(entity, (i / 2) % 2 == 0 ? cube : sphere,
                                                   materials[i % 2]);
    }

    test.engine->tick();
    const rhi::FrameStats stats = test.device().lastFrameStats();
    EXPECT_EQ(stats.drawCalls, 4u);  // one instanced draw per (material, mesh)
    EXPECT_EQ(stats.instances, 8u);
    EXPECT_EQ(stats.bindGroupBinds, 2u + 2u);  // frame and objects, then each material once
    // Each material uses both meshes: at most 2 mesh switches per material.
    EXPECT_EQ(stats.bufferBinds, 2u * 4u);
}

TEST(HeadlessEngine, MeshesOutsideTheViewAreNotDrawn) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    const std::vector<ecs::Entity> cubes = test.populate(3);  // in front of the camera

    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 3u);

    // Behind the camera, and far off to the side.
    test.world().get<scene::Transform>(cubes[0]).position = Vec3(0.0f, 0.0f, 20.0f);
    test.world().get<scene::Transform>(cubes[1]).position = Vec3(500.0f, 0.0f, 0.0f);
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 1u);

    // A cube scaled up enough reaches back into the view.
    test.world().get<scene::Transform>(cubes[1]).scale = Vec3(1000.0f);
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 2u);
}

TEST(HeadlessEngine, OpaqueMeshesCastShadowsFromTheSun) {
    Config config;
    config.set("renderer.shadows", "true");
    TestEngine test(kNullStack, config);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(3);  // opaque cubes
    const ecs::Entity sun = test.scene().createEntity("sun");
    test.world().emplace<render::DirectionalLight>(sun);

    render::MaterialDesc glass;
    glass.transparent = true;
    const ecs::Entity pane = test.scene().createEntity("pane");
    test.world().emplace<render::MeshRenderer>(pane, test.renderer().createMesh(assets::makeCube()),
                                               test.renderer().createMaterial(glass));

    // Shadow pass (3 opaque casters) + main pass (4 meshes).
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 3u + 4u);

    test.world().get<render::DirectionalLight>(sun).castShadows = false;
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().instances, 4u);
}

TEST(HeadlessEngine, TheRendererReportsMeshBounds) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    const render::MeshHandle cube = test.renderer().createMesh(assets::makeCube(2.0f));
    const auto bounds = test.renderer().meshBounds(cube);
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->radius, std::sqrt(3.0f), 1e-5f);
    test.renderer().destroyMesh(cube);
    EXPECT_FALSE(test.renderer().meshBounds(cube).has_value());
}

TEST(HeadlessEngine, DebugLinesAreDrawnForOneFrame) {
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;
    test.populate(1);
    auto& debug = test.engine->services().get<render::DebugDraw>();

    test.engine->tick();
    const u32 sceneDraws = test.device().lastFrameStats().drawCalls;

    // Added during the frame (here from an Update task), drawn in one call.
    bool drawLines = true;
    test.engine->scheduler().add(Phase::Update, "lines", [&](const FrameTime&) {
        if (drawLines) {
            debug.box(Vec3(-1.0f), Vec3(1.0f));
            debug.line(Vec3(0.0f), Vec3(0.0f, 3.0f, 0.0f));
        }
    });
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, sceneDraws + 1);
    EXPECT_EQ(debug.lineCount(), 0u);  // cleared once the frame is over

    drawLines = false;
    test.engine->tick();
    EXPECT_EQ(test.device().lastFrameStats().drawCalls, sceneDraws);
}

TEST(HeadlessEngine, TheFirstPrimaryCameraIsUsed) {
    std::vector<std::string> trace;  // outlives the engine, which detaches the probe
    TestEngine test(kNullStack);
    ASSERT_TRUE(test.ready()) << test.loadError << test.initError;

    auto probe = std::make_unique<ProbeFeature>("probe", 0, trace);
    ProbeFeature* probePtr = probe.get();
    ASSERT_TRUE(test.renderer().addFeature(std::move(probe)).has_value());

    const auto addCamera = [&](const Vec3& position, bool primary) {
        const ecs::Entity camera = test.scene().createEntity("camera");
        test.world().emplace<render::Camera>(camera).primary = primary;
        test.world().get<scene::Transform>(camera).position = position;
        return camera;
    };
    addCamera(Vec3(9.0f, 0.0f, 0.0f), false);
    const ecs::Entity first = addCamera(Vec3(1.0f, 0.0f, 0.0f), true);
    addCamera(Vec3(2.0f, 0.0f, 0.0f), true);

    test.engine->tick();
    EXPECT_FLOAT_EQ(probePtr->lastCameraPosition.x, 1.0f);

    // Once the first one is gone, the next primary camera takes over.
    test.world().destroy(first);
    test.engine->tick();
    EXPECT_FLOAT_EQ(probePtr->lastCameraPosition.x, 2.0f);
}

// --- Real GPU -------------------------------------------------------------------------
// Same engine, same renderer module, same scene: only the RHI module name
// changes. Skipped on machines without a usable Vulkan 1.3 device.

TEST(VulkanHeadless, RendersTheSceneOffscreenWithoutValidationErrors) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);  // keep the test output focused
    TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    EXPECT_EQ(test.device().info().backend, "vulkan");

    test.renderer().setClearColor(Vec4(0.0f, 0.0f, 1.0f, 1.0f));
    test.populate(3);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "bdex_tests_vulkan_capture.png";
    std::filesystem::remove(path);

    // A few frames so both frame slots and deferred releases are exercised.
    for (int i = 0; i < 4; ++i) {
        test.engine->tick();
    }
    EXPECT_EQ(test.device().lastFrameStats().instances, 3u);

    test.renderer().requestScreenshot(path);
    test.engine->tick();
    const auto png = fs::readBinaryFile(path);
    ASSERT_TRUE(png.has_value());
    std::filesystem::remove(path);

    // Check what was actually rendered: the corner shows the clear color, the
    // center shows the middle cube (red-ish material, lit).
    const auto decoded = assets::decodePng(*png);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    const assets::ImageData& frame = *decoded;
    ASSERT_EQ(frame.width, 320u);
    ASSERT_EQ(frame.height, 200u);
    ASSERT_TRUE(frame.valid());
    const auto pixel = [&](u32 x, u32 y) { return frame.pixels.data() + (y * frame.width + x) * 4; };

    const u8* corner = pixel(2, 2);
    EXPECT_EQ(corner[0], 0);
    EXPECT_EQ(corner[1], 0);
    EXPECT_EQ(corner[2], 255);
    EXPECT_EQ(corner[3], 255);

    const u8* center = pixel(frame.width / 2, frame.height / 2);
    EXPECT_GT(center[0], 60);          // lit
    EXPECT_GT(center[0], center[1]);   // red dominates
    EXPECT_GT(center[0], center[2]);

    EXPECT_EQ(test.device().validationErrorCount(), 0u);

    test.engine->shutdown();  // must release every GPU object cleanly
}

TEST(VulkanHeadless, RhiRoundTripsTextureData) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);
    TestEngine test({"rhi_vulkan"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    rhi::IDevice& device = test.device();

    // Upload a known image, read it back, compare byte for byte.
    const assets::ImageData image =
        assets::makeCheckerboard(64, 4, {255, 0, 0, 255}, {0, 64, 255, 128});
    rhi::TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.format = rhi::Format::RGBA8Unorm;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc;
    desc.mipLevels = 0;  // full chain: exercises mip generation as well
    desc.debugName = "round trip";
    const rhi::TextureHandle texture = device.createTexture(desc);
    device.writeTexture(texture, rhi::asBytes(image.pixels));

    EXPECT_EQ(device.readTexture(texture), image.pixels);  // level 0 is untouched
    EXPECT_EQ(device.textureDesc(texture).width, 64u);
    EXPECT_EQ(device.textureDesc(texture).mipLevels, 7u);  // 64, 32, 16, 8, 4, 2, 1

    device.destroyTexture(texture);
    EXPECT_EQ(device.textureDesc(texture).width, 0u);  // stale handle
    EXPECT_EQ(device.validationErrorCount(), 0u);
}

TEST(VulkanHeadless, BindGroupsOutgrowASingleDescriptorPool) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);
    TestEngine test({"rhi_vulkan"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    rhi::IDevice& device = test.device();

    const rhi::BindGroupLayoutHandle layout = device.createBindGroupLayout({
        {{0, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Vertex}},
        "uniform layout",
    });
    rhi::BufferDesc bufferDesc;
    bufferDesc.size = 256;
    bufferDesc.usage = rhi::BufferUsage::Uniform;
    bufferDesc.memory = rhi::MemoryUsage::CpuToGpu;
    const rhi::BufferHandle buffer = device.createBuffer(bufferDesc);

    // More uniform-buffer descriptors than one pool holds.
    constexpr int kGroupCount = 5000;
    std::vector<rhi::BindGroupHandle> groups;
    for (int i = 0; i < kGroupCount; ++i) {
        rhi::BindGroupDesc groupDesc;
        groupDesc.layout = layout;
        rhi::BindGroupEntry entry;
        entry.binding = 0;
        entry.buffer = buffer;
        groupDesc.entries.push_back(entry);
        groups.push_back(device.createBindGroup(groupDesc));
        ASSERT_TRUE(groups.back().valid());
    }
    for (const rhi::BindGroupHandle group : groups) {
        device.destroyBindGroup(group);
    }
    device.destroyBuffer(buffer);
    device.destroyBindGroupLayout(layout);
    device.waitIdle();
    EXPECT_EQ(device.validationErrorCount(), 0u);
}

TEST(VulkanHeadless, DestroyingATextureInUseKeepsMaterialsValid) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Error);
    TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
    if (!test.ready()) {
        log::setLevel(log::Level::Info);
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }

    test.populate(1);
    const render::TextureHandle texture =
        test.renderer().createTexture(assets::makeCheckerboard(16, 4, {255, 255, 255, 255},
                                                               {0, 0, 0, 255}));
    render::MaterialDesc textured;
    textured.albedo = texture;
    const render::MaterialHandle material = test.renderer().createMaterial(textured);
    const ecs::Entity cube = test.scene().createEntity("textured cube");
    test.world().emplace<render::MeshRenderer>(
        cube, test.renderer().createMesh(assets::makeCube()), material);

    test.engine->tick();
    test.renderer().destroyTexture(texture);  // the material still uses it
    for (int i = 0; i < 4; ++i) {
        test.engine->tick();
    }
    log::setLevel(log::Level::Info);

    EXPECT_EQ(test.device().validationErrorCount(), 0u);
    test.engine->shutdown();
}

TEST(VulkanHeadless, PointLightsLightTheScene) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);
    TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    test.renderer().setClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const ecs::Entity cube = test.populate(1).front();  // red-ish cube, camera at z = 6
    test.world().get<scene::Transform>(cube).position = Vec3(0.0f);

    // No sun, no ambient: only point lights contribute.
    const ecs::Entity sun = test.scene().createEntity("sun");
    test.world().emplace<render::DirectionalLight>(sun).intensity = 0.0f;
    test.world().get<render::DirectionalLight>(sun).ambient = Vec3(0.0f);

    const auto centerPixel = [&]() -> std::array<u8, 4> {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "bdex_tests_point_lights.png";
        test.renderer().requestScreenshot(path);
        test.engine->tick();
        const auto png = fs::readBinaryFile(path);
        std::filesystem::remove(path);
        const auto image = png ? assets::decodePng(*png) : Result<assets::ImageData>{};
        if (!image) {
            ADD_FAILURE() << "no capture";
            return {};
        }
        const u8* p = image->pixels.data() + (image->height / 2 * image->width + image->width / 2) * 4;
        return {p[0], p[1], p[2], p[3]};
    };
    EXPECT_LT(centerPixel()[0], 8);  // unlit

    // More lights than the renderer keeps, all far away and created both
    // before and after the lamp: whatever the storage order, the selection
    // must keep the one next to the cube.
    const auto addFarLights = [&](int count) {
        for (int i = 0; i < count; ++i) {
            const ecs::Entity far = test.scene().createEntity("far light");
            test.world().get<scene::Transform>(far).position =
                Vec3(0.0f, 500.0f + static_cast<f32>(i), 0.0f);
            test.world().emplace<render::PointLight>(far).range = 1.0f;
        }
    };
    addFarLights(20);
    const ecs::Entity lamp = test.scene().createEntity("lamp");
    test.world().get<scene::Transform>(lamp).position = Vec3(0.0f, 0.0f, 2.0f);
    test.world().emplace<render::PointLight>(lamp, render::PointLight{Vec3(1.0f, 0.0f, 0.0f), 4.0f, 6.0f});
    addFarLights(20);

    const std::array<u8, 4> lit = centerPixel();
    EXPECT_GT(lit[0], 100);           // the face toward the lamp is red
    EXPECT_LT(lit[1], 20);
    EXPECT_EQ(test.device().validationErrorCount(), 0u);
}

TEST(VulkanHeadless, TransparentSurfacesBlendBackToFront) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);
    TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    test.renderer().setClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    test.populate(0);  // camera at z = 6, looking at the origin

    // Unlit look: full ambient, no sun, so a surface shows its base color.
    const ecs::Entity sun = test.scene().createEntity("sun");
    test.world().emplace<render::DirectionalLight>(sun).intensity = 0.0f;
    test.world().get<render::DirectionalLight>(sun).ambient = Vec3(1.0f);

    const render::MeshHandle cube = test.renderer().createMesh(assets::makeCube());
    const auto addPane = [&](const Vec4& color, f32 z) {
        render::MaterialDesc desc;
        desc.baseColor = color;
        desc.specular = 0.0f;
        desc.transparent = true;
        const ecs::Entity pane = test.scene().createEntity("pane");
        test.world().get<scene::Transform>(pane).position = Vec3(0.0f, 0.0f, z);
        test.world().emplace<render::MeshRenderer>(pane, cube, test.renderer().createMaterial(desc));
    };
    // The far pane is created first, so storage order alone would draw it last.
    addPane(Vec4(0.0f, 0.0f, 1.0f, 0.5f), -1.5f);  // far, blue
    addPane(Vec4(1.0f, 0.0f, 0.0f, 0.5f), 1.5f);   // near, red

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "bdex_tests_transparency.png";
    test.renderer().requestScreenshot(path);
    test.engine->tick();
    const auto png = fs::readBinaryFile(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(png.has_value());
    const auto image = assets::decodePng(*png);
    ASSERT_TRUE(image.has_value()) << image.error().message;
    const u8* center = image->pixels.data() + (image->height / 2 * image->width + image->width / 2) * 4;

    // Back to front, red ends on top: half red over half blue over black.
    EXPECT_GT(center[0], center[2]);
    EXPECT_GT(center[2], 50);  // the far pane still shows through
    EXPECT_EQ(test.device().lastFrameStats().pipelineBinds, 1u);  // transparent only
    EXPECT_EQ(test.device().validationErrorCount(), 0u);
}

TEST(VulkanHeadless, TheSunCastsShadows) {
    const auto render = [](bool shadows) -> std::pair<int, int> {
        Config config;
        config.set("rhi.validation", "true");
        config.set("renderer.shadows", shadows ? "true" : "false");
        log::setLevel(log::Level::Warn);
        TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
        log::setLevel(log::Level::Info);
        if (!test.ready()) {
            return {-1, -1};
        }
        // Looking down at a ground plane, with a cube floating above it and
        // the sun straight overhead.
        const ecs::Entity camera = test.scene().createEntity("camera");
        test.world().emplace<render::Camera>(camera);
        scene::Transform& eye = test.world().get<scene::Transform>(camera);
        eye.position = Vec3(0.0f, 10.0f, 10.0f);
        eye.rotation = glm::quatLookAt(glm::normalize(Vec3(0.0f, -1.0f, -1.0f)), Vec3(0.0f, 1.0f, 0.0f));

        const ecs::Entity sun = test.scene().createEntity("sun");
        test.world().emplace<render::DirectionalLight>(
            sun, render::DirectionalLight{.direction = Vec3(0.0f, -1.0f, 0.0f),
                                          .color = Vec3(1.0f),
                                          .intensity = 1.0f,
                                          .ambient = Vec3(0.1f)});
        const ecs::Entity ground = test.scene().createEntity("ground");
        test.world().emplace<render::MeshRenderer>(
            ground, test.renderer().createMesh(assets::makePlane(20.0f)),
            test.renderer().createMaterial({}));
        const ecs::Entity cube = test.scene().createEntity("cube");
        test.world().get<scene::Transform>(cube).position = Vec3(0.0f, 3.0f, 0.0f);
        test.world().get<scene::Transform>(cube).scale = Vec3(2.0f);
        test.world().emplace<render::MeshRenderer>(
            cube, test.renderer().createMesh(assets::makeCube()), test.renderer().createMaterial({}));

        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "bdex_tests_shadows.png";
        test.engine->tick();  // world transforms
        test.renderer().requestScreenshot(path);
        test.engine->tick();
        const auto png = fs::readBinaryFile(path);
        std::filesystem::remove(path);
        const auto image = png ? assets::decodePng(*png) : Result<assets::ImageData>{};
        if (!image || test.device().validationErrorCount() != 0) {
            return {-2, -2};
        }

        // Where two ground points land on screen.
        const render::CameraData view = render::makeCameraData(
            test.world().get<scene::WorldTransform>(camera).matrix, render::Camera{},
            static_cast<f32>(image->width) / static_cast<f32>(image->height));
        const auto brightness = [&](const Vec3& point) {
            const Vec4 clip = view.viewProjection * Vec4(point, 1.0f);
            const u32 x = static_cast<u32>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<f32>(image->width));
            const u32 y = static_cast<u32>((clip.y / clip.w * 0.5f + 0.5f) * static_cast<f32>(image->height));
            return static_cast<int>(image->pixels[(y * image->width + x) * 4]);
        };
        // Under the cube, near its front edge (seen past the cube), and well
        // away from it.
        return {brightness(Vec3(0.0f, 0.0f, 0.8f)), brightness(Vec3(5.0f, 0.0f, 5.0f))};
    };

    const auto [shadowed, lit] = render(true);
    if (shadowed == -1) {
        GTEST_SKIP() << "Vulkan is not available here";
    }
    ASSERT_NE(shadowed, -2) << "capture failed or validation errors";
    EXPECT_LT(shadowed * 2, lit) << "shadowed " << shadowed << ", lit " << lit;

    // Without shadows, both points are lit alike.
    const auto [first, second] = render(false);
    EXPECT_NEAR(first, second, 6);
}

TEST(VulkanHeadless, HdrKeepsLightAboveWhiteForToneMapping) {
    // Center pixel (red channel) of a white face lit head-on by a sun of
    // intensity 2: twice what an 8-bit target can hold.
    const auto render = [](std::string_view tonemap, std::string_view exposure) -> int {
        Config config;
        config.set("rhi.validation", "true");
        config.set("renderer.hdr", "true");
        config.set("renderer.tonemap", tonemap);
        config.set("renderer.exposure", exposure);
        config.set("renderer.bloom", "false");  // exact tone mapping values
        log::setLevel(log::Level::Warn);
        TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
        log::setLevel(log::Level::Info);
        if (!test.ready()) {
            return -1;
        }
        test.populate(0);  // camera at z = 6, looking down -Z
        const ecs::Entity sun = test.scene().createEntity("sun");
        test.world().emplace<render::DirectionalLight>(
            sun, render::DirectionalLight{.direction = Vec3(0.0f, 0.0f, -1.0f),
                                          .color = Vec3(1.0f),
                                          .intensity = 2.0f,
                                          .ambient = Vec3(0.0f)});
        render::MaterialDesc white;
        white.specular = 0.0f;
        const ecs::Entity cube = test.scene().createEntity("cube");
        test.world().emplace<render::MeshRenderer>(cube, test.renderer().createMesh(assets::makeCube()),
                                                   test.renderer().createMaterial(white));

        const std::filesystem::path path = std::filesystem::temp_directory_path() / "bdex_tests_hdr.png";
        test.renderer().requestScreenshot(path);
        test.engine->tick();
        const auto png = fs::readBinaryFile(path);
        std::filesystem::remove(path);
        const auto image = png ? assets::decodePng(*png) : Result<assets::ImageData>{};
        if (!image || test.device().validationErrorCount() != 0) {
            return -2;
        }
        return image->pixels[(image->height / 2 * image->width + image->width / 2) * 4];
    };

    const int halfExposure = render("none", "0.25");
    if (halfExposure == -1) {
        GTEST_SKIP() << "Vulkan is not available here";
    }
    // 2.0 * 0.25 = 0.5 linear, 188 in sRGB. Had the scene pass clipped at
    // 1.0, it would be 0.25 linear (137).
    EXPECT_NEAR(halfExposure, 188, 3);

    // Without tone mapping 2.0 clips to white; ACES rolls it off below.
    EXPECT_EQ(render("none", "1"), 255);
    const int aces = render("aces", "1");
    EXPECT_GT(aces, 230);
    EXPECT_LT(aces, 252);
}

TEST(VulkanHeadless, EmissiveSurfacesShineInTheDark) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);
    TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    test.renderer().setClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    test.populate(0);
    const ecs::Entity sun = test.scene().createEntity("sun");  // no light at all
    test.world().emplace<render::DirectionalLight>(sun).intensity = 0.0f;
    test.world().get<render::DirectionalLight>(sun).ambient = Vec3(0.0f);

    render::MaterialDesc glowing;
    glowing.emissive = Vec3(0.5f, 0.0f, 0.0f);
    const ecs::Entity cube = test.scene().createEntity("cube");
    test.world().emplace<render::MeshRenderer>(cube, test.renderer().createMesh(assets::makeCube()),
                                               test.renderer().createMaterial(glowing));

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "bdex_tests_emissive.png";
    test.renderer().requestScreenshot(path);
    test.engine->tick();
    const auto png = fs::readBinaryFile(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(png.has_value());
    const auto image = assets::decodePng(*png);
    ASSERT_TRUE(image.has_value()) << image.error().message;
    const u8* center = image->pixels.data() + (image->height / 2 * image->width + image->width / 2) * 4;
    EXPECT_NEAR(center[0], 188, 3);  // 0.5 linear, encoded as sRGB
    EXPECT_EQ(center[1], 0);
    EXPECT_EQ(test.device().validationErrorCount(), 0u);
}

TEST(VulkanHeadless, NormalMapsBendTheLighting) {
    // Center pixel (red) of a white face lit head-on, through a normal map
    // tilting every normal 45 degrees toward +U.
    const auto render = [](f32 strength, Vec3 lightDirection) -> int {
        Config config;
        config.set("rhi.validation", "true");
        log::setLevel(log::Level::Warn);
        TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
        log::setLevel(log::Level::Info);
        if (!test.ready()) {
            return -1;
        }
        test.populate(0);  // camera at z = 6, looking down -Z
        const ecs::Entity sun = test.scene().createEntity("sun");
        test.world().emplace<render::DirectionalLight>(
            sun, render::DirectionalLight{.direction = lightDirection,
                                          .color = Vec3(1.0f),
                                          .intensity = 1.0f,
                                          .ambient = Vec3(0.0f)});
        // (sin 45, 0, cos 45) encoded in 0..255.
        const u8 tilted = static_cast<u8>(std::lround((0.70710678f * 0.5f + 0.5f) * 255.0f));
        render::MaterialDesc bumpy;
        bumpy.specular = 0.0f;
        bumpy.normalStrength = strength;
        bumpy.normalMap = test.renderer().createTexture(
            assets::makeSolidImage(4, 4, {tilted, 128, tilted, 255}), render::TextureColorSpace::Linear);
        const ecs::Entity cube = test.scene().createEntity("cube");
        test.world().emplace<render::MeshRenderer>(cube, test.renderer().createMesh(assets::makeCube()),
                                                   test.renderer().createMaterial(bumpy));

        const std::filesystem::path path = std::filesystem::temp_directory_path() / "bdex_tests_normal_map.png";
        test.renderer().requestScreenshot(path);
        test.engine->tick();
        const auto png = fs::readBinaryFile(path);
        std::filesystem::remove(path);
        const auto image = png ? assets::decodePng(*png) : Result<assets::ImageData>{};
        if (!image || test.device().validationErrorCount() != 0) {
            return -2;
        }
        return image->pixels[(image->height / 2 * image->width + image->width / 2) * 4];
    };

    const Vec3 headOn(0.0f, 0.0f, -1.0f);
    const int bent = render(1.0f, headOn);
    if (bent == -1) {
        GTEST_SKIP() << "Vulkan is not available here";
    }
    // cos 45 = 0.707 linear, 219 in sRGB.
    EXPECT_NEAR(bent, 219, 4);
    EXPECT_EQ(render(0.0f, headOn), 255);  // strength 0: the flat surface, fully lit

    // Light coming from +X+Z faces the bent normals (+U is +X on the front
    // face): full light. A flipped tangent would face away and stay black.
    const Vec3 fromRight = glm::normalize(Vec3(-1.0f, 0.0f, -1.0f));
    EXPECT_EQ(render(1.0f, fromRight), 255);
    EXPECT_NEAR(render(0.0f, fromRight), 219, 4);
}

TEST(VulkanHeadless, DebugLinesShowUnlessHidden) {
    Config config;
    config.set("rhi.validation", "true");
    log::setLevel(log::Level::Warn);
    TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
    log::setLevel(log::Level::Info);
    if (!test.ready()) {
        GTEST_SKIP() << "Vulkan is not available here: " << test.loadError << test.initError;
    }
    test.renderer().setClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    test.populate(0);  // camera at z = 6, looking down -Z
    auto& debug = test.engine->services().get<render::DebugDraw>();

    const auto capture = [&]() -> assets::ImageData {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "bdex_tests_debug.png";
        // A horizontal red line through the middle of the view, behind z = 0.
        debug.line(Vec3(-3.0f, 0.0f, -1.0f), Vec3(3.0f, 0.0f, -1.0f), Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        test.renderer().requestScreenshot(path);
        test.engine->tick();
        const auto png = fs::readBinaryFile(path);
        std::filesystem::remove(path);
        auto image = png ? assets::decodePng(*png) : Result<assets::ImageData>{};
        return image ? *image : assets::ImageData{};
    };
    // Brightest red in the center column: the 1-pixel line may fall on
    // either row around the middle.
    const auto redAtCenter = [](const assets::ImageData& image) {
        int best = 0;
        for (u32 y = image.height / 2 - 2; y <= image.height / 2 + 2; ++y) {
            best = std::max<int>(best, image.pixels[(y * image.width + image.width / 2) * 4]);
        }
        return best;
    };

    const assets::ImageData visible = capture();
    ASSERT_TRUE(visible.valid());
    EXPECT_GT(redAtCenter(visible), 200);

    // A cube in front of the line hides it.
    render::MaterialDesc dark;
    dark.baseColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    dark.specular = 0.0f;
    const ecs::Entity cube = test.scene().createEntity("cube");
    test.world().emplace<render::MeshRenderer>(cube, test.renderer().createMesh(assets::makeCube()),
                                               test.renderer().createMaterial(dark));
    test.engine->tick();  // world transforms
    const assets::ImageData hidden = capture();
    ASSERT_TRUE(hidden.valid());
    EXPECT_LT(redAtCenter(hidden), 20);
    EXPECT_EQ(test.device().validationErrorCount(), 0u);
}

TEST(VulkanHeadless, BloomSpreadsBrightLight) {
    // Red level just outside a small, very bright emissive cube.
    const auto haloAround = [](bool bloom) -> int {
        Config config;
        config.set("rhi.validation", "true");
        config.set("renderer.hdr", "true");
        config.set("renderer.tonemap", "none");
        config.set("renderer.bloom", bloom ? "true" : "false");
        config.set("renderer.bloom_intensity", "0.2");
        log::setLevel(log::Level::Warn);
        TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
        log::setLevel(log::Level::Info);
        if (!test.ready()) {
            return -1;
        }
        test.renderer().setClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
        test.populate(0);  // camera at z = 6, looking down -Z
        const ecs::Entity sun = test.scene().createEntity("sun");
        test.world().emplace<render::DirectionalLight>(sun).intensity = 0.0f;
        test.world().get<render::DirectionalLight>(sun).ambient = Vec3(0.0f);
        render::MaterialDesc blazing;
        blazing.emissive = Vec3(50.0f, 0.0f, 0.0f);
        const ecs::Entity cube = test.scene().createEntity("cube");
        test.world().get<scene::Transform>(cube).scale = Vec3(0.5f);
        test.world().emplace<render::MeshRenderer>(cube, test.renderer().createMesh(assets::makeCube()),
                                                   test.renderer().createMaterial(blazing));

        const std::filesystem::path path = std::filesystem::temp_directory_path() / "bdex_tests_bloom.png";
        test.engine->tick();  // world transforms
        test.renderer().requestScreenshot(path);
        test.engine->tick();
        const auto png = fs::readBinaryFile(path);
        std::filesystem::remove(path);
        const auto image = png ? assets::decodePng(*png) : Result<assets::ImageData>{};
        if (!image || test.device().validationErrorCount() != 0) {
            return -2;
        }
        // The cube spans about +-15 pixels around the center at this size;
        // sample 40 pixels to the right of it, on the background.
        return image->pixels[(image->height / 2 * image->width + image->width / 2 + 40) * 4];
    };

    const int withBloom = haloAround(true);
    if (withBloom == -1) {
        GTEST_SKIP() << "Vulkan is not available here";
    }
    ASSERT_NE(withBloom, -2) << "capture failed or validation errors";
    EXPECT_EQ(haloAround(false), 0);  // plain black background
    EXPECT_GT(withBloom, 20);         // the glow reaches past the edge
}

TEST(VulkanHeadless, FxaaSmoothsEdges) {
    // Pixels partly red along the edges of a tilted, uniformly red square.
    const auto partialPixels = [](std::string_view antialiasing) -> int {
        Config config;
        config.set("rhi.validation", "true");
        config.set("renderer.hdr", "true");
        config.set("renderer.tonemap", "none");
        config.set("renderer.bloom", "false");
        config.set("renderer.antialiasing", antialiasing);
        log::setLevel(log::Level::Warn);
        TestEngine test({"window_null", "rhi_vulkan", "renderer_forward"}, config);
        log::setLevel(log::Level::Info);
        if (!test.ready()) {
            return -1;
        }
        test.renderer().setClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
        test.populate(0);
        const ecs::Entity sun = test.scene().createEntity("sun");  // flat lighting
        test.world().emplace<render::DirectionalLight>(sun).intensity = 0.0f;
        test.world().get<render::DirectionalLight>(sun).ambient = Vec3(1.0f);
        render::MaterialDesc red;
        red.baseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        red.specular = 0.0f;
        const ecs::Entity cube = test.scene().createEntity("cube");
        test.world().get<scene::Transform>(cube).rotation = glm::angleAxis(0.5f, Vec3(0.0f, 0.0f, 1.0f));
        test.world().emplace<render::MeshRenderer>(cube, test.renderer().createMesh(assets::makeCube(2.0f)),
                                                   test.renderer().createMaterial(red));

        const std::filesystem::path path = std::filesystem::temp_directory_path() / "bdex_tests_fxaa.png";
        test.engine->tick();
        test.renderer().requestScreenshot(path);
        test.engine->tick();
        const auto png = fs::readBinaryFile(path);
        std::filesystem::remove(path);
        const auto image = png ? assets::decodePng(*png) : Result<assets::ImageData>{};
        if (!image || test.device().validationErrorCount() != 0) {
            return -2;
        }
        int partial = 0;
        for (usize i = 0; i < image->pixels.size(); i += 4) {
            partial += image->pixels[i] > 25 && image->pixels[i] < 230 ? 1 : 0;
        }
        return partial;
    };

    const int aliased = partialPixels("none");
    if (aliased == -1) {
        GTEST_SKIP() << "Vulkan is not available here";
    }
    const int smoothed = partialPixels("fxaa");
    ASSERT_GE(smoothed, 0) << "capture failed or validation errors";
    EXPECT_GT(smoothed, aliased * 3 + 50) << "aliased " << aliased << ", fxaa " << smoothed;
}
