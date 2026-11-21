#pragma once

#include <phi/render/Components.hpp>
#include <phi/scene/Serialization.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace phi::render {

/// Names for meshes and materials, so scene files can refer to them: a GPU
/// handle means nothing once the program exits, a name ("lamp/glass")
/// does. An application names what it creates; uploadModel can name a
/// model's resources.
class ResourceNames {
public:
    /// Names a mesh or material, replacing any name it or another had.
    void name(MeshHandle mesh, std::string_view name);
    void name(MaterialHandle material, std::string_view name);

    /// Invalid handles for unknown names.
    [[nodiscard]] MeshHandle mesh(std::string_view name) const;
    [[nodiscard]] MaterialHandle material(std::string_view name) const;
    /// Empty for unnamed handles.
    [[nodiscard]] std::string_view nameOf(MeshHandle mesh) const;
    [[nodiscard]] std::string_view nameOf(MaterialHandle material) const;

private:
    template <class HandleType>
    struct Table {
        std::unordered_map<std::string, HandleType> byName;
        std::unordered_map<u64, std::string> byHandle;

        static u64 key(HandleType handle) { return static_cast<u64>(handle.index) << 32 | handle.generation; }
        void set(HandleType handle, std::string_view name);
        [[nodiscard]] HandleType find(std::string_view name) const;
        [[nodiscard]] std::string_view nameOf(HandleType handle) const;
    };
    Table<MeshHandle> meshes_;
    Table<MaterialHandle> materials_;
};

/// Registers MeshRenderer with scene files, saved as the names of its mesh
/// and material ("MeshRenderer.mesh = lamp/pole"). Saving a renderer whose
/// mesh or material has no name, or loading a name `names` does not know,
/// is an error naming it. `names` must outlive the serializers.
void addMeshRendererSerializer(scene::ComponentSerializers& serializers, const ResourceNames& names);

}  // namespace phi::render
