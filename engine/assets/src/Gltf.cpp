#include <phi/assets/Gltf.hpp>

#include <phi/core/Json.hpp>

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>

namespace phi::assets {

namespace {

constexpr u32 kGlbMagic = 0x46546C67;  // "glTF"
constexpr u32 kChunkJson = 0x4E4F534A;
constexpr u32 kChunkBinary = 0x004E4942;

u32 readU32(std::span<const u8> bytes, usize offset) {
    return static_cast<u32>(bytes[offset]) | static_cast<u32>(bytes[offset + 1]) << 8 |
           static_cast<u32>(bytes[offset + 2]) << 16 | static_cast<u32>(bytes[offset + 3]) << 24;
}

/// A non-negative integer JSON number (indices, sizes, offsets), if it is one.
std::optional<u64> integer(const json::Value& value) {
    if (!value.isNumber()) {
        return std::nullopt;
    }
    const f64 number = value.asNumber();
    if (!std::isfinite(number) || number < 0.0 || number > 9007199254740992.0 || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<u64>(number);
}

Result<std::vector<u8>> decodeBase64(std::string_view text) {
    const auto digit = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::vector<u8> out;
    out.reserve(text.size() / 4 * 3);
    u32 bits = 0;
    int count = 0;
    for (const char c : text) {
        if (c == '=') {
            break;
        }
        const int value = digit(c);
        if (value < 0) {
            return fail("gltf: bad base64 data");
        }
        bits = (bits << 6) | static_cast<u32>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(static_cast<u8>(bits >> count));
        }
    }
    return out;
}

/// The bytes of a URI: embedded as base64, or read through the caller.
Result<std::vector<u8>> loadUri(std::string_view uri, const UriReader& readUri) {
    if (uri.starts_with("data:")) {
        const usize comma = uri.find(',');
        if (comma == std::string_view::npos || uri.substr(0, comma).find(";base64") == std::string_view::npos) {
            return fail("gltf: only base64 data URIs are supported");
        }
        return decodeBase64(uri.substr(comma + 1));
    }
    if (!readUri) {
        return fail("gltf: '{}' is an external file and no reader was given", uri);
    }
    return readUri(uri);
}

class Decoder {
public:
    Decoder(const json::Value& root, std::vector<std::vector<u8>> buffers)
        : root_(root), buffers_(std::move(buffers)) {}

    /// The bytes of a buffer view.
    Result<std::span<const u8>> view(u64 index, u64* stride = nullptr) const {
        const json::Value& view = root_["bufferViews"][static_cast<usize>(index)];
        const auto buffer = integer(view["buffer"]);
        const auto length = integer(view["byteLength"]);
        const u64 offset = integer(view["byteOffset"]).value_or(0);
        if (!buffer || !length || *buffer >= buffers_.size()) {
            return fail("gltf: buffer view {} is malformed", index);
        }
        const std::vector<u8>& bytes = buffers_[static_cast<usize>(*buffer)];
        if (offset > bytes.size() || *length > bytes.size() - offset) {
            return fail("gltf: buffer view {} lies outside its buffer", index);
        }
        if (stride != nullptr) {
            *stride = integer(view["byteStride"]).value_or(0);
        }
        return std::span<const u8>(bytes).subspan(static_cast<usize>(offset), static_cast<usize>(*length));
    }

    /// An accessor's elements as floats, `components` per element (2, 3
    /// or 4 floats; scalars for indices are read by indices()).
    Result<std::vector<f32>> floats(u64 index, u32 components) const {
        const json::Value& accessor = root_["accessors"][static_cast<usize>(index)];
        auto layout = this->layout(accessor, index);
        if (!layout) {
            return std::unexpected(layout.error());
        }
        if (layout->components != components) {
            return fail("gltf: accessor {} has {} components, {} expected", index, layout->components, components);
        }
        const bool normalized = accessor["normalized"].asBool(false);
        std::vector<f32> values(static_cast<usize>(layout->count) * components);
        for (u64 element = 0; element < layout->count; ++element) {
            const u8* at = layout->bytes.data() + element * layout->stride;
            for (u32 c = 0; c < components; ++c) {
                values[static_cast<usize>(element * components + c)] =
                    component(at + c * layout->componentSize, layout->type, normalized);
            }
        }
        return values;
    }

