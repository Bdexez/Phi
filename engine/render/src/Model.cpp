#include <phi/render/Model.hpp>

#include <phi/render/Components.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace phi::render {

MaterialDesc approximateMaterial(const assets::ModelMaterial& material) {
    MaterialDesc desc;
    const f32 metallic = std::clamp(material.metallic, 0.0f, 1.0f);
    const f32 roughness = std::clamp(material.roughness, 0.0f, 1.0f);
    // Metals reflect their color rather than scattering it.
    desc.baseColor = Vec4(Vec3(material.baseColor) * (1.0f - 0.6f * metallic), material.baseColor.a);
    // A Blinn-Phong exponent from the GGX width alpha = roughness^2.
    const f32 alpha = std::max(roughness * roughness, 0.06f);
    desc.shininess = std::clamp(2.0f / (alpha * alpha) - 2.0f, 2.0f, 512.0f);
    desc.specular = (0.15f + 0.75f * metallic) * (1.0f - 0.8f * roughness);
    desc.emissive = material.emissive;
    desc.normalStrength = material.normalScale;
    desc.transparent = material.blend;
    return desc;
}

namespace {

/// A model's name for its index-th item: its own name if no other item has
/// it, else the name and the index ("box#2"), or the index alone.
template <class Items>
std::string itemName(const Items& items, usize index) {
    const std::string& name = items[index].name;
    if (name.empty()) {
        return std::to_string(index);
    }
    const auto same = std::count_if(items.begin(), items.end(), [&](const auto& item) { return item.name == name; });
    return same > 1 ? std::format("{}#{}", name, index) : name;
}

}  // namespace

ModelResources uploadModel(IRenderer& renderer, const assets::ModelData& model, ResourceNames* names,
                           std::string_view prefix) {
    ModelResources resources;
    // One texture per image and color space it is used in.
    std::vector<std::pair<i32, TextureColorSpace>> made;
    const auto textureFor = [&](i32 image, TextureColorSpace space) -> TextureHandle {
        if (image < 0 || static_cast<usize>(image) >= model.images.size() || !model.images[static_cast<usize>(image)].valid()) {
            return {};
        }
        for (usize i = 0; i < made.size(); ++i) {
            if (made[i] == std::pair(image, space)) {
                return resources.textures[i];
            }
        }
        made.emplace_back(image, space);
        resources.textures.push_back(renderer.createTexture(model.images[static_cast<usize>(image)], space));
        return resources.textures.back();
    };
    for (usize index = 0; index < model.materials.size(); ++index) {
        const assets::ModelMaterial& material = model.materials[index];
        MaterialDesc desc = approximateMaterial(material);
        desc.albedo = textureFor(material.baseColorImage, TextureColorSpace::Srgb);
        desc.normalMap = textureFor(material.normalImage, TextureColorSpace::Linear);
        resources.materials.push_back(renderer.createMaterial(desc));
        if (names != nullptr) {
            names->name(resources.materials.back(), std::format("{}/{}", prefix, itemName(model.materials, index)));
        }
    }
    resources.defaultMaterial = renderer.createMaterial(MaterialDesc{});
    if (names != nullptr) {
        names->name(resources.defaultMaterial, std::format("{}/default", prefix));
    }
    for (usize index = 0; index < model.meshes.size(); ++index) {
        const assets::ModelMesh& mesh = model.meshes[index];
        std::vector<MeshHandle>& parts = resources.meshes.emplace_back();
        for (usize part = 0; part < mesh.primitives.size(); ++part) {
            parts.push_back(renderer.createMesh(mesh.primitives[part].mesh));
            if (names != nullptr) {
                names->name(parts.back(), std::format("{}/{}/{}", prefix, itemName(model.meshes, index), part));
            }
        }
    }
    return resources;
}

void releaseModel(IRenderer& renderer, ModelResources& resources) {
    for (const std::vector<MeshHandle>& parts : resources.meshes) {
        for (const MeshHandle mesh : parts) {
            renderer.destroyMesh(mesh);
        }
    }
    for (const MaterialHandle material : resources.materials) {
        renderer.destroyMaterial(material);
    }
    renderer.destroyMaterial(resources.defaultMaterial);
    for (const TextureHandle texture : resources.textures) {
        renderer.destroyTexture(texture);
    }
    resources = {};
}

ecs::Entity instantiateModel(scene::Scene& scene, const assets::ModelData& model, const ModelResources& resources,
                             std::string_view name) {
    ecs::World& world = scene.world();
    const ecs::Entity root = scene.createEntity(name);
    const auto materialOf = [&](i32 index) {
        return index >= 0 && static_cast<usize>(index) < resources.materials.size()
                   ? resources.materials[static_cast<usize>(index)]
                   : resources.defaultMaterial;
    };

    // Depth-first from the scene's roots. ModelData from decodeGltf is a
    // forest; the visited marks keep hand-made ones from looping.
    std::vector<u8> visited(model.nodes.size(), 0);
    std::vector<std::pair<u32, ecs::Entity>> pending;
    for (auto it = model.roots.rbegin(); it != model.roots.rend(); ++it) {
        pending.emplace_back(*it, root);
    }
    while (!pending.empty()) {
        const auto [index, parent] = pending.back();
        pending.pop_back();
        if (index >= model.nodes.size() || visited[index] != 0) {
            continue;
        }
        visited[index] = 1;
        const assets::ModelNode& node = model.nodes[index];
        const ecs::Entity entity =
            scene.createEntity(node.name.empty() ? std::format("node {}", index) : node.name);
        scene::Transform& transform = world.get<scene::Transform>(entity);
        transform.position = node.translation;
        transform.rotation = node.rotation;
        transform.scale = node.scale;
        world.emplace<scene::Parent>(entity, parent);
        world.emplace<scene::ModelNode>(entity, index);

        if (node.mesh >= 0 && static_cast<usize>(node.mesh) < resources.meshes.size()) {
            const std::vector<MeshHandle>& parts = resources.meshes[static_cast<usize>(node.mesh)];
            const std::vector<assets::ModelPrimitive>& primitives = model.meshes[static_cast<usize>(node.mesh)].primitives;
            for (usize part = 0; part < parts.size() && part < primitives.size(); ++part) {
                ecs::Entity holder = entity;
                if (parts.size() > 1) {
                    holder = scene.createEntity(std::format("{} part {}", world.get<scene::Name>(entity).value, part));
                    world.emplace<scene::Parent>(holder, entity);
                }
                world.emplace<MeshRenderer>(holder, parts[part], materialOf(primitives[part].material));
            }
        }
        for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
            pending.emplace_back(*child, entity);
        }
    }
    return root;
}

}  // namespace phi::render
