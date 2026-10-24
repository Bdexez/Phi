// Sandbox: a small scene exercising every layer of the engine.
//
//   - modules chosen at runtime (see sandbox.ini)
//   - ECS components and a system defined by the application (Spin)
//   - transform hierarchy (moons orbiting a pivot, carrying point lights)
//   - meshes, textures and materials (one transparent) created through the
//     renderer service
//   - a custom render feature (SkyFeature) plugged into the renderer
//
// Controls (rebindable in the [input] section of sandbox.ini): hold the
// right mouse button to look around, WASD or the arrows to move, Space /
// Left Ctrl to go up / down, Left Shift to move faster, F2 for a screenshot,
// F3 to log a frame profile, F4 for debug gizmos, left click to select an
// object, F5 / F9 to save / load the scene, F6 for a flashlight, F7 to reload
// shaders, F8 to throw a ball, F10 / Delete to duplicate / delete the
// selected object, F11 for fullscreen, F12 for the stats panel, F1 for this
// list on screen, Tab for the settings panel, G to walk
// (Space jumps) or fly, C to throw a crate, Escape to quit. A gamepad works too: sticks to
// move and look, bumpers up / down, right trigger to go faster.

#include "FlyCamera.hpp"
#include "Hud.hpp"
#include "SkyFeature.hpp"
#include "Sounds.hpp"
#include "SettingsPanel.hpp"
#include "Sparks.hpp"

#include <bdex/animation/Animation.hpp>
#include <bdex/app/Application.hpp>
#include <bdex/assets/Audio.hpp>
#include <bdex/audio/Components.hpp>
#include <bdex/core/FileSystem.hpp>
#include <bdex/core/FileWatcher.hpp>
#include <bdex/core/JobSystem.hpp>
#include <bdex/physics/Physics.hpp>
#include <bdex/render/DebugDraw.hpp>
#include <bdex/render/Model.hpp>
#include <bdex/render/ResourceNames.hpp>
#include <bdex/render/Serialization.hpp>
#include <bdex/scene/Serialization.hpp>
#include <bdex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <filesystem>
#include <format>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace bdex;

namespace {

/// Application-defined component: rotates the entity around an axis.
struct Spin {
    Vec3 axis{0.0f, 1.0f, 0.0f};
    f32 speed = 1.0f;  // radians per second
};

/// Application-defined system. Systems are plain functions over ECS views;
/// this one only touches each entity's own components, so it can run on the
/// job system's threads (a handful of entities stay on the calling thread).
void spinSystem(ecs::World& world, JobSystem& jobs, f32 deltaSeconds) {
    world.view<scene::Transform, const Spin>().parallelEach(
        jobs, [deltaSeconds](scene::Transform& transform, const Spin& spin) {
            transform.rotation =
                glm::normalize(glm::angleAxis(spin.speed * deltaSeconds, spin.axis) * transform.rotation);
        });
}

Vec3 hsvToRgb(f32 hue, f32 saturation, f32 value) {
    const auto channel = [&](f32 offset) {
        const f32 k = std::fmod(offset + hue * 6.0f, 6.0f);
        return value - value * saturation * std::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f);
    };
    return {channel(5.0f), channel(3.0f), channel(1.0f)};
}

}  // namespace

class Sandbox final : public app::Application {
protected:
    void onConfigure(Config& config) override {
        config.set("window.title", "bdex sandbox");

        // Default bindings; an [input] section in sandbox.ini overrides them.
        config.setDefault("input.move_forward", "W, Up, LeftStickUp");
        config.setDefault("input.move_back", "S, Down, LeftStickDown");
        config.setDefault("input.move_left", "A, Left, LeftStickLeft");
        config.setDefault("input.move_right", "D, Right, LeftStickRight");
        config.setDefault("input.move_up", "Space, GamepadRightBumper");
        config.setDefault("input.move_down", "LeftControl, GamepadLeftBumper");
        config.setDefault("input.move_fast", "LeftShift, RightTrigger");
        config.setDefault("input.look", "MouseRight");
        config.setDefault("input.look_up", "RightStickUp");
        config.setDefault("input.look_down", "RightStickDown");
        config.setDefault("input.look_left", "RightStickLeft");
        config.setDefault("input.look_right", "RightStickRight");
        config.setDefault("input.screenshot", "F2, GamepadY");
        config.setDefault("input.profile", "F3");
        config.setDefault("input.debug_view", "F4");
        config.setDefault("input.select", "MouseLeft");
        config.setDefault("input.save_scene", "F5");
        config.setDefault("input.load_scene", "F9");
        config.setDefault("input.toggle_fullscreen", "F11");
        config.setDefault("input.flashlight", "F6, GamepadX");
        config.setDefault("input.reload_shaders", "F7");
        config.setDefault("input.throw_ball", "F8, GamepadA");
        config.setDefault("input.duplicate", "F10");
        config.setDefault("input.delete", "Delete");
        config.setDefault("input.stats", "F12");
        config.setDefault("input.help", "F1");
        config.setDefault("input.panel", "Tab");
        config.setDefault("input.walk", "G, GamepadB");
        config.setDefault("input.throw_crate", "C");
        config.setDefault("input.quit", "Escape, GamepadBack");
    }

