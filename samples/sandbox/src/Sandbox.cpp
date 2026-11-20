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
#include "Lamps.hpp"
#include "Level.hpp"
#include "Palette.hpp"
#include "SettingsPanel.hpp"
#include "SkyFeature.hpp"
#include "Sounds.hpp"
#include "Sparks.hpp"
#include "Spin.hpp"

#include <bdex/animation/Animation.hpp>
#include <bdex/app/Application.hpp>
#include <bdex/assets/Audio.hpp>
#include <bdex/audio/Components.hpp>
#include <bdex/core/FileSystem.hpp>
#include <bdex/core/FileWatcher.hpp>
#include <bdex/core/JobSystem.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/particles/Particles.hpp>
#include <bdex/physics/Physics.hpp>
#include <bdex/render/DebugDraw.hpp>
#include <bdex/render/Model.hpp>
#include <bdex/render/ResourceNames.hpp>
#include <bdex/render/Serialization.hpp>
#include <bdex/scene/Serialization.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace bdex;


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
            if (const auto sky = renderer().addFeature(std::make_unique<sandbox::SkyFeature>()); !sky) {
                return sky;
            }
            if (const auto built = palette_.create(renderer()); !built) {
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
            sandbox::spinSystem(scene().world(), engine().services().get<JobSystem>(), static_cast<f32>(time.delta));
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
    // --- Scene files ---------------------------------------------------------------
    // Shows how applications extend scene files: Spin is the sandbox's own
    // component, and MeshRenderer handles are saved as model names.

    static constexpr std::string_view kSceneFile = "sandbox_scene.ini";


    void registerSerializers() {
        serializers_ = scene::ComponentSerializers::withSceneComponents();
        render::addComponentSerializers(serializers_);
        physics::addComponentSerializers(serializers_);
        audio::addComponentSerializers(serializers_);
        particles::addComponentSerializers(serializers_);
        animation::addComponentSerializers(serializers_, clips_);  // the lanterns sway on after a load
        serializers_.add<sandbox::Spin>(
            "Spin",
            [](const sandbox::Spin& spin, scene::Fields& out, const scene::EntityRefs&) {
                out["axis"] = scene::formatField(spin.axis);
                out["speed"] = scene::formatField(spin.speed);
            },
            [](sandbox::Spin& spin, const scene::Fields& in, const scene::EntityRefs&) -> Result<> {
                if (auto parsed = scene::parseField(in, "axis", spin.axis); !parsed) {
                    return parsed;
                }
                return scene::parseField(in, "speed", spin.speed);
            });
        if (engine().services().has<render::IRenderer>()) {
            render::addMeshRendererSerializer(serializers_, palette_.names);  // by the names given at creation
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
        log::info("sandbox", "scene loaded from {} ({} entities)", kSceneFile, world.entityCount());
    }

    /// Throws a bouncing ball from the camera; the oldest goes once there
    /// are too many.
    void throwBall() {
        if (!palette_.sphere.valid()) {
            return;  // nothing to draw it with
        }
        ecs::World& world = scene().world();
        const scene::Transform& eye = world.get<scene::Transform>(camera_);
        const ecs::Entity ball = scene().createEntity("Ball");
        scene::Transform& transform = world.get<scene::Transform>(ball);
        transform.position = eye.position + eye.forward() * 1.0f;
        transform.scale = Vec3(0.6f);
        world.emplace<render::MeshRenderer>(ball, palette_.sphere, palette_.ball);
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
        if (!palette_.cube.valid()) {
            return;
        }
        ecs::World& world = scene().world();
        const scene::Transform& eye = world.get<scene::Transform>(camera_);
        const ecs::Entity crate = scene().createEntity("Crate");
        scene::Transform& transform = world.get<scene::Transform>(crate);
        transform.position = eye.position + eye.forward() * 1.2f;
        transform.rotation = eye.rotation;
        transform.scale = Vec3(0.7f);
        world.emplace<render::MeshRenderer>(crate, palette_.cube, palette_.cubes[crates_.size() % kCubeCount]);
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

    void buildScene() {
        scene::Scene& level = scene();
        const sandbox::LevelEntities built = sandbox::buildLevel(level, palette_);
        camera_ = built.camera;
        platform_ = built.platform;
        flyCamera_.attach(level.world(), camera_, 0.0f, -0.26f);
        if (engine().services().has<render::IRenderer>()) {
            lamps_.placeDefault(engine().services().get<VirtualFileSystem>(), renderer(), level, palette_.names, clips_);
        }
        log::info("sandbox", "scene ready: {} entities", level.world().entityCount());
    }

    /// Hot reloading: the lamp model when its file changes, shaders when
    /// they are rebuilt (no need for F7).
    void setUpHotReload() {
        const auto& vfs = engine().services().get<VirtualFileSystem>();
        for (const char* path : {sandbox::Lamps::kModel, "engine:/shaders", "app:/shaders"}) {
            if (const auto resolved = vfs.resolve(path)) {
                watcher_.watch(*resolved);
            }
        }
        if (const auto lamp = vfs.resolve(sandbox::Lamps::kModel)) {
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
                lamps_.reload(engine().services().get<VirtualFileSystem>(), renderer(), scene(), palette_.names, clips_);
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
            slab->position = sandbox::kPlatformBase + Vec3(0.0f, 0.0f, 3.0f * std::sin(phase));
        }
    }

    void onStop() override {
        if (engine().services().has<render::IRenderer>()) {
            lamps_.release(renderer());
        }
    }

    static constexpr u32 kCubeCount = sandbox::Palette::kCubeCount;

    sandbox::Palette palette_;
    sandbox::Lamps lamps_;
    animation::ClipNames clips_;  ///< Before the serializers that refer to it.
    bool debugView_ = false;
    static constexpr usize kMaxBalls = 40;
    std::vector<ecs::Entity> balls_;
    std::vector<ecs::Entity> crates_;
    ecs::Entity platform_;
    f64 platformTime_ = 0.0;  ///< Paused with the platform.
    bool platformMoving_ = true;
    FileWatcher watcher_;
    std::filesystem::path lampFile_;
    f64 secondsSinceWatch_ = 0.0;
    scene::ComponentSerializers serializers_;
    ecs::Entity selected_;

    ecs::Entity camera_;
    sandbox::FlyCamera flyCamera_;

    sandbox::Hud hud_;
    sandbox::Sounds sounds_;
    sandbox::Sparks sparks_;
    sandbox::SettingsPanel panel_;
};

BDEX_MAIN(Sandbox)
