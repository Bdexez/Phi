#pragma once

#include <bdex/assets/Image.hpp>
#include <bdex/assets/Mesh.hpp>
#include <bdex/core/Math.hpp>
#include <bdex/core/Result.hpp>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bdex::assets {

/// A glTF metallic-roughness material, as authored.
struct ModelMaterial {
    std::string name;
    Vec4 baseColor{1.0f};
    i32 baseColorImage = -1;  ///< Index into ModelData::images, or -1.
    i32 normalImage = -1;
    f32 normalScale = 1.0f;
    f32 metallic = 1.0f;
    f32 roughness = 1.0f;
    Vec3 emissive{0.0f};
    bool blend = false;  ///< alphaMode BLEND: drawn transparent.
};

struct ModelPrimitive {
    MeshData mesh;
    i32 material = -1;  ///< Index into ModelData::materials, or -1 for a default one.
};

struct ModelMesh {
    std::string name;
    std::vector<ModelPrimitive> primitives;
};

struct ModelNode {
    std::string name;
    Vec3 translation{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f};
    i32 mesh = -1;  ///< Index into ModelData::meshes, or -1.
    std::vector<u32> children;
};

/// A glTF scene's content, renderer-agnostic: a node tree whose nodes may
/// draw meshes made of primitives, each with a material.
struct ModelData {
    std::vector<ModelNode> nodes;
    std::vector<u32> roots;  ///< Nodes of the default scene.
    std::vector<ModelMesh> meshes;
    std::vector<ModelMaterial> materials;
    /// Decoded images; an image the engine cannot decode (JPEG...) is left
    /// empty (not valid()) and named in `warnings`.
    std::vector<ImageData> images;
    std::vector<std::string> warnings;
};

/// Reads a file a glTF refers to by URI (a .bin buffer, a .png image).
using UriReader = std::function<Result<std::vector<u8>>(std::string_view uri)>;

/// Decodes glTF 2.0, as JSON (.gltf) or binary (.glb): nodes with their
/// transforms, triangle meshes (positions, normals, UVs, tangents, any
/// index or component type, normalized or not, interleaved or not),
/// metallic-roughness materials and PNG images. Buffers and images may be
/// embedded (GLB chunk, base64 data URI) or external, read through
/// `readUri`. Everything is bounds-checked: a malformed file is an error,
/// never a crash. Unsupported features (other primitive modes, sparse
/// accessors, skins, animations) are skipped or reported.
[[nodiscard]] Result<ModelData> decodeGltf(std::span<const u8> bytes, const UriReader& readUri = {});

}  // namespace bdex::assets
