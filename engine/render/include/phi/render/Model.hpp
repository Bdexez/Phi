#pragma once

#include <phi/assets/Gltf.hpp>
#include <phi/render/Renderer.hpp>
#include <phi/render/ResourceNames.hpp>
#include <phi/scene/Scene.hpp>

#include <string_view>
#include <vector>

namespace phi::render {

/// The GPU side of a ModelData: what uploadModel made, shared by every
/// instance of the model.
struct ModelResources {
    std::vector<std::vector<MeshHandle>> meshes;  ///< Per mesh, per primitive.
    std::vector<MaterialHandle> materials;
    MaterialHandle defaultMaterial;  ///< For primitives without one.
    std::vector<TextureHandle> textures;
};

/// The engine's material closest to a glTF metallic-roughness one. The
/// forward renderer lights with Blinn-Phong, so this approximates: shininess
/// follows roughness (smooth is sharp), specular strength grows with
/// metalness and smoothness, metals darken their diffuse color.
[[nodiscard]] MaterialDesc approximateMaterial(const assets::ModelMaterial& material);

/// Creates the meshes, textures and materials of `model` (once, for any
/// number of instances). Images that did not decode leave their materials
/// untextured. With `names`, the meshes and materials are named after
/// `prefix` for scene files: "<prefix>/<mesh name or index>/<primitive>",
/// "<prefix>/<material name or index>", "<prefix>/default".
[[nodiscard]] ModelResources uploadModel(IRenderer& renderer, const assets::ModelData& model,
                                         ResourceNames* names = nullptr, std::string_view prefix = {});
void releaseModel(IRenderer& renderer, ModelResources& resources);

/// Creates an entity per node of the model's scene under one root entity
/// named `name` (returned), with Name, Transform, Parent and ModelNode (its
/// node index, which animations find nodes by); a node's mesh
/// becomes a MeshRenderer on it, or on one child per primitive when the
/// mesh has several.
ecs::Entity instantiateModel(scene::Scene& scene, const assets::ModelData& model, const ModelResources& resources,
                             std::string_view name = "Model");

}  // namespace phi::render
