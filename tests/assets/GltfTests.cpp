#include <bdex/assets/Gltf.hpp>
#include <bdex/core/Json.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace bdex;
using namespace bdex::assets;

namespace {

/// Builds the binary buffer of a glTF test file piece by piece.
struct Buffer {
    std::vector<u8> bytes;
    json::Array views;
    json::Array accessors;

    /// Appends raw data as a buffer view; returns its index.
    usize view(const void* data, usize size, usize stride = 0) {
        while (bytes.size() % 4 != 0) bytes.push_back(0);
        json::Object view = {{"buffer", 0}, {"byteOffset", static_cast<f64>(bytes.size())},
                             {"byteLength", static_cast<f64>(size)}};
        if (stride != 0) view.emplace_back("byteStride", static_cast<f64>(stride));
        const auto* raw = static_cast<const u8*>(data);
        bytes.insert(bytes.end(), raw, raw + size);
        views.emplace_back(std::move(view));
        return views.size() - 1;
    }
    usize accessor(usize view, f64 componentType, const char* type, usize count, f64 offset = 0, bool normalized = false) {
        accessors.emplace_back(json::Object{{"bufferView", static_cast<f64>(view)}, {"componentType", componentType},
                                            {"type", type}, {"count", static_cast<f64>(count)},
                                            {"byteOffset", offset}, {"normalized", normalized}});
        return accessors.size() - 1;
    }
};

std::string base64(const std::vector<u8>& data) {
    static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (usize i = 0; i < data.size(); i += 3) {
        const u32 chunk = static_cast<u32>(data[i]) << 16 | (i + 1 < data.size() ? data[i + 1] << 8 : 0) |
                          (i + 2 < data.size() ? data[i + 2] : 0);
        out += kDigits[chunk >> 18 & 63];
        out += kDigits[chunk >> 12 & 63];
        out += i + 1 < data.size() ? kDigits[chunk >> 6 & 63] : '=';
        out += i + 2 < data.size() ? kDigits[chunk & 63] : '=';
    }
    return out;
}

std::vector<u8> bytesOf(const std::string& text) { return {text.begin(), text.end()}; }

std::vector<u8> glb(const std::string& text, const std::vector<u8>& binary) {
    std::vector<u8> out;
    const auto u32le = [&](u32 value) {
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<u8>(value >> shift));
    };
    std::string padded = text;
    while (padded.size() % 4 != 0) padded += ' ';
    std::vector<u8> bin = binary;
    while (bin.size() % 4 != 0) bin.push_back(0);
    u32le(0x46546C67);
    u32le(2);
    u32le(static_cast<u32>(12 + 8 + padded.size() + 8 + bin.size()));
    u32le(static_cast<u32>(padded.size()));
    u32le(0x4E4F534A);
    out.insert(out.end(), padded.begin(), padded.end());
    u32le(static_cast<u32>(bin.size()));
    u32le(0x004E4942);
    out.insert(out.end(), bin.begin(), bin.end());
    return out;
}

/// A quad (two triangles) with indices, no normals, and a red material.
struct QuadFile {
    Buffer buffer;
    json::Object root;