    Result<std::vector<u32>> indices(u64 index, usize vertexCount) const {
        const json::Value& accessor = root_["accessors"][static_cast<usize>(index)];
        auto layout = this->layout(accessor, index);
        if (!layout) {
            return std::unexpected(layout.error());
        }
        if (layout->components != 1 || (layout->type != 5121 && layout->type != 5123 && layout->type != 5125)) {
            return fail("gltf: index accessor {} is not unsigned scalars", index);
        }
        std::vector<u32> values(static_cast<usize>(layout->count));
        for (u64 element = 0; element < layout->count; ++element) {
            const u8* at = layout->bytes.data() + element * layout->stride;
            u32 value = at[0];
            if (layout->type == 5123) value = static_cast<u32>(at[0] | at[1] << 8);
            if (layout->type == 5125) value = readU32(std::span<const u8>(at, 4), 0);
            if (value >= vertexCount) {
                return fail("gltf: index {} past the {} vertices", value, vertexCount);
            }
            values[static_cast<usize>(element)] = value;
        }
        return values;
    }

private:
    struct Layout {
        std::span<const u8> bytes;  ///< From the first element.
        u64 count = 0;
        u64 stride = 0;
        u32 components = 0;
        u32 componentSize = 0;
        u32 type = 0;
    };

    Result<Layout> layout(const json::Value& accessor, u64 index) const {
        if (!accessor.isObject()) {
            return fail("gltf: accessor {} does not exist", index);
        }
        if (accessor.find("sparse") != nullptr) {
            return fail("gltf: accessor {} is sparse, which is not supported", index);
        }
        Layout layout;
        const auto count = integer(accessor["count"]);
        const auto type = integer(accessor["componentType"]);
        const std::string_view shape = accessor["type"].asString();
        if (!count || !type) {
            return fail("gltf: accessor {} is malformed", index);
        }
        layout.count = *count;
        layout.type = static_cast<u32>(*type);
        layout.components = shape == "SCALAR" ? 1 : shape == "VEC2" ? 2 : shape == "VEC3" ? 3 : shape == "VEC4" ? 4 : 0;
        switch (layout.type) {
            case 5120: case 5121: layout.componentSize = 1; break;
            case 5122: case 5123: layout.componentSize = 2; break;
            case 5125: case 5126: layout.componentSize = 4; break;
            default: layout.componentSize = 0;
        }
        if (layout.components == 0 || layout.componentSize == 0) {
            return fail("gltf: accessor {} has an unsupported type", index);
        }
        const u64 elementSize = static_cast<u64>(layout.components) * layout.componentSize;
        const auto viewIndex = integer(accessor["bufferView"]);
        if (!viewIndex) {
            return fail("gltf: accessor {} has no buffer view", index);  // all zeros: not supported
        }
        u64 stride = 0;
        auto bytes = view(*viewIndex, &stride);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        layout.stride = stride != 0 ? stride : elementSize;
        if (layout.stride < elementSize) {
            return fail("gltf: accessor {} overlaps itself", index);
        }
        const u64 offset = integer(accessor["byteOffset"]).value_or(0);
        // The last element must end inside the view; checked without
        // overflowing on hostile counts.
        if (layout.count > 0) {
            if (offset > bytes->size() || layout.count - 1 > (bytes->size() - offset) / layout.stride ||
                (layout.count - 1) * layout.stride + elementSize > bytes->size() - offset) {
                return fail("gltf: accessor {} runs past its buffer view", index);
            }
            layout.bytes = bytes->subspan(static_cast<usize>(offset));
        }
        return layout;
    }

