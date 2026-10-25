#pragma once

#include <bdex/assets/Gltf.hpp>
#include <bdex/core/FileSystem.hpp>
#include <bdex/render/Model.hpp>
#include <bdex/render/ResourceNames.hpp>
#include <bdex/scene/Scene.hpp>

#include <memory>
#include <vector>

namespace sandbox {

/// Lamp posts from a glTF file, each with a warm light hanging in its
/// lantern, which sways if the file animates it. The model is uploaded
/// once however many lamps stand.
class Lamps {
public:
    static constexpr const char* kModel = "app:/models/lamp.gltf";

    /// The two lamps at the back of the scene, their arms reaching inward.
    void placeDefault(const bdex::VirtualFileSystem& vfs, bdex::render::IRenderer& renderer, bdex::scene::Scene& scene,
                      bdex::render::ResourceNames& names);
    /// Loads the model (again) and replaces every lamp with ones standing
    /// where they stood: for when its file changes. A model that does not
    /// load leaves the current ones in place.
    void reload(const bdex::VirtualFileSystem& vfs, bdex::render::IRenderer& renderer, bdex::scene::Scene& scene,
                bdex::render::ResourceNames& names);
    /// Sets the lanterns swaying again after a scene load, which keeps
    /// their nodes but not their players.
    void restartSway(bdex::ecs::World& world) const;
    void release(bdex::render::IRenderer& renderer);

    /// The lamp instances: entities named "Lamp" (duplicates included).
    [[nodiscard]] static std::vector<bdex::ecs::Entity> roots(bdex::ecs::World& world);

private:
    void place(const bdex::VirtualFileSystem& vfs, bdex::render::IRenderer& renderer, bdex::scene::Scene& scene,
               bdex::render::ResourceNames& names, const std::vector<bdex::scene::Transform>& poses);

    bdex::render::ModelResources resources_;
    std::shared_ptr<const bdex::assets::ModelAnimation> sway_;
};

}  // namespace sandbox
