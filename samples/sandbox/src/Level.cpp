#include "Level.hpp"

#include "Spin.hpp"

#include <phi/audio/Components.hpp>
#include <phi/physics/Physics.hpp>
#include <phi/render/Renderer.hpp>

#include <cmath>
#include <format>
#include <numbers>

namespace sandbox {

using namespace phi;

LevelEntities buildLevel(scene::Scene& level, const Palette& palette) {
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
    LevelEntities built;
    built.camera = level.createEntity("Camera");
    world.emplace<render::Camera>(built.camera);
    world.emplace<audio::AudioListener>(built.camera);  // the ears go where the eyes are
    world.get<scene::Transform>(built.camera).position = Vec3(0.0f, 3.2f, 9.5f);

    // Ground
    const ecs::Entity ground = level.createEntity("Ground");
    world.emplace<render::MeshRenderer>(ground, palette.plane, palette.ground);
    world.emplace<physics::BoxCollider>(ground, Vec3(20.0f, 0.01f, 20.0f));

    // A platform gliding back and forth beside the scene (the sandbox
    // moves it in onFixedUpdate): throw things onto it, or walk onto it (G).
    built.platform = level.createEntity("Platform");
    scene::Transform& slab = world.get<scene::Transform>(built.platform);
    slab.position = kPlatformBase;
    slab.scale = Vec3(2.4f, 0.3f, 2.4f);
    world.emplace<render::MeshRenderer>(built.platform, palette.cube, palette.ground);
    world.emplace<physics::BoxCollider>(built.platform, Vec3(0.5f));
    world.emplace<physics::Kinematic>(built.platform);

    // Centerpiece
    const ecs::Entity sphere = level.createEntity("Sphere");
    world.get<scene::Transform>(sphere).position = Vec3(0.0f, 1.4f, 0.0f);
    world.get<scene::Transform>(sphere).scale = Vec3(2.2f);
    world.emplace<render::MeshRenderer>(sphere, palette.sphere, palette.polished);
    world.emplace<physics::SphereCollider>(sphere, 0.5f);  // static: no RigidBody

    // A slowly turning glass block in front of the sphere
    const ecs::Entity glassBlock = level.createEntity("Glass block");
    scene::Transform& glassTransform = world.get<scene::Transform>(glassBlock);
    glassTransform.position = Vec3(0.0f, 1.6f, 2.4f);
    glassTransform.scale = Vec3(1.6f, 1.6f, 0.4f);
    world.emplace<render::MeshRenderer>(glassBlock, palette.cube, palette.glass);
    world.emplace<Spin>(glassBlock, Vec3(0.0f, 1.0f, 0.0f), 0.35f);

    // A ring of spinning cubes
    for (u32 i = 0; i < Palette::kCubeCount; ++i) {
        const f32 angle = 2.0f * pi * static_cast<f32>(i) / Palette::kCubeCount;
        const ecs::Entity cube = level.createEntity(std::format("Cube {}", i));
        scene::Transform& transform = world.get<scene::Transform>(cube);
        transform.position = Vec3(std::cos(angle) * 4.5f, 0.75f, std::sin(angle) * 4.5f);
        transform.rotation = glm::angleAxis(angle, Vec3(0.0f, 1.0f, 0.0f));
        world.emplace<render::MeshRenderer>(cube, palette.cube, palette.cubes[i]);
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
        world.emplace<render::MeshRenderer>(moon, i == 0 ? palette.sphere : palette.cube,
                                            palette.moons[i]);
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
    return built;
}

}  // namespace sandbox
