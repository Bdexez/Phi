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
// selected object, F11 for fullscreen, Escape to quit. A gamepad works too: sticks to
// move and look, bumpers up / down, right trigger to go faster.

#include "FlyCamera.hpp"
#include "SkyFeature.hpp"

#include <bdex/app/Application.hpp>
#include <bdex/assets/Audio.hpp>
#include <bdex/audio/Components.hpp>
#include <bdex/core/FileSystem.hpp>
#include <bdex/core/JobSystem.hpp>
#include <bdex/physics/Physics.hpp>
#include <bdex/render/DebugDraw.hpp>
#include <bdex/render/Serialization.hpp>
#include <bdex/scene/Serialization.hpp>
#include <bdex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
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
        setUpSounds();

        engine().scheduler().add(Phase::Update, "sandbox.spin", [this](const FrameTime& time) {
            spinSystem(scene().world(), engine().services().get<JobSystem>(), static_cast<f32>(time.delta));
        });
        return {};
    }

    void onUpdate(const FrameTime& time) override {
        if (const auto* controls = actions(); controls != nullptr && input() != nullptr) {
            flyCamera_.update(scene().world(), *controls, *input(), window(), static_cast<f32>(time.delta));
        }
        updateTitle(time);

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
            if (controls->pressed("select")) {
                selectUnderCursor();
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

    struct Model {
        std::string name;
        render::MeshHandle mesh;
        render::MaterialHandle material;
    };

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
        if (models_.empty()) {
            return;  // no renderer: nothing is drawn, nothing to save
        }
        serializers_.add<render::MeshRenderer>(
            "Model",
            [this](const render::MeshRenderer& renderer, scene::Fields& out, const scene::EntityRefs&) {
                for (const Model& model : models_) {
                    if (model.mesh == renderer.mesh && model.material == renderer.material) {
                        out["name"] = model.name;
                    }
                }
                out["visible"] = scene::formatField(renderer.visible);
            },
            [this](render::MeshRenderer& renderer, const scene::Fields& in, const scene::EntityRefs&) -> Result<> {
                const auto name = in.find("name");
                const auto model = std::find_if(models_.begin(), models_.end(), [&](const Model& candidate) {
                    return name != in.end() && candidate.name == name->second;
                });
                if (model == models_.end()) {
                    return fail("unknown model '{}'", name != in.end() ? name->second : "");
                }
                renderer.mesh = model->mesh;
                renderer.material = model->material;
                return scene::parseField(in, "visible", renderer.visible);
            });
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
        log::info("sandbox", "scene loaded from {} ({} entities)", kSceneFile, world.entityCount());
    }

    /// Throws a bouncing ball from the camera; the oldest goes once there
    /// are too many.
    void throwBall() {
        if (models_.empty()) {
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
        if (auto* mixer = engine().services().tryGet<audio::Mixer>(); mixer != nullptr && throwSound_) {
            mixer->play(throwSound_, {.volume = 0.4f});
        }
        balls_.push_back(ball);
        if (balls_.size() > kMaxBalls) {
            world.destroy(balls_.front());
            balls_.erase(balls_.begin());
        }
    }

    /// Sounds are made here rather than loaded, like the textures: a whoosh
    /// for throws, and a knock for impacts, played where they happen and as
    /// loud as they are hard.
    void setUpSounds() {
        if (!engine().services().has<audio::Mixer>()) {
            return;
        }
        constexpr u32 kRate = 48000;
        const auto shaped = [&](f32 seconds, auto&& sample) {
            auto sound = std::make_shared<assets::AudioData>();
            sound->sampleRate = kRate;
            sound->channels = 1;
            sound->samples.resize(static_cast<usize>(seconds * kRate));
            for (usize i = 0; i < sound->samples.size(); ++i) {
                sound->samples[i] = sample(static_cast<f32>(i) / kRate);
            }
            return sound;
        };
        const f32 tau = 2.0f * std::numbers::pi_v<f32>;
        // A rising chirp, faded in and out.
        throwSound_ = shaped(0.16f, [&](f32 t) {
            const f32 envelope = std::sin(std::numbers::pi_v<f32> * t / 0.16f);
            return 0.3f * envelope * std::sin(tau * (250.0f * t + 1400.0f * t * t));
        });
        // A low tone with a click of noise, dying away quickly.
        u32 noise = 12345;
        impactSound_ = shaped(0.3f, [&](f32 t) {
            noise = noise * 1664525u + 1013904223u;
            const f32 grain = static_cast<f32>(noise >> 8) / static_cast<f32>(1u << 24) * 2.0f - 1.0f;
            return std::exp(-t * 16.0f) * (0.7f * std::sin(tau * 150.0f * t) + 0.3f * grain * std::exp(-t * 60.0f));
        });
        collisions_ = engine().events().subscribe<physics::Collision>([this](const physics::Collision& event) {
            auto* mixer = engine().services().tryGet<audio::Mixer>();
            if (mixer == nullptr || mixer->voiceCount() >= 24) {
                return;  // a pile of balls settling would make a racket
            }
            const physics::Contact& contact = event.contact;
            mixer->play(impactSound_, {.volume = std::clamp(contact.impulse / 6.0f, 0.05f, 1.0f),
                                       // Heavier hits sound a little lower.
                                       .pitch = std::clamp(1.3f - contact.impulse / 30.0f, 0.7f, 1.3f),
                                       .spatial = true,
                                       .position = contact.point,
                                       .minDistance = 3.0f,
                                       .maxDistance = 60.0f});
        });
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
                                     .outerAngle = glm::radians(22.0f)});
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

    /// Gizmos: the sun's direction, point light ranges, the axes of
    /// everything drawn, and physics colliders.
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

        models_ = {{"ground", planeMesh_, groundMaterial_},
                   {"ball", sphereMesh_, ballMaterial_},
                   {"sphere", sphereMesh_, sphereMaterial_},
                   {"glass", cubeMesh_, glassMaterial_},
                   {"moon_0", sphereMesh_, moonMaterials_[0]},
                   {"moon_1", cubeMesh_, moonMaterials_[1]}};
        for (u32 i = 0; i < kCubeCount; ++i) {
            models_.push_back({std::format("cube_{}", i), cubeMesh_, cubeMaterials_[i]});
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

        log::info("sandbox", "scene ready: {} entities", world.entityCount());
    }

    void updateTitle(const FrameTime& time) {
        ++framesSinceTitle_;
        secondsSinceTitle_ += time.delta;
        if (secondsSinceTitle_ < 0.5) {
            return;
        }
        if (platform::IWindow* target = window()) {
            const f64 fps = framesSinceTitle_ / secondsSinceTitle_;
            target->setTitle(std::format("bdex sandbox - {:.0f} fps ({:.2f} ms)", fps, 1000.0 / fps));
        }
        framesSinceTitle_ = 0;
        secondsSinceTitle_ = 0.0;
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
    std::shared_ptr<const assets::AudioData> throwSound_;
    std::shared_ptr<const assets::AudioData> impactSound_;
    Subscription collisions_;
    render::MaterialHandle ballMaterial_;
    scene::ComponentSerializers serializers_;
    std::vector<Model> models_;
    ecs::Entity selected_;
    std::array<render::MaterialHandle, 2> moonMaterials_{};
    std::array<render::MaterialHandle, kCubeCount> cubeMaterials_{};

    ecs::Entity camera_;
    sandbox::FlyCamera flyCamera_;

    u32 framesSinceTitle_ = 0;
    f64 secondsSinceTitle_ = 0.0;
};

BDEX_MAIN(Sandbox)