    Result<> onStart() override {
        // Without a renderer module the sandbox still runs its simulation.
        if (engine().services().has<render::IRenderer>()) {
            if (const auto built = buildRenderResources(); !built) {
                return built;
            }
        } else {
            log::warn("sandbox", "no renderer module loaded: simulation only");
        }
        buildScene();
        registerSerializers();
        setUpHotReload();
        sounds_.start(engine());
        sparks_.start(engine());

        engine().scheduler().add(Phase::Update, "sandbox.spin", [this](const FrameTime& time) {
            spinSystem(scene().world(), engine().services().get<JobSystem>(), static_cast<f32>(time.delta));
        });
        return {};
    }

    void onUpdate(const FrameTime& time) override {
        if (const auto* controls = actions(); controls != nullptr && input() != nullptr) {
            // Real time: the camera flies on through slow motion or a pause.
            flyCamera_.update(scene().world(), *controls, *input(), window(), static_cast<f32>(time.unscaledDelta));
        }
        hud_.update(engine(), time, scene().world().entityCount(), actions(), window());
        sparks_.update(scene().world());
        const sandbox::PanelRequests requests = panel_.draw(engine(), scene().world(), input(),
                                                            {platformMoving_, sparks_.enabled, debugView_});
        if (requests.throwCrate) {
            throwCrate();
        }
        if (requests.throwBall) {
            throwBall();
        }
        if (requests.clearThrows) {
            clearThrows();
        }
        pollHotReload(time);

        if (const platform::InputActions* controls = actions(); controls != nullptr) {
            if (controls->pressed("quit")) {
                engine().requestExit();
            }
            if (controls->pressed("screenshot") && engine().services().has<render::IRenderer>()) {
                renderer().requestScreenshot(std::format("screenshot_{}.png", time.frame));
            }
            if (controls->pressed("profile")) {
                logProfile();
            }
            if (controls->pressed("debug_view")) {
                debugView_ = !debugView_;
            }
            if (controls->pressed("stats")) {
                hud_.toggleStats();
            }
            if (controls->pressed("help")) {
                hud_.toggleHelp();
            }
            if (controls->pressed("panel")) {
                panel_.toggle();
            }
            if (controls->pressed("walk")) {
                flyCamera_.setWalking(scene().world(), !flyCamera_.walking());
                log::info("sandbox", "{}", flyCamera_.walking() ? "walking (G to fly)" : "flying (G to walk)");
            }
            if (controls->pressed("select") && !panel_.wantsPointer()) {
                selectUnderCursor();  // unless the click was on the panel
            }
            if (controls->pressed("save_scene")) {
                saveScene();
            }
            if (controls->pressed("load_scene")) {
                loadScene();
            }
            if (controls->pressed("flashlight")) {
                toggleFlashlight();
            }
            if (controls->pressed("throw_ball")) {
                throwBall();
            }
            if (controls->pressed("throw_crate")) {
                throwCrate();
            }
            if (controls->pressed("duplicate")) {
                duplicateSelection();
            }
            if (controls->pressed("delete") && scene().world().alive(selected_)) {
                scene::destroyHierarchy(scene().world(), selected_);
                selected_ = {};
            }
            if (controls->pressed("reload_shaders") && engine().services().has<render::IRenderer>()) {
                if (const auto reloaded = renderer().reloadShaders(); !reloaded) {
                    log::error("sandbox", "{}", reloaded.error().message);
                }
            }
            if (platform::IWindow* target = window(); target != nullptr && controls->pressed("toggle_fullscreen")) {
                target->setFullscreen(!target->fullscreen());
            }
        }
        if (debugView_) {
            drawDebugView();
        }
        highlightSelection();
    }

private:
    /// Normal map of `cells` x `cells` square tiles whose edges slope down
    /// over `bevel` texels.
    static assets::ImageData beveledTiles(u32 size, u32 cells, f32 bevel, f32 strength) {
        const f32 cell = static_cast<f32>(size) / static_cast<f32>(cells);
        std::vector<f32> heights(static_cast<usize>(size) * size);
        for (u32 y = 0; y < size; ++y) {
            for (u32 x = 0; x < size; ++x) {
                const f32 u = std::fmod(static_cast<f32>(x) + 0.5f, cell);
                const f32 v = std::fmod(static_cast<f32>(y) + 0.5f, cell);
                const f32 edge = std::min({u, v, cell - u, cell - v});
                heights[static_cast<usize>(y) * size + x] = std::min(edge, bevel);
            }
        }
        return assets::makeNormalMap(heights, size, size, strength / bevel);
    }

    // --- Scene files ---------------------------------------------------------------
    // Shows how applications extend scene files: Spin is the sandbox's own
    // component, and MeshRenderer handles are saved as model names.

    static constexpr std::string_view kSceneFile = "sandbox_scene.ini";