    static f32 component(const u8* at, u32 type, bool normalized) {
        switch (type) {
            case 5126: {
                f32 value = 0.0f;
                std::memcpy(&value, at, sizeof(value));
                return std::isfinite(value) ? value : 0.0f;
            }
            case 5120: {
                const auto value = static_cast<i8>(at[0]);
                return normalized ? std::max(static_cast<f32>(value) / 127.0f, -1.0f) : static_cast<f32>(value);
            }
            case 5121: return normalized ? static_cast<f32>(at[0]) / 255.0f : static_cast<f32>(at[0]);
            case 5122: {
                const auto value = static_cast<i16>(at[0] | at[1] << 8);
                return normalized ? std::max(static_cast<f32>(value) / 32767.0f, -1.0f) : static_cast<f32>(value);
            }
            case 5123: {
                const auto value = static_cast<u16>(at[0] | at[1] << 8);
                return normalized ? static_cast<f32>(value) / 65535.0f : static_cast<f32>(value);
            }
            default: {
                const u32 value = readU32(std::span<const u8>(at, 4), 0);
                return normalized ? static_cast<f32>(static_cast<f64>(value) / 4294967295.0) : static_cast<f32>(value);
            }
        }
    }

    const json::Value& root_;
    std::vector<std::vector<u8>> buffers_;
};

void computeNormals(MeshData& mesh) {
    std::vector<Vec3> sums(mesh.vertices.size(), Vec3(0.0f));
    for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const u32 a = mesh.indices[i];
        const u32 b = mesh.indices[i + 1];
        const u32 c = mesh.indices[i + 2];
        // Area-weighted: the cross product's length is twice the area.
        const Vec3 face = glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                                     mesh.vertices[c].position - mesh.vertices[a].position);
        sums[a] += face;
        sums[b] += face;
        sums[c] += face;
    }
    for (usize i = 0; i < sums.size(); ++i) {
        const f32 length = glm::length(sums[i]);
        mesh.vertices[i].normal = length > 1e-12f ? sums[i] / length : Vec3(0.0f, 1.0f, 0.0f);
    }
}

Vec3 vec3(const json::Value& value, Vec3 fallback) {
    if (value.size() != 3) {
        return fallback;
    }
    return Vec3(static_cast<f32>(value[0].asNumber()), static_cast<f32>(value[1].asNumber()),
                static_cast<f32>(value[2].asNumber()));
}

}  // namespace

