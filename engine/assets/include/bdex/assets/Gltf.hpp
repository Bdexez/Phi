#pragma once

#include <bdex/assets/Image.hpp>
#include <bdex/assets/Mesh.hpp>
#include <bdex/core/FileSystem.hpp>
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

enum class AnimatedProperty : u8 { Translation, Rotation, Scale };
enum class Interpolation : u8 { Linear, Step, CubicSpline };

/// One property of one node over time: keyframes at `times` (seconds,
/// ascending). Values are xyz for translation and scale, a quaternion
/// (x, y, z, w) for rotation; with CubicSpline each key has three values:
/// in-tangent, value, out-tangent.
struct AnimationChannel {
    u32 node = 0;
    AnimatedProperty property = AnimatedProperty::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<f32> times;
    std::vector<Vec4> values;
};

struct ModelAnimation {
    std::string name;
    f32 duration = 0.0f;  ///< Its last keyframe.
    std::vector<AnimationChannel> channels;
};

/// A channel's value at `time` (clamped to its keys): xyz for translation
/// and scale; for rotation, a unit quaternion as (x, y, z, w). Linear
/// rotations are slerped, as glTF specifies.
[[nodiscard]] Vec4 sampleChannel(const AnimationChannel& channel, f32 time);

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
    std::vector<ModelAnimation> animations;
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

/// Reads and decodes a .gltf or .glb file of the virtual file system; the
/// files it refers to are read next to it ("app:/ships/hull.bin"), never
/// outside its mount point.
[[nodiscard]] Result<ModelData> loadGltf(const VirtualFileSystem& vfs, std::string_view virtualPath);

}  // namespace bdex::assets
