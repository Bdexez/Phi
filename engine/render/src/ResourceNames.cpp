#include <phi/render/ResourceNames.hpp>

namespace phi::render {

template <class HandleType>
void ResourceNames::Table<HandleType>::set(HandleType handle, std::string_view name) {
    // One name per handle and one handle per name.
    if (const auto old = byHandle.find(key(handle)); old != byHandle.end()) {
        byName.erase(old->second);
    }
    if (const auto taken = byName.find(std::string(name)); taken != byName.end()) {
        byHandle.erase(key(taken->second));
    }
    byName.insert_or_assign(std::string(name), handle);
    byHandle.insert_or_assign(key(handle), std::string(name));
}

template <class HandleType>
HandleType ResourceNames::Table<HandleType>::find(std::string_view name) const {
    const auto it = byName.find(std::string(name));
    return it == byName.end() ? HandleType{} : it->second;
}

template <class HandleType>
std::string_view ResourceNames::Table<HandleType>::nameOf(HandleType handle) const {
    const auto it = byHandle.find(key(handle));
    return it == byHandle.end() ? std::string_view{} : std::string_view(it->second);
}

void ResourceNames::name(MeshHandle mesh, std::string_view name) { meshes_.set(mesh, name); }
void ResourceNames::name(MaterialHandle material, std::string_view name) { materials_.set(material, name); }
MeshHandle ResourceNames::mesh(std::string_view name) const { return meshes_.find(name); }
MaterialHandle ResourceNames::material(std::string_view name) const { return materials_.find(name); }
std::string_view ResourceNames::nameOf(MeshHandle mesh) const { return meshes_.nameOf(mesh); }
std::string_view ResourceNames::nameOf(MaterialHandle material) const { return materials_.nameOf(material); }

void addMeshRendererSerializer(scene::ComponentSerializers& serializers, const ResourceNames& names) {
    // Saving cannot fail, so an unnamed handle is written as an empty name,
    // which loading then refuses with the entity's context.
    serializers.add<MeshRenderer>(
        "MeshRenderer",
        [&names](const MeshRenderer& renderer, scene::Fields& out, const scene::EntityRefs&) {
            out["mesh"] = std::string(names.nameOf(renderer.mesh));
            out["material"] = std::string(names.nameOf(renderer.material));
            out["visible"] = scene::formatField(renderer.visible);
        },
        [&names](MeshRenderer& renderer, const scene::Fields& in, const scene::EntityRefs&) -> Result<> {
            std::string meshName;
            std::string materialName;
            for (auto parsed : {scene::parseField(in, "mesh", meshName), scene::parseField(in, "material", materialName),
                                scene::parseField(in, "visible", renderer.visible)}) {
                if (!parsed) {
                    return parsed;
                }
            }
            renderer.mesh = names.mesh(meshName);
            renderer.material = names.material(materialName);
            if (!renderer.mesh.valid()) {
                return fail("unknown mesh '{}' (name it with render::ResourceNames)", meshName);
            }
            if (!renderer.material.valid()) {
                return fail("unknown material '{}' (name it with render::ResourceNames)", materialName);
            }
            return {};
        });
}

}  // namespace phi::render