    QuadFile() {
        const f32 positions[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
        const u16 indices[] = {0, 1, 2, 0, 2, 3};
        const usize positionView = buffer.view(positions, sizeof(positions));
        const usize indexView = buffer.view(indices, sizeof(indices));
        buffer.accessor(positionView, 5126, "VEC3", 4);
        buffer.accessor(indexView, 5123, "SCALAR", 6);
        const json::Object primitive = {
            {"attributes", json::Object{{"POSITION", 0}}}, {"indices", 1}, {"material", 0}};
        const json::Object mesh = {{"name", "quad"}, {"primitives", json::Array{primitive}}};
        const json::Object node = {{"name", "Quad"}, {"mesh", 0}, {"translation", json::Array{1, 2, 3}}};
        const json::Object pbr = {{"baseColorFactor", json::Array{1, 0, 0, 1}}, {"roughnessFactor", 0.25}};
        const json::Object material = {{"name", "red"},
                                       {"pbrMetallicRoughness", pbr},
                                       {"emissiveFactor", json::Array{0, 0.5, 0}},
                                       {"alphaMode", "BLEND"}};
        root = {
            {"asset", json::Object{{"version", "2.0"}}},
            {"scene", 0},
            {"scenes", json::Array{json::Object{{"nodes", json::Array{0}}}}},
            {"nodes", json::Array{node}},
            {"meshes", json::Array{mesh}},
            {"materials", json::Array{material}},
        };
    }

    json::Object withBuffer(json::Object buffer0) const {
        json::Object copy = root;
        copy.emplace_back("buffers", json::Array{std::move(buffer0)});
        copy.emplace_back("bufferViews", buffer.views);
        copy.emplace_back("accessors", buffer.accessors);
        return copy;
    }
};

void expectQuad(const ModelData& model) {
    ASSERT_EQ(model.roots, (std::vector<u32>{0}));
    ASSERT_EQ(model.nodes.size(), 1u);
    EXPECT_EQ(model.nodes[0].name, "Quad");
    EXPECT_EQ(model.nodes[0].translation, Vec3(1.0f, 2.0f, 3.0f));
    ASSERT_EQ(model.meshes.size(), 1u);
    ASSERT_EQ(model.meshes[0].primitives.size(), 1u);
    const ModelPrimitive& part = model.meshes[0].primitives[0];
    EXPECT_EQ(part.mesh.vertices.size(), 4u);
    EXPECT_EQ(part.mesh.indices, (std::vector<u32>{0, 1, 2, 0, 2, 3}));
    EXPECT_EQ(part.mesh.vertices[2].position, Vec3(1.0f, 1.0f, 0.0f));
    EXPECT_NEAR(part.mesh.vertices[0].normal.z, 1.0f, 1e-6f);  // computed: facing +Z
    EXPECT_EQ(part.material, 0);
    ASSERT_EQ(model.materials.size(), 1u);
    EXPECT_EQ(model.materials[0].baseColor, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(model.materials[0].roughness, 0.25f);
    EXPECT_FLOAT_EQ(model.materials[0].metallic, 1.0f);  // the default
    EXPECT_EQ(model.materials[0].emissive, Vec3(0.0f, 0.5f, 0.0f));
    EXPECT_TRUE(model.materials[0].blend);
}

}  // namespace

TEST(Gltf, ReadsTheSameQuadFromEveryContainer) {
    QuadFile file;
    const auto size = static_cast<f64>(file.buffer.bytes.size());

    // Binary GLB: the buffer is the BIN chunk.
    const auto fromGlb = decodeGltf(glb(json::write(file.withBuffer({{"byteLength", size}})), file.buffer.bytes));
    ASSERT_TRUE(fromGlb.has_value()) << fromGlb.error().message;
    expectQuad(*fromGlb);

    // JSON with the buffer embedded as base64.
    const auto fromDataUri = decodeGltf(bytesOf(json::write(file.withBuffer(
        {{"byteLength", size}, {"uri", "data:application/octet-stream;base64," + base64(file.buffer.bytes)}}))));
    ASSERT_TRUE(fromDataUri.has_value()) << fromDataUri.error().message;
    expectQuad(*fromDataUri);

    // JSON with an external .bin, read through the caller.
    const std::string external = json::write(file.withBuffer({{"byteLength", size}, {"uri", "quad.bin"}}));
    std::string asked;
    const auto fromFile = decodeGltf(bytesOf(external), [&](std::string_view uri) -> Result<std::vector<u8>> {
        asked = std::string(uri);
        return file.buffer.bytes;
    });
    ASSERT_TRUE(fromFile.has_value()) << fromFile.error().message;
    EXPECT_EQ(asked, "quad.bin");
    expectQuad(*fromFile);
    EXPECT_FALSE(decodeGltf(bytesOf(external)).has_value());  // no reader for it
}

TEST(Gltf, ReadsInterleavedNormalizedAttributesAndHierarchies) {
    // One interleaved view: position (3 floats), then UV as normalized
    // unsigned bytes, padded to a 16-byte stride.
    struct Packed {
        f32 position[3];
        u8 uv[2];
        u8 padding[2];
    };
    static_assert(sizeof(Packed) == 16);
    const Packed vertices[3] = {{{0, 0, 0}, {0, 255}, {}}, {{1, 0, 0}, {255, 255}, {}}, {{0, 1, 0}, {0, 0}, {}}};
    Buffer buffer;
    const usize view = buffer.view(vertices, sizeof(vertices), sizeof(Packed));
    buffer.accessor(view, 5126, "VEC3", 3);
    buffer.accessor(view, 5121, "VEC2", 3, 12, true);
    const f32 matrix[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 5, 6, 7, 1};  // column-major: scale 2, move (5, 6, 7)
    json::Array matrixValues;
    for (const f32 value : matrix) matrixValues.emplace_back(static_cast<f64>(value));
    const json::Object primitive = {{"attributes", json::Object{{"POSITION", 0}, {"TEXCOORD_0", 1}}}};
    const json::Object meshEntry = {{"primitives", json::Array{primitive}}};
    const json::Object parent = {{"name", "parent"}, {"children", json::Array{1}}, {"matrix", matrixValues}};
    const json::Object child = {{"name", "child"}, {"mesh", 0}, {"rotation", json::Array{0, 0.7071068, 0, 0.7071068}}};
    const json::Object embedded = {{"byteLength", static_cast<f64>(buffer.bytes.size())},
                                   {"uri", "data:;base64," + base64(buffer.bytes)}};
    const json::Object root = {
        {"asset", json::Object{{"version", "2.0"}}},
        {"buffers", json::Array{embedded}},
        {"bufferViews", buffer.views},
        {"accessors", buffer.accessors},
        {"meshes", json::Array{meshEntry}},
        {"nodes", json::Array{parent, child}},
    };
    const auto model = decodeGltf(bytesOf(json::write(json::Value(root))));
    ASSERT_TRUE(model.has_value()) << model.error().message;
    // No scenes: the parentless node is the root.
    EXPECT_EQ(model->roots, (std::vector<u32>{0}));
    EXPECT_EQ(model->nodes[0].children, (std::vector<u32>{1}));
    EXPECT_NEAR(model->nodes[0].scale.x, 2.0f, 1e-5f);
    EXPECT_NEAR(model->nodes[0].translation.z, 7.0f, 1e-5f);
    EXPECT_NEAR(glm::angle(model->nodes[1].rotation), glm::radians(90.0f), 1e-4f);
    const MeshData& mesh = model->meshes[0].primitives[0].mesh;
    EXPECT_EQ(mesh.vertices[1].position, Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(mesh.vertices[1].uv, Vec2(1.0f, 1.0f));
    EXPECT_EQ(mesh.vertices[2].uv, Vec2(0.0f, 0.0f));
    EXPECT_EQ(mesh.indices, (std::vector<u32>{0, 1, 2}));  // none given: in order
    EXPECT_EQ(model->meshes[0].primitives[0].material, -1);
}

TEST(Gltf, DecodesEmbeddedPngsAndReportsOtherImages) {
    ImageData pixel;
    pixel.width = 1;
    pixel.height = 1;
    pixel.pixels = {10, 20, 30, 255};
    const std::vector<u8> png = encodePng(pixel);
    Buffer buffer;
    const usize view = buffer.view(png.data(), png.size());
    const json::Object root = {
        {"asset", json::Object{{"version", "2.0"}}},
        {"buffers", json::Array{json::Object{{"byteLength", static_cast<f64>(buffer.bytes.size())}}}},
        {"bufferViews", buffer.views},
        {"images", json::Array{json::Object{{"bufferView", static_cast<f64>(view)}, {"mimeType", "image/png"}},
                               json::Object{{"uri", "photo.jpg"}}}},
        {"textures", json::Array{json::Object{{"source", 0}}, json::Object{{"source", 1}}}},
        {"materials", json::Array{json::Object{
                          {"pbrMetallicRoughness", json::Object{{"baseColorTexture", json::Object{{"index", 0}}}}},
                          {"normalTexture", json::Object{{"index", 1}, {"scale", 0.5}}}}}},
    };
    const auto model = decodeGltf(glb(json::write(json::Value(root)), buffer.bytes),
                                  [](std::string_view) -> Result<std::vector<u8>> { return std::vector<u8>{0xFF, 0xD8}; });
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ASSERT_EQ(model->images.size(), 2u);
    ASSERT_TRUE(model->images[0].valid());
    EXPECT_EQ(model->images[0].pixels[1], 20);
    EXPECT_FALSE(model->images[1].valid());  // a JPEG: kept as a gap, reported
    ASSERT_EQ(model->warnings.size(), 1u);
    EXPECT_EQ(model->materials[0].baseColorImage, 0);
    EXPECT_EQ(model->materials[0].normalImage, 1);
    EXPECT_FLOAT_EQ(model->materials[0].normalScale, 0.5f);
}

TEST(Gltf, RefusesMalformedFiles) {
    QuadFile file;
    const auto size = static_cast<f64>(file.buffer.bytes.size());
    const auto decode = [&](json::Object root) { return decodeGltf(glb(json::write(json::Value(root)), file.buffer.bytes)); };
    const auto base = [&]() { return file.withBuffer({{"byteLength", size}}); };
    const auto set = [](json::Object& object, const std::string& key, json::Value value) {
        for (auto& [name, member] : object) {
            if (name == key) {
                member = std::move(value);
                return;
            }
        }
        object.emplace_back(key, std::move(value));
    };

    EXPECT_FALSE(decodeGltf(bytesOf("not json")).has_value());
    json::Object oldVersion = base();
    set(oldVersion, "asset", json::Object{{"version", "1.0"}});
    EXPECT_FALSE(decode(oldVersion).has_value());

    // An accessor longer than its view, and an index past the vertices.
    json::Object tooLong = base();
    json::Array accessors = file.buffer.accessors;
    accessors[0] = json::Object{{"bufferView", 0}, {"componentType", 5126}, {"type", "VEC3"}, {"count", 1e9}};
    set(tooLong, "accessors", accessors);
    EXPECT_FALSE(decode(tooLong).has_value());
    json::Object badIndex = base();
    accessors = file.buffer.accessors;
    accessors[0] = json::Object{{"bufferView", 0}, {"componentType", 5126}, {"type", "VEC3"}, {"count", 2}};
    set(badIndex, "accessors", accessors);
    EXPECT_FALSE(decode(badIndex).has_value());  // indices reach vertex 3

    // A buffer claiming more bytes than it has; a view outside its buffer.
    EXPECT_FALSE(decode(file.withBuffer({{"byteLength", size + 100}})).has_value());
    json::Object badView = base();
    json::Array views = file.buffer.views;
    views[0] = json::Object{{"buffer", 0}, {"byteOffset", 1e6}, {"byteLength", 48}};
    set(badView, "bufferViews", views);
    EXPECT_FALSE(decode(badView).has_value());

    // Nodes that are not trees.
    json::Object shared = base();
    set(shared, "nodes", json::Array{json::Object{{"children", json::Array{2}}}, json::Object{{"children", json::Array{2}}},
                                     json::Object{}});
    EXPECT_FALSE(decode(shared).has_value());
    json::Object selfParent = base();
    set(selfParent, "nodes", json::Array{json::Object{{"children", json::Array{0}}}});
    EXPECT_FALSE(decode(selfParent).has_value());

    // A truncated GLB.
    std::vector<u8> truncated = glb(json::write(json::Value(base())), file.buffer.bytes);
    truncated[12] = 0xFF;  // JSON chunk length past the end
    EXPECT_FALSE(decodeGltf(truncated).has_value());
}