    void registerSerializers() {
        serializers_ = scene::ComponentSerializers::withSceneComponents();
        render::addComponentSerializers(serializers_);
        physics::addComponentSerializers(serializers_);
        audio::addComponentSerializers(serializers_);
        serializers_.add<Spin>(
            "Spin",
            [](const Spin& spin, scene::Fields& out, const scene::EntityRefs&) {
                out["axis"] = scene::formatField(spin.axis);
                out["speed"] = scene::formatField(spin.speed);
            },
            [](Spin& spin, const scene::Fields& in, const scene::EntityRefs&) -> Result<> {
                if (auto parsed = scene::parseField(in, "axis", spin.axis); !parsed) {
                    return parsed;
                }
                return scene::parseField(in, "speed", spin.speed);
            });
        if (engine().services().has<render::IRenderer>()) {
            render::addMeshRendererSerializer(serializers_, names_);  // by the names given at creation
        }
    }

    void saveScene() {
        const std::string text = serializers_.save(scene().world());
        if (const auto written = fs::writeBinaryFile(std::filesystem::path(kSceneFile), text.data(), text.size());
            !written) {
            log::error("sandbox", "{}", written.error().message);
            return;
        }
        log::info("sandbox", "scene saved to {} ({} entities)", kSceneFile, scene().world().entityCount());
    }

    void loadScene() {
        const auto text = fs::readTextFile(std::filesystem::path(kSceneFile));
        if (!text) {
            log::warn("sandbox", "{}", text.error().message);
            return;
        }
        // Validate on a scratch world first: a broken file must not leave
        // the sandbox empty.
        ecs::World scratch;
        if (const auto checked = serializers_.load(scratch, *text); !checked) {
            log::error("sandbox", "cannot load {}: {}", kSceneFile, checked.error().message);
            return;
        }
        ecs::World& world = scene().world();
        world.clear();
        if (const auto loaded = serializers_.load(world, *text); !loaded) {
            // Validated above: only a bug could get here.
            log::error("sandbox", "reloading {} failed: {}", kSceneFile, loaded.error().message);
        }
        camera_ = scene().find("Camera");
        selected_ = {};
        flyCamera_.adoptFacing(world, camera_);
        platform_ = scene().find("Platform");
        // Players are not saved; the lamps' nodes are, so they sway again.
        if (lampSway_ != nullptr) {
            for (const ecs::Entity lamp : lampRoots()) {
                world.emplace<animation::AnimationPlayer>(lamp, lampSway_);
            }
        }
        // A body saved while walking would stand there, invisible.
        if (const ecs::Entity player = scene().find("Player"); world.alive(player)) {
            world.destroy(player);
        }
        log::info("sandbox", "scene loaded from {} ({} entities)", kSceneFile, world.entityCount());
    }

    /// Throws a bouncing ball from the camera; the oldest goes once there
    /// are too many.
    void throwBall() {
        if (!sphereMesh_.valid()) {
            return;  // nothing to draw it with
        }
        ecs::World& world = scene().world();
        const scene::Transform& eye = world.get<scene::Transform>(camera_);
        const ecs::Entity ball = scene().createEntity("Ball");
        scene::Transform& transform = world.get<scene::Transform>(ball);
        transform.position = eye.position + eye.forward() * 1.0f;
        transform.scale = Vec3(0.6f);
        world.emplace<render::MeshRenderer>(ball, sphereMesh_, ballMaterial_);
        world.emplace<physics::SphereCollider>(ball, 0.5f);
        world.emplace<physics::RigidBody>(
            ball, physics::RigidBody{.velocity = eye.forward() * 12.0f + Vec3(0.0f, 2.0f, 0.0f),
                                     .restitution = 0.7f,
                                     .angularDamping = 1.5f});  // rolls to a stop in a few seconds
        sounds_.playThrow(engine());
        balls_.push_back(ball);
        if (balls_.size() > kMaxBalls) {
            world.destroy(balls_.front());
            balls_.erase(balls_.begin());
        }
    }

    /// Throws a tumbling crate from the camera; the oldest goes once there
    /// are too many.
    void clearThrows() {
        for (const ecs::Entity thrown : balls_) {
            scene().world().destroy(thrown);
        }
        for (const ecs::Entity thrown : crates_) {
            scene().world().destroy(thrown);
        }
        balls_.clear();
        crates_.clear();
    }

    void throwCrate() {
        if (!cubeMesh_.valid()) {
            return;
        }
        ecs::World& world = scene().world();
        const scene::Transform& eye = world.get<scene::Transform>(camera_);
        const ecs::Entity crate = scene().createEntity("Crate");
        scene::Transform& transform = world.get<scene::Transform>(crate);
        transform.position = eye.position + eye.forward() * 1.2f;
        transform.rotation = eye.rotation;
        transform.scale = Vec3(0.7f);
        world.emplace<render::MeshRenderer>(crate, cubeMesh_, cubeMaterials_[crates_.size() % kCubeCount]);
        world.emplace<physics::BoxCollider>(crate, Vec3(0.5f));
        world.emplace<physics::RigidBody>(
            crate, physics::RigidBody{.velocity = eye.forward() * 8.0f + Vec3(0.0f, 2.0f, 0.0f),
                                      .angularVelocity = eye.right() * -4.0f,  // end over end
                                      .restitution = 0.2f,
                                      .friction = 0.7f});
        crates_.push_back(crate);
        if (crates_.size() > kMaxBalls) {
            world.destroy(crates_.front());
            crates_.erase(crates_.begin());
        }
    }