Result<ModelData> decodeGltf(std::span<const u8> bytes, const UriReader& readUri) {
    // Container: GLB chunks, or the JSON text itself.
    std::string_view text;
    std::vector<u8> binaryChunk;
    bool hasBinaryChunk = false;
    if (bytes.size() >= 12 && readU32(bytes, 0) == kGlbMagic) {
        if (readU32(bytes, 4) != 2) {
            return fail("gltf: GLB version {} (only 2 is supported)", readU32(bytes, 4));
        }
        const usize total = std::min<usize>(readU32(bytes, 8), bytes.size());
        for (usize offset = 12; offset + 8 <= total;) {
            const usize length = readU32(bytes, offset);
            const u32 type = readU32(bytes, offset + 4);
            if (length > total - offset - 8) {
                return fail("gltf: GLB chunk runs past the file");
            }
            const std::span<const u8> chunk = bytes.subspan(offset + 8, length);
            if (type == kChunkJson && text.empty()) {
                text = std::string_view(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            } else if (type == kChunkBinary && !hasBinaryChunk) {
                binaryChunk.assign(chunk.begin(), chunk.end());
                hasBinaryChunk = true;
            }
            offset += 8 + length;
        }
        if (text.empty()) {
            return fail("gltf: GLB without a JSON chunk");
        }
    } else {
        text = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    const auto parsed = json::parse(text);
    if (!parsed) {
        return fail("gltf: {}", parsed.error().message);
    }
    const json::Value& root = *parsed;
    if (!root["asset"]["version"].asString().starts_with("2.")) {
        return fail("gltf: not a glTF 2.0 file");
    }

    ModelData model;

    // Buffers.
    std::vector<std::vector<u8>> buffers;
    for (usize i = 0; i < root["buffers"].size(); ++i) {
        const json::Value& buffer = root["buffers"][i];
        std::vector<u8> data;
        if (const json::Value* uri = buffer.find("uri")) {
            auto loaded = loadUri(uri->asString(), readUri);
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            data = std::move(*loaded);
        } else if (i == 0 && hasBinaryChunk) {
            data = std::move(binaryChunk);
            hasBinaryChunk = false;
        } else {
            return fail("gltf: buffer {} has no data", i);
        }
        const auto length = integer(buffer["byteLength"]);
        if (!length || *length > data.size()) {
            return fail("gltf: buffer {} holds {} bytes, fewer than it declares", i, data.size());
        }
        data.resize(static_cast<usize>(*length));
        buffers.push_back(std::move(data));
    }
    const Decoder decoder(root, std::move(buffers));

    // Images (PNG), and textures pointing at them.
    for (usize i = 0; i < root["images"].size(); ++i) {
        const json::Value& image = root["images"][i];
        std::vector<u8> data;
        if (const json::Value* uri = image.find("uri")) {
            if (auto loaded = loadUri(uri->asString(), readUri)) {
                data = std::move(*loaded);
            } else {
                model.warnings.push_back(loaded.error().message);
            }
        } else if (const auto viewIndex = integer(image["bufferView"])) {
            if (auto view = decoder.view(*viewIndex)) {
                data.assign(view->begin(), view->end());
            } else {
                return std::unexpected(view.error());
            }
        }
        auto decoded = data.empty() ? Result<ImageData>(std::unexpected(Error{"gltf: image without data"}))
                                    : decodePng(data);
        if (decoded) {
            model.images.push_back(std::move(*decoded));
        } else {
            model.images.emplace_back();
            model.warnings.push_back(std::format("gltf: image {} ({}): {}", i, image["mimeType"].asString("?"),
                                                 decoded.error().message));
        }
    }
    const auto imageOf = [&](const json::Value& textureInfo) -> i32 {
        const auto texture = integer(textureInfo["index"]);
        if (!texture) {
            return -1;
        }
        const auto source = integer(root["textures"][static_cast<usize>(*texture)]["source"]);
        return source && *source < model.images.size() ? static_cast<i32>(*source) : -1;
    };

    // Materials.
    for (const json::Value& material : root["materials"].asArray()) {
        ModelMaterial out;
        out.name = std::string(material["name"].asString());
        const json::Value& pbr = material["pbrMetallicRoughness"];
        if (pbr["baseColorFactor"].size() == 4) {
            for (int c = 0; c < 4; ++c) {
                out.baseColor[c] = static_cast<f32>(pbr["baseColorFactor"][static_cast<usize>(c)].asNumber(1.0));
            }
        }
        out.baseColorImage = imageOf(pbr["baseColorTexture"]);
        out.metallic = static_cast<f32>(pbr["metallicFactor"].asNumber(1.0));
        out.roughness = static_cast<f32>(pbr["roughnessFactor"].asNumber(1.0));
        out.normalImage = imageOf(material["normalTexture"]);
        out.normalScale = static_cast<f32>(material["normalTexture"]["scale"].asNumber(1.0));
        out.emissive = vec3(material["emissiveFactor"], Vec3(0.0f));
        out.blend = material["alphaMode"].asString() == "BLEND";
        model.materials.push_back(std::move(out));
    }

    // Meshes: triangle primitives.
    for (usize m = 0; m < root["meshes"].size(); ++m) {
        const json::Value& mesh = root["meshes"][m];
        ModelMesh out;
        out.name = std::string(mesh["name"].asString());
        for (const json::Value& primitive : mesh["primitives"].asArray()) {
            if (integer(primitive["mode"]).value_or(4) != 4) {
                model.warnings.push_back(std::format("gltf: mesh {} has a non-triangle primitive, skipped", m));
                continue;
            }
            const json::Value& attributes = primitive["attributes"];
            const auto positionsIndex = integer(attributes["POSITION"]);
            if (!positionsIndex) {
                return fail("gltf: mesh {} has a primitive without positions", m);
            }
            auto positions = decoder.floats(*positionsIndex, 3);
            if (!positions) {
                return std::unexpected(positions.error());
            }
            ModelPrimitive part;
            const usize vertexCount = positions->size() / 3;
            part.mesh.vertices.resize(vertexCount);
            for (usize v = 0; v < vertexCount; ++v) {
                part.mesh.vertices[v].position = Vec3((*positions)[v * 3], (*positions)[v * 3 + 1], (*positions)[v * 3 + 2]);
            }
            // Other attributes must match the positions in count.
            const auto attribute = [&](const char* name, u32 components) -> Result<std::vector<f32>> {
                const auto index = integer(attributes[name]);
                if (!index) {
                    return std::vector<f32>{};
                }
                auto values = decoder.floats(*index, components);
                if (values && values->size() != vertexCount * components) {
                    return fail("gltf: mesh {}: {} has a different count than POSITION", m, name);
                }
                return values;
            };
            auto normals = attribute("NORMAL", 3);
            auto uvs = attribute("TEXCOORD_0", 2);
            auto tangents = attribute("TANGENT", 4);
            for (const auto* values : {&normals, &uvs, &tangents}) {
                if (!*values) {
                    return std::unexpected(values->error());
                }
            }
            for (usize v = 0; v < vertexCount; ++v) {
                Vertex& vertex = part.mesh.vertices[v];
                if (!normals->empty()) vertex.normal = Vec3((*normals)[v * 3], (*normals)[v * 3 + 1], (*normals)[v * 3 + 2]);
                if (!uvs->empty()) vertex.uv = Vec2((*uvs)[v * 2], (*uvs)[v * 2 + 1]);
                if (!tangents->empty()) {
                    vertex.tangent = Vec4((*tangents)[v * 4], (*tangents)[v * 4 + 1], (*tangents)[v * 4 + 2],
                                          (*tangents)[v * 4 + 3] < 0.0f ? -1.0f : 1.0f);
                }
            }
            if (const auto indexAccessor = integer(primitive["indices"])) {
                auto indices = decoder.indices(*indexAccessor, vertexCount);
                if (!indices) {
                    return std::unexpected(indices.error());
                }
                part.mesh.indices = std::move(*indices);
            } else {
                part.mesh.indices.resize(vertexCount);
                for (usize v = 0; v < vertexCount; ++v) {
                    part.mesh.indices[v] = static_cast<u32>(v);
                }
            }
            part.mesh.indices.resize(part.mesh.indices.size() / 3 * 3);  // whole triangles only
            if (normals->empty()) {
                computeNormals(part.mesh);
            }
            if (tangents->empty()) {
                computeTangents(part.mesh);
            }
            const auto material = integer(primitive["material"]);
            part.material = material && *material < model.materials.size() ? static_cast<i32>(*material) : -1;
            out.primitives.push_back(std::move(part));
        }
        model.meshes.push_back(std::move(out));
    }

    // Nodes, which must form trees: one parent at most.
    const usize nodeCount = root["nodes"].size();
    std::vector<u8> hasParent(nodeCount, 0);
    for (usize n = 0; n < nodeCount; ++n) {
        const json::Value& node = root["nodes"][n];
        ModelNode out;
        out.name = std::string(node["name"].asString());
        if (node["matrix"].size() == 16) {
            Mat4 matrix(1.0f);
            for (int i = 0; i < 16; ++i) {
                matrix[i / 4][i % 4] = static_cast<f32>(node["matrix"][static_cast<usize>(i)].asNumber());
            }
            Vec3 skew;
            Vec4 perspective;
            glm::decompose(matrix, out.scale, out.rotation, out.translation, skew, perspective);
        } else {
            out.translation = vec3(node["translation"], Vec3(0.0f));
            out.scale = vec3(node["scale"], Vec3(1.0f));
            if (node["rotation"].size() == 4) {
                // glTF stores x, y, z, w.
                const json::Value& r = node["rotation"];
                out.rotation = glm::normalize(Quat(static_cast<f32>(r[3].asNumber(1.0)), static_cast<f32>(r[0].asNumber()),
                                                   static_cast<f32>(r[1].asNumber()), static_cast<f32>(r[2].asNumber())));
            }
        }
        if (!std::isfinite(glm::dot(out.rotation, out.rotation))) {
            out.rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        if (const auto mesh = integer(node["mesh"]); mesh && *mesh < model.meshes.size()) {
            out.mesh = static_cast<i32>(*mesh);
        }
        for (const json::Value& child : node["children"].asArray()) {
            const auto index = integer(child);
            if (!index || *index >= nodeCount || *index == n || hasParent[static_cast<usize>(*index)] != 0) {
                return fail("gltf: node {} has an invalid child (nodes must form trees)", n);
            }
            hasParent[static_cast<usize>(*index)] = 1;
            out.children.push_back(static_cast<u32>(*index));
        }
        model.nodes.push_back(std::move(out));
    }
    // The default scene's roots, or every parentless node.
    const json::Value& scene = root["scenes"][static_cast<usize>(integer(root["scene"]).value_or(0))];
    if (scene.isObject()) {
        for (const json::Value& node : scene["nodes"].asArray()) {
            const auto index = integer(node);
            if (!index || *index >= nodeCount || hasParent[static_cast<usize>(*index)] != 0) {
                return fail("gltf: the scene lists an invalid root node");
            }
            model.roots.push_back(static_cast<u32>(*index));
        }
    } else {
        for (usize n = 0; n < nodeCount; ++n) {
            if (hasParent[n] == 0) {
                model.roots.push_back(static_cast<u32>(n));
            }
        }
    }
    // Animations: each channel samples one property of one node.
    for (usize a = 0; a < root["animations"].size(); ++a) {
        const json::Value& animation = root["animations"][a];
        ModelAnimation out;
        out.name = std::string(animation["name"].asString());
        for (const json::Value& channel : animation["channels"].asArray()) {
            const json::Value& target = channel["target"];
            const std::string_view path = target["path"].asString();
            const auto node = integer(target["node"]);
            if (path == "weights") {
                model.warnings.push_back(std::format("gltf: animation {} morphs weights, which is not supported", a));
                continue;
            }
            if (!node || *node >= model.nodes.size() || (path != "translation" && path != "rotation" && path != "scale")) {
                return fail("gltf: animation {} has a malformed channel", a);
            }
            const auto samplerIndex = integer(channel["sampler"]);
            const json::Value& sampler = animation["samplers"][static_cast<usize>(samplerIndex.value_or(~0ull))];
            const auto input = integer(sampler["input"]);
            const auto output = integer(sampler["output"]);
            if (!sampler.isObject() || !input || !output) {
                return fail("gltf: animation {} has a malformed sampler", a);
            }
            AnimationChannel track;
            track.node = static_cast<u32>(*node);
            track.property = path == "translation" ? AnimatedProperty::Translation
                             : path == "rotation"  ? AnimatedProperty::Rotation
                                                   : AnimatedProperty::Scale;
            const std::string_view interpolation = sampler["interpolation"].asString("LINEAR");
            track.interpolation = interpolation == "STEP"          ? Interpolation::Step
                                  : interpolation == "CUBICSPLINE" ? Interpolation::CubicSpline
                                                                   : Interpolation::Linear;
            auto times = decoder.floats(*input, 1);
            if (!times) {
                return std::unexpected(times.error());
            }
            const u32 components = track.property == AnimatedProperty::Rotation ? 4 : 3;
            auto values = decoder.floats(*output, components);
            if (!values) {
                return std::unexpected(values.error());
            }
            const usize perKey = track.interpolation == Interpolation::CubicSpline ? 3 : 1;
            if (times->empty() || values->size() != times->size() * perKey * components) {
                return fail("gltf: animation {}: keyframe times and values do not match", a);
            }
            for (usize k = 0; k < times->size(); ++k) {
                if (!std::isfinite((*times)[k]) || (k > 0 && (*times)[k] < (*times)[k - 1])) {
                    return fail("gltf: animation {}: keyframe times must rise", a);
                }
            }
            track.times = std::move(*times);
            track.values.resize(values->size() / components);
            for (usize v = 0; v < track.values.size(); ++v) {
                track.values[v] = Vec4((*values)[v * components], (*values)[v * components + 1],
                                       (*values)[v * components + 2], components == 4 ? (*values)[v * components + 3] : 0.0f);
            }
            out.duration = std::max(out.duration, track.times.back());
            out.channels.push_back(std::move(track));
        }
        model.animations.push_back(std::move(out));
    }
    return model;
}

Vec4 sampleChannel(const AnimationChannel& channel, f32 time) {
    if (channel.times.empty() || channel.values.empty()) {
        return channel.property == AnimatedProperty::Rotation ? Vec4(0.0f, 0.0f, 0.0f, 1.0f)
               : channel.property == AnimatedProperty::Scale  ? Vec4(1.0f)
                                                              : Vec4(0.0f);
    }
    const bool cubic = channel.interpolation == Interpolation::CubicSpline;
    const auto key = [&](usize index) { return channel.values[cubic ? index * 3 + 1 : index]; };
    const auto unit = [&](Vec4 value) {
        if (channel.property != AnimatedProperty::Rotation) {
            return value;
        }
        const f32 length = glm::length(value);
        return length > 1e-8f ? value / length : Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    };
    const usize last = channel.times.size() - 1;
    if (!(time > channel.times.front())) {
        return unit(key(0));
    }
    if (time >= channel.times[last]) {
        return unit(key(last));
    }
    // The keys around `time`.
    const usize next = static_cast<usize>(std::upper_bound(channel.times.begin(), channel.times.end(), time) -
                                          channel.times.begin());
    const usize previous = next - 1;
    const f32 span = channel.times[next] - channel.times[previous];
    const f32 t = span > 0.0f ? (time - channel.times[previous]) / span : 0.0f;
    switch (channel.interpolation) {
        case Interpolation::Step:
            return unit(key(previous));
        case Interpolation::CubicSpline: {
            // Hermite, with tangents scaled by the keys' spacing.
            const Vec4 p0 = key(previous);
            const Vec4 m0 = channel.values[previous * 3 + 2] * span;  // out-tangent
            const Vec4 p1 = key(next);
            const Vec4 m1 = channel.values[next * 3] * span;  // in-tangent
            const f32 t2 = t * t;
            const f32 t3 = t2 * t;
            return unit((2.0f * t3 - 3.0f * t2 + 1.0f) * p0 + (t3 - 2.0f * t2 + t) * m0 + (-2.0f * t3 + 3.0f * t2) * p1 +
                        (t3 - t2) * m1);
        }
        case Interpolation::Linear:
            break;
    }
    if (channel.property == AnimatedProperty::Rotation) {
        const Vec4 a = key(previous);
        const Vec4 b = key(next);
        const Quat q = glm::slerp(Quat(a.w, a.x, a.y, a.z), Quat(b.w, b.x, b.y, b.z), t);
        return unit(Vec4(q.x, q.y, q.z, q.w));
    }
    return key(previous) + (key(next) - key(previous)) * t;
}

Result<ModelData> loadGltf(const VirtualFileSystem& vfs, std::string_view virtualPath) {
    const auto bytes = vfs.readBinary(virtualPath);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const usize slash = virtualPath.rfind('/');
    const std::string directory(slash == std::string_view::npos ? std::string_view{} : virtualPath.substr(0, slash + 1));
    return decodeGltf(*bytes, [&](std::string_view uri) -> Result<std::vector<u8>> {
        // URIs are percent-encoded ("my%20ship.bin").
        std::string path = directory;
        for (usize i = 0; i < uri.size(); ++i) {
            if (uri[i] == '%' && i + 2 < uri.size()) {
                const std::string hex(uri.substr(i + 1, 2));
                char* end = nullptr;
                const long value = std::strtol(hex.c_str(), &end, 16);
                if (end == hex.c_str() + 2) {
                    path += static_cast<char>(value);
                    i += 2;
                    continue;
                }
            }
            path += uri[i];
        }
        return vfs.readBinary(path);
    });
}

}  // namespace phi::assets
