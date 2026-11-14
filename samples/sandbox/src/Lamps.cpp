#include "Lamps.hpp"

#include <phi/animation/Animation.hpp>
#include <phi/core/Log.hpp>

#include <numbers>

namespace sandbox {

using namespace phi;

void Lamps::placeDefault(const VirtualFileSystem& vfs, render::IRenderer& renderer, scene::Scene& scene,
                         render::ResourceNames& names, animation::ClipNames& clips) {
    std::vector<scene::Transform> poses;
    for (const f32 side : {-1.0f, 1.0f}) {
        scene::Transform pose;
        pose.position = Vec3(side * 4.5f, 0.0f, -2.5f);
        // The arm reaches toward the middle of the scene.
        pose.rotation = glm::angleAxis(side < 0.0f ? 0.0f : std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f));
        poses.push_back(pose);
    }
    place(vfs, renderer, scene, names, clips, poses);
}

void Lamps::reload(const VirtualFileSystem& vfs, render::IRenderer& renderer, scene::Scene& scene,
                   render::ResourceNames& names, animation::ClipNames& clips) {
    std::vector<scene::Transform> poses;
    for (const ecs::Entity lamp : roots(scene.world())) {
        poses.push_back(scene.world().get<scene::Transform>(lamp));
    }
    place(vfs, renderer, scene, names, clips, poses);
}

void Lamps::place(const VirtualFileSystem& vfs, render::IRenderer& renderer, scene::Scene& scene,
                  render::ResourceNames& names, animation::ClipNames& clips,
                  const std::vector<scene::Transform>& poses) {
    const auto model = assets::loadGltf(vfs, kModel);
    if (!model) {
        log::warn("sandbox", "{}", model.error().message);
        return;
    }
    for (const std::string& warning : model->warnings) {
        log::warn("sandbox", "{}", warning);
    }
    ecs::World& world = scene.world();
    // Replacing: the old instances and their GPU side go first.
    for (const ecs::Entity lamp : roots(world)) {
        scene::destroyHierarchy(world, lamp);
    }
    render::releaseModel(renderer, resources_);
    // Named "lamp/..." for scene files.
    resources_ = render::uploadModel(renderer, *model, &names, "lamp");
    // Its lantern sways, if the file says so.
    sway_ = model->animations.empty() ? nullptr : std::make_shared<assets::ModelAnimation>(model->animations.front());
    if (sway_ != nullptr) {
        clips.name(sway_, "lamp/sway");  // so scene files can bring the players back
    }
    u32 lanternNode = 0;
    for (u32 node = 0; node < model->nodes.size(); ++node) {
        if (model->nodes[node].name == "Lantern") {
            lanternNode = node;
        }
    }
    for (const scene::Transform& pose : poses) {
        const ecs::Entity lamp = render::instantiateModel(scene, *model, resources_, "Lamp");
        scene::Transform& transform = world.get<scene::Transform>(lamp);
        transform.position = pose.position;
        transform.rotation = pose.rotation;
        transform.scale = pose.scale;
        if (sway_ != nullptr) {
            world.emplace<animation::AnimationPlayer>(lamp, sway_);
        }
        // The light hangs in the lantern, swaying with it.
        ecs::Entity lantern = lamp;
        for (const ecs::Entity part : scene::branch(world, lamp)) {
            if (const auto* node = world.tryGet<scene::ModelNode>(part); node != nullptr && node->index == lanternNode) {
                lantern = part;
            }
        }
        const ecs::Entity glow = scene.createEntity("Lamp light");
        world.emplace<scene::Parent>(glow, lantern);
        world.emplace<render::PointLight>(
            glow, render::PointLight{.color = Vec3(1.0f, 0.75f, 0.4f), .intensity = 4.0f, .range = 6.0f});
    }
}

void Lamps::release(render::IRenderer& renderer) {
    render::releaseModel(renderer, resources_);
    sway_ = nullptr;
}

std::vector<ecs::Entity> Lamps::roots(const ecs::World& world) {
    std::vector<ecs::Entity> found;
    world.view<const scene::Name>().each([&](ecs::Entity entity, const scene::Name& name) {
        if (name.value == "Lamp") {
            found.push_back(entity);
        }
    });
    return found;
}

}  // namespace sandbox