    /// A spot light carried by the camera, switched on and off.
    /// Copies the selected object and everything attached to it, a little
    /// above the original, and selects the copy.
    void duplicateSelection() {
        ecs::World& world = scene().world();
        if (!world.alive(selected_) || selected_ == camera_) {
            return;
        }
        const ecs::Entity copy = scene::cloneHierarchy(world, selected_);
        if (auto* transform = world.tryGet<scene::Transform>(copy)) {
            transform->position.y += 1.5f;
        }
        selected_ = copy;
    }

    void toggleFlashlight() {
        ecs::World& world = scene().world();
        if (const ecs::Entity torch = scene().find("Flashlight"); torch.valid()) {
            world.destroy(torch);
            return;
        }
        const ecs::Entity torch = scene().createEntity("Flashlight");
        world.emplace<scene::Parent>(torch, camera_);
        world.get<scene::Transform>(torch).position = Vec3(0.3f, -0.2f, 0.0f);  // held low and right
        world.emplace<render::SpotLight>(
            torch, render::SpotLight{.color = Vec3(1.0f, 0.95f, 0.8f),
                                     .intensity = 30.0f,
                                     .range = 25.0f,
                                     .innerAngle = glm::radians(12.0f),
                                     .outerAngle = glm::radians(22.0f),
                                     .castShadows = true});
    }

    /// Picks the nearest drawn object under the cursor (bounding spheres).
    void selectUnderCursor() {
        const platform::InputState* pointer = input();
        if (pointer == nullptr || !engine().services().has<render::IRenderer>()) {
            return;
        }
        ecs::World& world = scene().world();
        const Extent2D extent = renderer().renderExtent();
        const render::CameraData view = render::makeCameraData(
            world.get<scene::WorldTransform>(camera_).matrix, world.get<render::Camera>(camera_),
            static_cast<f32>(extent.width) / static_cast<f32>(std::max(extent.height, 1u)));
        // Cursor coordinates are taken as framebuffer pixels, which holds
        // unless the display scales the window (HiDPI).
        const render::Ray ray = render::rayThroughPixel(view.viewProjection, pointer->mousePosition(), extent);

        selected_ = {};
        f32 nearest = std::numeric_limits<f32>::max();
        world.view<const render::MeshRenderer, const scene::WorldTransform>().each(
            [&](ecs::Entity entity, const render::MeshRenderer& mesh, const scene::WorldTransform& transform) {
                const auto bounds = renderer().meshBounds(mesh.mesh);
                if (!bounds || !mesh.visible) {
                    return;
                }
                const auto hit = render::intersect(ray, render::transformSphere(*bounds, transform.matrix));
                if (!hit) {
                    return;
                }
                // Bounds around the camera (the ground) say nothing about what
                // is under the cursor: they only win when nothing else is hit.
                const f32 rank = *hit > 0.0f ? *hit : std::numeric_limits<f32>::max() * 0.5f;
                if (rank < nearest) {
                    nearest = rank;
                    selected_ = entity;
                }
            });
        if (const auto* name = world.tryGet<scene::Name>(selected_)) {
            log::info("sandbox", "selected '{}'", name->value);
        }
    }

    /// Circles the selected object.
    void highlightSelection() {
        auto* debug = engine().services().tryGet<render::DebugDraw>();
        ecs::World& world = scene().world();
        const auto* mesh = world.tryGet<render::MeshRenderer>(selected_);
        if (debug == nullptr || mesh == nullptr) {
            return;
        }
        if (const auto bounds = renderer().meshBounds(mesh->mesh)) {
            const render::BoundingSphere around =
                render::transformSphere(*bounds, world.get<scene::WorldTransform>(selected_).matrix);
            debug->sphere(around.center, around.radius, Vec4(1.0f, 0.85f, 0.1f, 1.0f), 48);
        }
    }

