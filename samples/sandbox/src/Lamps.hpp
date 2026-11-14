#pragma once

#include <phi/animation/Animation.hpp>
#include <phi/assets/Gltf.hpp>
#include <phi/core/FileSystem.hpp>
#include <phi/render/Model.hpp>
#include <phi/render/ResourceNames.hpp>
#include <phi/scene/Scene.hpp>

#include <memory>
#include <vector>

namespace sandbox {

/// Lamp posts from a glTF file, each with a warm light hanging in its
/// lantern, which sways if the file animates it. The model is uploaded
/// once however many lamps stand; its sway is named "lamp/sway" for scene
/// files.
class Lamps {
public:
    static constexpr const char* kModel = "app:/models/lamp.gltf";

    /// The two lamps at the back of the scene, their arms reaching inward.
    void placeDefault(const phi::VirtualFileSystem& vfs, phi::render::IRenderer& renderer, phi::scene::Scene& scene,
                      phi::render::ResourceNames& names, phi::animation::ClipNames& clips);
    /// Loads the model (again) and replaces every lamp with ones standing
    /// where they stood: for when its file changes. A model that does not
    /// load leaves the current ones in place.
    void reload(const phi::VirtualFileSystem& vfs, phi::render::IRenderer& renderer, phi::scene::Scene& scene,
                phi::render::ResourceNames& names, phi::animation::ClipNames& clips);
    void release(phi::render::IRenderer& renderer);

    /// The lamp instances: entities named "Lamp" (duplicates included).
    [[nodiscard]] static std::vector<phi::ecs::Entity> roots(const phi::ecs::World& world);

private:
    void place(const phi::VirtualFileSystem& vfs, phi::render::IRenderer& renderer, phi::scene::Scene& scene,
               phi::render::ResourceNames& names, phi::animation::ClipNames& clips,
               const std::vector<phi::scene::Transform>& poses);

    phi::render::ModelResources resources_;
    std::shared_ptr<const phi::assets::ModelAnimation> sway_;
};

}  // namespace sandbox