    /// Gizmos: the sun's direction, point light ranges, the axes and names
    /// of everything drawn, and physics colliders.
    void drawDebugView() {
        auto* debug = engine().services().tryGet<render::DebugDraw>();
        if (debug == nullptr) {
            return;
        }
        ecs::World& world = scene().world();
        world.view<const render::DirectionalLight>().each([&](const render::DirectionalLight& sun) {
            const Vec3 from(0.0f, 6.0f, 0.0f);
            debug->line(from, from + glm::normalize(sun.direction) * 3.0f, Vec4(1.0f, 0.9f, 0.2f, 1.0f));
        });
        world.view<const render::PointLight, const scene::WorldTransform>().each(
            [&](const render::PointLight& light, const scene::WorldTransform& transform) {
                debug->sphere(Vec3(transform.matrix[3]), light.range, Vec4(light.color, 0.6f), 48);
            });
        world.view<const render::MeshRenderer, const scene::WorldTransform>().each(
            [&](const render::MeshRenderer&, const scene::WorldTransform& transform) {
                debug->axes(transform.matrix, 0.75f);
            });
        // Names above what is drawn, facing the camera.
        if (world.has<scene::Transform>(camera_)) {
            const scene::Transform& eye = world.get<scene::Transform>(camera_);
            const Vec3 right = eye.right();
            const Vec3 up = eye.rotation * Vec3(0.0f, 1.0f, 0.0f);
            world.view<const scene::Name, const render::MeshRenderer, const scene::WorldTransform>().each(
                [&](ecs::Entity, const scene::Name& name, const render::MeshRenderer& mesh,
                    const scene::WorldTransform& transform) {
                    f32 top = 0.6f;
                    Vec3 center(transform.matrix[3]);
                    if (const auto bounds = renderer().meshBounds(mesh.mesh)) {
                        const render::BoundingSphere around = render::transformSphere(*bounds, transform.matrix);
                        center = around.center;
                        top = around.radius + 0.15f;
                    }
                    constexpr f32 kHeight = 0.22f;
                    const f32 width = render::DebugDraw::textWidth(name.value, kHeight);
                    // Dark ink reads on both the pale sky and the checkered floor.
                    debug->text(center + Vec3(0.0f, top, 0.0f) - right * (width * 0.5f), name.value, kHeight,
                                Vec4(0.08f, 0.08f, 0.16f, 1.0f), right, up);
                });
        }
        // Colliders as physics sees them (spheres scaled by the largest
        // axis); triggers in purple.
        const auto colorOf = [&](ecs::Entity entity) {
            return world.has<physics::Trigger>(entity) ? Vec4(0.8f, 0.3f, 1.0f, 1.0f)
                   : world.has<physics::RigidBody>(entity) ? Vec4(1.0f, 0.6f, 0.1f, 1.0f)
                                                            : Vec4(0.3f, 1.0f, 0.4f, 1.0f);
        };
        world.view<const scene::Transform, const physics::BoxCollider>().each(
            [&](ecs::Entity entity, const scene::Transform& transform, const physics::BoxCollider& box) {
                debug->box(transform.localMatrix(), box.halfExtents, colorOf(entity));
            });
        world.view<const scene::Transform, const physics::SphereCollider>().each(
            [&](ecs::Entity entity, const scene::Transform& transform, const physics::SphereCollider& sphere) {
                const Vec3 scale = glm::abs(transform.scale);
                debug->sphere(transform.position, sphere.radius * std::max({scale.x, scale.y, scale.z}),
                              colorOf(entity), 24);
            });
    }

    /// Logs the CPU time of every engine task since the previous profile.
    void logProfile() {
        Scheduler& scheduler = engine().scheduler();
        std::vector<TaskStats> stats = scheduler.taskStats();
        std::ranges::sort(stats, [](const TaskStats& a, const TaskStats& b) {
            return a.averageMs * static_cast<f64>(a.runs) > b.averageMs * static_cast<f64>(b.runs);
        });
        log::info("profile", "{:<32} {:>12} {:>10} {:>8}", "task", "phase", "avg ms", "runs");
        for (const TaskStats& task : stats) {
            log::info("profile", "{:<32} {:>12} {:>10.3f} {:>8}", task.name, phaseName(task.phase),
                      task.averageMs, task.runs);
        }
        if (engine().services().has<render::IRenderer>()) {
            const rhi::FrameStats frame = engine().services().get<rhi::IDevice>().lastFrameStats();
            log::info("profile",
                      "last frame: {} draws ({} instances), {} triangles, {} pipeline / {} bind group / "
                      "{} buffer binds",
                      frame.drawCalls, frame.instances, frame.triangles, frame.pipelineBinds,
                      frame.bindGroupBinds, frame.bufferBinds);
            for (const rhi::GpuTiming& timing : engine().services().get<rhi::IDevice>().lastGpuTimings()) {
                log::info("profile", "gpu {:<28} {:>10.3f}", std::string(timing.depth * 2, ' ') + timing.name,
                          timing.milliseconds);
            }
        }
        scheduler.resetStats();
    }

    Result<> buildRenderResources() {
        render::IRenderer& gfx = renderer();

        if (const auto added = gfx.addFeature(std::make_unique<sandbox::SkyFeature>()); !added) {
            return added;
        }

        cubeMesh_ = gfx.createMesh(assets::makeCube(1.0f));
        sphereMesh_ = gfx.createMesh(assets::makeSphere(0.5f, 48, 24));
        planeMesh_ = gfx.createMesh(assets::makePlane(40.0f, 20.0f));

        const render::TextureHandle checker = gfx.createTexture(
            assets::makeCheckerboard(256, 8, {235, 235, 235, 255}, {120, 124, 130, 255}));
        const render::TextureHandle crate = gfx.createTexture(
            assets::makeCheckerboard(64, 4, {255, 255, 255, 255}, {200, 200, 200, 255}));

        // Beveled tiles, matching the checker cells, give the floor and the
        // crates some relief through normal maps.
        const render::TextureHandle tiles = gfx.createTexture(beveledTiles(256, 8, 6.0f, 3.0f),
                                                              render::TextureColorSpace::Linear);
        const render::TextureHandle panels = gfx.createTexture(beveledTiles(64, 4, 3.0f, 2.0f),
                                                               render::TextureColorSpace::Linear);

        render::MaterialDesc ground;
        ground.baseColor = Vec4(0.55f, 0.6f, 0.55f, 1.0f);
        ground.albedo = checker;
        ground.normalMap = tiles;
        ground.specular = 0.05f;
        groundMaterial_ = gfx.createMaterial(ground);

        render::MaterialDesc polished;  // no texture: plain base color
        polished.baseColor = Vec4(0.95f, 0.95f, 1.0f, 1.0f);
        polished.specular = 0.9f;
        polished.shininess = 96.0f;
        sphereMaterial_ = gfx.createMaterial(polished);

        // The moons glow with the color of the lamp they carry.
        for (u32 i = 0; i < 2; ++i) {
            render::MaterialDesc moon;
            moon.baseColor = Vec4(0.05f, 0.05f, 0.05f, 1.0f);
            moon.emissive = i == 0 ? Vec3(1.0f, 0.35f, 0.05f) : Vec3(0.05f, 0.4f, 1.0f);
            moonMaterials_[i] = gfx.createMaterial(moon);
        }

        render::MaterialDesc rubber;
        rubber.baseColor = Vec4(0.95f, 0.3f, 0.2f, 1.0f);
        rubber.specular = 0.5f;
        ballMaterial_ = gfx.createMaterial(rubber);

        render::MaterialDesc glass;  // tinted and see-through
        glass.baseColor = Vec4(0.3f, 0.7f, 1.0f, 0.45f);
        glass.specular = 1.0f;
        glass.shininess = 128.0f;
        glass.transparent = true;
        glassMaterial_ = gfx.createMaterial(glass);

        for (u32 i = 0; i < kCubeCount; ++i) {
            render::MaterialDesc painted;
            painted.baseColor = Vec4(hsvToRgb(static_cast<f32>(i) / kCubeCount, 0.65f, 0.95f), 1.0f);
            painted.albedo = crate;
            painted.normalMap = panels;
            painted.specular = 0.4f;
            cubeMaterials_[i] = gfx.createMaterial(painted);
        }

        // Names for scene files.
        names_.name(planeMesh_, "plane");
        names_.name(cubeMesh_, "cube");
        names_.name(sphereMesh_, "sphere");
        names_.name(groundMaterial_, "ground");
        names_.name(ballMaterial_, "ball");
        names_.name(sphereMaterial_, "sphere");
        names_.name(glassMaterial_, "glass");
        names_.name(moonMaterials_[0], "moon 0");
        names_.name(moonMaterials_[1], "moon 1");
        for (u32 i = 0; i < kCubeCount; ++i) {
            names_.name(cubeMaterials_[i], std::format("cube {}", i));
        }
        return {};
    }

    void buildScene() {
        scene::Scene& level = scene();
        ecs::World& world = level.world();
        const f32 pi = std::numbers::pi_v<f32>;

        // Sun
        const ecs::Entity sun = level.createEntity("Sun");
        world.emplace<render::DirectionalLight>(
            sun, render::DirectionalLight{.direction = Vec3(-0.45f, -0.8f, -0.35f),
                                          .color = Vec3(1.0f, 0.96f, 0.9f),
                                          .intensity = 1.1f,
                                          .ambient = Vec3(0.16f, 0.19f, 0.25f)});

        // Camera
        camera_ = level.createEntity("Camera");
        world.emplace<render::Camera>(camera_);
        world.emplace<audio::AudioListener>(camera_);  // the ears go where the eyes are
        world.get<scene::Transform>(camera_).position = Vec3(0.0f, 3.2f, 9.5f);
        flyCamera_.attach(world, camera_, 0.0f, -0.26f);

        // Ground
        const ecs::Entity ground = level.createEntity("Ground");
        world.emplace<render::MeshRenderer>(ground, planeMesh_, groundMaterial_);
        world.emplace<physics::BoxCollider>(ground, Vec3(20.0f, 0.01f, 20.0f));

        // A platform gliding back and forth beside the scene (moved in
        // onFixedUpdate): throw things onto it, or walk onto it (G).
        platform_ = level.createEntity("Platform");
        scene::Transform& slab = world.get<scene::Transform>(platform_);
        slab.position = kPlatformBase;
        slab.scale = Vec3(2.4f, 0.3f, 2.4f);
        world.emplace<render::MeshRenderer>(platform_, cubeMesh_, groundMaterial_);
        world.emplace<physics::BoxCollider>(platform_, Vec3(0.5f));
        world.emplace<physics::Kinematic>(platform_);

        // Centerpiece
        const ecs::Entity sphere = level.createEntity("Sphere");
        world.get<scene::Transform>(sphere).position = Vec3(0.0f, 1.4f, 0.0f);
        world.get<scene::Transform>(sphere).scale = Vec3(2.2f);
        world.emplace<render::MeshRenderer>(sphere, sphereMesh_, sphereMaterial_);
        world.emplace<physics::SphereCollider>(sphere, 0.5f);  // static: no RigidBody

        // A slowly turning glass block in front of the sphere
        const ecs::Entity glassBlock = level.createEntity("Glass block");
        scene::Transform& glassTransform = world.get<scene::Transform>(glassBlock);
        glassTransform.position = Vec3(0.0f, 1.6f, 2.4f);
        glassTransform.scale = Vec3(1.6f, 1.6f, 0.4f);
        world.emplace<render::MeshRenderer>(glassBlock, cubeMesh_, glassMaterial_);
        world.emplace<Spin>(glassBlock, Vec3(0.0f, 1.0f, 0.0f), 0.35f);

        // A ring of spinning cubes
        for (u32 i = 0; i < kCubeCount; ++i) {
            const f32 angle = 2.0f * pi * static_cast<f32>(i) / kCubeCount;
            const ecs::Entity cube = level.createEntity(std::format("Cube {}", i));
            scene::Transform& transform = world.get<scene::Transform>(cube);
            transform.position = Vec3(std::cos(angle) * 4.5f, 0.75f, std::sin(angle) * 4.5f);
            transform.rotation = glm::angleAxis(angle, Vec3(0.0f, 1.0f, 0.0f));
            world.emplace<render::MeshRenderer>(cube, cubeMesh_, cubeMaterials_[i]);
            world.emplace<physics::BoxCollider>(cube);  // axis-aligned, ignores the spin
            world.emplace<Spin>(cube, glm::normalize(Vec3(0.3f, 1.0f, 0.2f * static_cast<f32>(i))),
                                0.6f + 0.15f * static_cast<f32>(i));
        }

        // Hierarchy: an invisible pivot spins above the sphere and carries
        // two moons. Only the pivot is animated; the moons follow through
        // their Parent link.
        const ecs::Entity pivot = level.createEntity("Moon pivot");
        world.get<scene::Transform>(pivot).position = Vec3(0.0f, 3.4f, 0.0f);
        world.emplace<Spin>(pivot, Vec3(0.0f, 1.0f, 0.0f), 0.9f);
        for (u32 i = 0; i < 2; ++i) {
            const ecs::Entity moon = level.createEntity(std::format("Moon {}", i));
            scene::Transform& transform = world.get<scene::Transform>(moon);
            transform.position = Vec3(i == 0 ? 2.0f : -2.0f, 0.0f, 0.0f);
            transform.scale = Vec3(0.45f);
            world.emplace<scene::Parent>(moon, pivot);
            world.emplace<render::MeshRenderer>(moon, i == 0 ? sphereMesh_ : cubeMesh_,
                                                moonMaterials_[i]);
            world.emplace<Spin>(moon, Vec3(1.0f, 0.0f, 0.0f), 2.0f);

            // Each moon carries a colored lamp, slightly below it, that
            // sweeps over the sphere and the cubes as the pivot turns.
            const ecs::Entity lamp = level.createEntity(std::format("Moon lamp {}", i));
            world.get<scene::Transform>(lamp).position = Vec3(i == 0 ? 2.0f : -2.0f, -1.0f, 0.0f);
            world.emplace<scene::Parent>(lamp, pivot);
            world.emplace<render::PointLight>(
                lamp, render::PointLight{.color = i == 0 ? Vec3(1.0f, 0.55f, 0.2f) : Vec3(0.2f, 0.6f, 1.0f),
                                         .intensity = 6.0f,
                                         .range = 7.0f});
        }

        placeLamps();
        log::info("sandbox", "scene ready: {} entities", world.entityCount());
    }

    /// Two lamp posts from a glTF file, each with a warm light in its
    /// lantern. The model is uploaded once and placed twice.
    /// Two lamp posts from a glTF file, each with a warm light in its
    /// lantern. The model is uploaded once and placed twice.
    void placeLamps() {
        std::vector<scene::Transform> poses;
        for (const f32 side : {-1.0f, 1.0f}) {
            scene::Transform pose;
            pose.position = Vec3(side * 4.5f, 0.0f, -2.5f);
            // The arm reaches toward the middle of the scene.
            pose.rotation = glm::angleAxis(side < 0.0f ? 0.0f : std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f));
            poses.push_back(pose);
        }
        placeLamps(poses);
    }

    /// Loads the lamp model (again) and places it at `poses`. A model that
    /// does not load leaves the current one in place.
    void placeLamps(const std::vector<scene::Transform>& poses) {
        if (!engine().services().has<render::IRenderer>()) {
            return;
        }
        const auto model = assets::loadGltf(engine().services().get<VirtualFileSystem>(), kLampModel);
        if (!model) {
            log::warn("sandbox", "{}", model.error().message);
            return;
        }
        for (const std::string& warning : model->warnings) {
            log::warn("sandbox", "{}", warning);
        }
        ecs::World& world = scene().world();
        // Replacing: the old instances and their GPU side go first.
        for (const ecs::Entity lamp : lampRoots()) {
            scene::destroyHierarchy(world, lamp);
        }
        render::releaseModel(renderer(), lampResources_);
        // Named "lamp/..." for scene files.
        lampResources_ = render::uploadModel(renderer(), *model, &names_, "lamp");
        // Its lantern sways, if the file says so.
        lampSway_ = model->animations.empty() ? nullptr
                                              : std::make_shared<assets::ModelAnimation>(model->animations.front());
        u32 lanternNode = 0;
        for (u32 node = 0; node < model->nodes.size(); ++node) {
            if (model->nodes[node].name == "Lantern") {
                lanternNode = node;
            }
        }
        for (const scene::Transform& pose : poses) {
            const ecs::Entity lamp = render::instantiateModel(scene(), *model, lampResources_, "Lamp");
            scene::Transform& transform = world.get<scene::Transform>(lamp);
            transform.position = pose.position;
            transform.rotation = pose.rotation;
            transform.scale = pose.scale;
            if (lampSway_ != nullptr) {
                world.emplace<animation::AnimationPlayer>(lamp, lampSway_);
            }
            // The light hangs in the lantern, swaying with it.
            ecs::Entity lantern = lamp;
            for (const ecs::Entity part : scene::branch(world, lamp)) {
                if (const auto* node = world.tryGet<scene::ModelNode>(part); node != nullptr && node->index == lanternNode) {
                    lantern = part;
                }
            }
            const ecs::Entity glow = scene().createEntity("Lamp light");
            world.emplace<scene::Parent>(glow, lantern);
            world.emplace<render::PointLight>(
                glow, render::PointLight{.color = Vec3(1.0f, 0.75f, 0.4f), .intensity = 4.0f, .range = 6.0f});
        }
    }

    /// The lamp instances: entities named "Lamp" (duplicates included).
    [[nodiscard]] std::vector<ecs::Entity> lampRoots() {
        std::vector<ecs::Entity> roots;
        scene().world().view<const scene::Name>().each([&](ecs::Entity entity, const scene::Name& name) {
            if (name.value == "Lamp") {
                roots.push_back(entity);
            }
        });
        return roots;
    }

    /// Hot reloading: the lamp model when its file changes, shaders when
    /// they are rebuilt (no need for F7).
    void setUpHotReload() {
        const auto& vfs = engine().services().get<VirtualFileSystem>();
        for (const char* path : {kLampModel, "engine:/shaders", "app:/shaders"}) {
            if (const auto resolved = vfs.resolve(path)) {
                watcher_.watch(*resolved);
            }
        }
        if (const auto lamp = vfs.resolve(kLampModel)) {
            lampFile_ = *lamp;
        }
    }

    void pollHotReload(const FrameTime& time) {
        secondsSinceWatch_ += time.unscaledDelta;
        if (secondsSinceWatch_ < 0.5 || !engine().services().has<render::IRenderer>()) {
            return;
        }
        secondsSinceWatch_ = 0.0;
        bool shaders = false;
        for (const std::filesystem::path& changed : watcher_.poll()) {
            if (changed == lampFile_) {
                log::info("sandbox", "{} changed: reloading the lamps", changed.filename().string());
                std::vector<scene::Transform> poses;
                for (const ecs::Entity lamp : lampRoots()) {
                    poses.push_back(scene().world().get<scene::Transform>(lamp));
                }
                placeLamps(poses);
            } else if (changed.extension() == ".spv") {
                shaders = true;
            }
        }
        if (shaders) {
            log::info("sandbox", "shaders changed: reloading them");
            if (const auto reloaded = renderer().reloadShaders(); !reloaded) {
                log::error("sandbox", "{}", reloaded.error().message);
            }
        }
    }

    void onFixedUpdate(const FrameTime& time) override {
        // Ten seconds there and back, easing at the ends.
        if (!platformMoving_) {
            return;
        }
        if (auto* slab = scene().world().tryGet<scene::Transform>(platform_)) {
            platformTime_ += time.delta;
            const f32 phase = static_cast<f32>(platformTime_) * 2.0f * std::numbers::pi_v<f32> / 10.0f;
            slab->position = kPlatformBase + Vec3(0.0f, 0.0f, 3.0f * std::sin(phase));
        }
    }

    void onStop() override {
        if (engine().services().has<render::IRenderer>()) {
            render::releaseModel(renderer(), lampResources_);
        }
    }

    static constexpr u32 kCubeCount = 8;

    render::MeshHandle cubeMesh_;
    render::MeshHandle sphereMesh_;
    render::MeshHandle planeMesh_;
    render::MaterialHandle groundMaterial_;
    render::MaterialHandle sphereMaterial_;
    render::MaterialHandle glassMaterial_;
    bool debugView_ = false;
    static constexpr usize kMaxBalls = 40;
    std::vector<ecs::Entity> balls_;
    std::vector<ecs::Entity> crates_;
    render::ModelResources lampResources_;
    ecs::Entity platform_;
    f64 platformTime_ = 0.0;  ///< Paused with the platform.
    bool platformMoving_ = true;
    static constexpr Vec3 kPlatformBase{-7.5f, 0.16f, 1.0f};
    std::shared_ptr<const assets::ModelAnimation> lampSway_;
    static constexpr const char* kLampModel = "app:/models/lamp.gltf";
    FileWatcher watcher_;
    std::filesystem::path lampFile_;
    f64 secondsSinceWatch_ = 0.0;
    render::MaterialHandle ballMaterial_;
    scene::ComponentSerializers serializers_;
    render::ResourceNames names_;
    ecs::Entity selected_;
    std::array<render::MaterialHandle, 2> moonMaterials_{};
    std::array<render::MaterialHandle, kCubeCount> cubeMaterials_{};

    ecs::Entity camera_;
    sandbox::FlyCamera flyCamera_;

    sandbox::Hud hud_;
    sandbox::Sounds sounds_;
    sandbox::Sparks sparks_;
    sandbox::SettingsPanel panel_;
};

BDEX_MAIN(Sandbox)
