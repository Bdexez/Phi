#include <bdex/assets/AssetManager.hpp>
#include <bdex/assets/Image.hpp>
#include <bdex/assets/Mesh.hpp>
#include <bdex/core/FileSystem.hpp>
#include <bdex/core/JobSystem.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace bdex;
using namespace bdex::assets;

namespace {

/// True when every triangle is wound counter-clockwise as seen from outside,
/// i.e. its geometric normal agrees with the vertex normals.
bool windingMatchesNormals(const MeshData& mesh) {
    for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[i]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const Vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::dot(faceNormal, a.normal + b.normal + c.normal) <= 0.0f) {
            return false;
        }
    }
    return true;
}

u32 readBigEndian(const std::vector<u8>& bytes, usize offset) {
    return (static_cast<u32>(bytes[offset]) << 24) | (static_cast<u32>(bytes[offset + 1]) << 16) |
           (static_cast<u32>(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

/// Temporary directory removed at scope exit.
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("bdex_tests_" + std::to_string(std::hash<std::string>{}(
                                    ::testing::UnitTest::GetInstance()->current_test_info()->name())));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    void write(const std::string& name, const std::string& content) const {
        std::ofstream(path / name, std::ios::binary) << content;
    }
};

}  // namespace

// --- Primitives ----------------------------------------------------------------

TEST(Primitives, TangentsFollowTheTextureAxes) {
    for (const MeshData& mesh : {makeCube(2.0f), makePlane(3.0f), makeSphere(1.0f, 16, 8)}) {
        for (const Vertex& vertex : mesh.vertices) {
            const Vec3 tangent(vertex.tangent);
            EXPECT_NEAR(glm::length(tangent), 1.0f, 1e-4f);
            EXPECT_NEAR(glm::dot(tangent, vertex.normal), 0.0f, 1e-4f);
            EXPECT_EQ(std::abs(vertex.tangent.w), 1.0f);
        }
    }

    // Front face of the cube (+Z): U grows toward +X, texture "up" is +Y.
    const MeshData cube = makeCube(2.0f);
    for (const Vertex& vertex : cube.vertices) {
        if (vertex.normal == Vec3(0.0f, 0.0f, 1.0f)) {
            EXPECT_NEAR(vertex.tangent.x, 1.0f, 1e-5f);
            const Vec3 up = glm::cross(vertex.normal, Vec3(vertex.tangent)) * vertex.tangent.w;
            EXPECT_NEAR(up.y, 1.0f, 1e-5f);
        }
    }

    // OBJ meshes get them too, mirrored UVs included.
    const auto mirrored = parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 1 0\nvt 0 0\nvt 1 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n");
    ASSERT_TRUE(mirrored.has_value());
    EXPECT_NEAR(mirrored->vertices[0].tangent.x, -1.0f, 1e-5f);  // U grows toward -X
    const Vertex& corner = mirrored->vertices[0];
    const Vec3 up = glm::cross(corner.normal, Vec3(corner.tangent)) * corner.tangent.w;
    EXPECT_NEAR(up.y, 1.0f, 1e-5f);  // V (flipped on load) still maps up to +Y
}

TEST(Primitives, CubeHasPerFaceVerticesAndOutwardWinding) {
    const MeshData cube = makeCube(2.0f);
    EXPECT_EQ(cube.vertices.size(), 24u);
    EXPECT_EQ(cube.triangleCount(), 12u);
    EXPECT_TRUE(windingMatchesNormals(cube));
    for (const Vertex& vertex : cube.vertices) {
        EXPECT_FLOAT_EQ(std::abs(vertex.position.x), 1.0f);
        EXPECT_FLOAT_EQ(glm::length(vertex.normal), 1.0f);
    }
}

TEST(Primitives, PlaneFacesUp) {
    const MeshData plane = makePlane(10.0f, 4.0f);
    EXPECT_EQ(plane.triangleCount(), 2u);
    EXPECT_TRUE(windingMatchesNormals(plane));
    EXPECT_FLOAT_EQ(plane.vertices[1].uv.x, 4.0f);
}

TEST(Primitives, SphereIsClosedAndOutwardFacing) {
    const u32 segments = 16;
    const u32 rings = 8;
    const MeshData sphere = makeSphere(0.5f, segments, rings);
    EXPECT_EQ(sphere.vertices.size(), static_cast<usize>((segments + 1) * (rings + 1)));
    // Two triangles per quad, minus the degenerate ones at both poles.
    EXPECT_EQ(sphere.triangleCount(), static_cast<usize>(segments * (rings - 1) * 2));
    EXPECT_TRUE(windingMatchesNormals(sphere));
    for (const Vertex& vertex : sphere.vertices) {
        EXPECT_NEAR(glm::length(vertex.position), 0.5f, 1e-5f);
    }
}

// --- OBJ -------------------------------------------------------------------------

TEST(ObjLoader, TriangulatesPolygonsAndMergesVertices) {
    const auto mesh = parseObj(R"(
# a unit quad
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1 4/4/1
)");
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_EQ(mesh->vertices.size(), 4u);  // shared corners are not duplicated
    EXPECT_EQ(mesh->indices, (std::vector<u32>{0, 1, 2, 0, 2, 3}));
    EXPECT_EQ(mesh->vertices[2].normal, Vec3(0.0f, 0.0f, 1.0f));
    // V is flipped: OBJ's origin is bottom-left, the engine's is top-left.
    EXPECT_EQ(mesh->vertices[0].uv, Vec2(0.0f, 1.0f));
    EXPECT_EQ(mesh->vertices[2].uv, Vec2(1.0f, 0.0f));
}

TEST(ObjLoader, SupportsNegativeIndicesAndComputesMissingNormals) {
    const auto mesh = parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n");
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    ASSERT_EQ(mesh->vertices.size(), 3u);
    for (const Vertex& vertex : mesh->vertices) {
        EXPECT_EQ(vertex.normal, Vec3(0.0f, 0.0f, 1.0f));  // CCW in the XY plane => +Z
    }
}

TEST(ObjLoader, ReportsErrorsWithLineNumbers) {
    const auto badIndex = parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n");
    ASSERT_FALSE(badIndex.has_value());
    EXPECT_NE(badIndex.error().message.find("line 4"), std::string::npos);

    EXPECT_FALSE(parseObj("v 0 0\n").has_value());         // malformed position
    EXPECT_FALSE(parseObj("v 0 0 0\nv 1 0 0\n").has_value());  // no faces
}

// --- Images ----------------------------------------------------------------------

TEST(Image, CheckerboardAlternatesCells) {
    const Color8 white{255, 255, 255, 255};
    const Color8 black{0, 0, 0, 255};
    const ImageData image = makeCheckerboard(4, 2, white, black);
    ASSERT_TRUE(image.valid());
    const auto pixel = [&](u32 x, u32 y) { return image.pixels[(y * 4 + x) * 4]; };
    EXPECT_EQ(pixel(0, 0), 255);
    EXPECT_EQ(pixel(2, 0), 0);
    EXPECT_EQ(pixel(0, 2), 0);
    EXPECT_EQ(pixel(3, 3), 255);
}

TEST(Image, DecodesRawAndRleTga) {
    // 2x2, 24-bit, bottom-left origin. Pixels are stored as BGR.
    const std::vector<u8> header = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0};
    std::vector<u8> raw = header;
    raw.insert(raw.end(), {0, 0, 255, /* bottom-left: red */ 0, 255, 0, /* bottom-right: green */
                           255, 0, 0, /* top-left: blue */ 255, 255, 255 /* top-right: white */});

    const auto decoded = decodeTga(raw);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    ASSERT_TRUE(decoded->valid());
    // Rows are stored top to bottom in ImageData.
    EXPECT_EQ((std::vector<u8>(decoded->pixels.begin(), decoded->pixels.begin() + 8)),
              (std::vector<u8>{0, 0, 255, 255, 255, 255, 255, 255}));
    EXPECT_EQ((std::vector<u8>(decoded->pixels.begin() + 8, decoded->pixels.end())),
              (std::vector<u8>{255, 0, 0, 255, 0, 255, 0, 255}));

    // Same size, RLE (type 10), top-left origin, one run of 4 orange pixels.
    std::vector<u8> rle = header;
    rle[2] = 10;
    rle[17] = 0x20;
    rle.insert(rle.end(), {0x83, 0, 128, 255});
    const auto run = decodeTga(rle);
    ASSERT_TRUE(run.has_value()) << run.error().message;
    for (usize i = 0; i < run->pixels.size(); i += 4) {
        EXPECT_EQ(run->pixels[i], 255);
        EXPECT_EQ(run->pixels[i + 1], 128);
        EXPECT_EQ(run->pixels[i + 2], 0);
        EXPECT_EQ(run->pixels[i + 3], 255);
    }

    EXPECT_FALSE(decodeTga(std::vector<u8>(4, 0)).has_value());                     // too small
    EXPECT_FALSE(decodeTga(std::vector<u8>(header.begin(), header.end())).has_value());  // truncated
}

TEST(Image, NormalMapsFollowTheSlopes) {
    // Heights rising to the right: normals lean left (-X).
    std::vector<f32> ramp(8 * 8);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 1; x < 7; ++x) {
            ramp[y * 8 + x] = static_cast<f32>(x);
        }
    }
    const ImageData right = makeNormalMap(ramp, 8, 8);
    ASSERT_TRUE(right.valid());
    const u8* middle = right.pixels.data() + (4 * 8 + 4) * 4;
    EXPECT_LT(middle[0], 128);
    EXPECT_EQ(middle[1], 128);
    EXPECT_GT(middle[2], 128);

    // Heights rising toward the bottom rows: the surface faces up the
    // texture, so normals lean toward +Y (texture up).
    std::vector<f32> down(8 * 8);
    for (u32 y = 1; y < 7; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            down[y * 8 + x] = static_cast<f32>(y);
        }
    }
    const ImageData rising = makeNormalMap(down, 8, 8);
    const u8* lower = rising.pixels.data() + (4 * 8 + 4) * 4;
    EXPECT_GT(lower[1], 128);

    // Flat: straight up.
    const ImageData flat = makeNormalMap(std::vector<f32>(16, 3.0f), 4, 4);
    EXPECT_EQ(flat.pixels[0], 128);
    EXPECT_EQ(flat.pixels[1], 128);
    EXPECT_EQ(flat.pixels[2], 255);
    EXPECT_FALSE(makeNormalMap(std::vector<f32>(3), 2, 2).valid());  // size mismatch
}

TEST(Image, EncodesWellFormedPng) {
    const ImageData image = makeCheckerboard(300, 3, {255, 0, 0, 255}, {0, 0, 255, 255});
    const std::vector<u8> png = encodePng(image);

    const std::vector<u8> signature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    ASSERT_GT(png.size(), 33u);
    EXPECT_TRUE(std::equal(signature.begin(), signature.end(), png.begin()));

    // IHDR is the first chunk: length 13, then width and height.
    EXPECT_EQ(readBigEndian(png, 8), 13u);
    EXPECT_EQ(std::string(png.begin() + 12, png.begin() + 16), "IHDR");
    EXPECT_EQ(readBigEndian(png, 16), 300u);
    EXPECT_EQ(readBigEndian(png, 20), 300u);
    EXPECT_EQ(png[24], 8);  // bit depth
    EXPECT_EQ(png[25], 6);  // RGBA

    // The file ends with an empty IEND chunk.
    EXPECT_EQ(std::string(png.end() - 8, png.end() - 4), "IEND");

    // A flat checkerboard compresses to a tiny fraction of its 360 KB of
    // pixels, and decodes back to them.
    EXPECT_LT(png.size(), image.pixels.size() / 100);
    const auto decoded = decodePng(png);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->pixels, image.pixels);
}

// --- AssetManager ------------------------------------------------------------------

TEST(AssetManager, LoadsThroughTheVirtualFileSystemAndCaches) {
    const TempDir dir;
    dir.write("triangle.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);

    const auto first = assets.load<MeshData>("data:/triangle.obj");
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_EQ((*first)->triangleCount(), 1u);

    const auto second = assets.load<MeshData>("data:/triangle.obj");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->get(), second->get());  // same instance: served from the cache
    EXPECT_EQ(assets.cachedCount(), 1u);
}

TEST(AssetManager, LoadsPngImages) {
    const TempDir dir;
    const ImageData source = makeCheckerboard(8, 2, {255, 0, 0, 255}, {0, 255, 0, 255});
    ASSERT_TRUE(writePng(dir.path / "checker.png", source).has_value());

    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);
    const auto image = assets.load<ImageData>("data:/checker.png");
    ASSERT_TRUE(image.has_value()) << image.error().message;
    EXPECT_EQ((*image)->pixels, source.pixels);
}

TEST(AssetManager, LoadsAsynchronouslyOnTheJobSystem) {
    const TempDir dir;
    dir.write("triangle.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    JobSystem jobs(2);
    AssetManager assets(vfs, &jobs);

    const AssetFuture<MeshData> pending = assets.loadAsync<MeshData>("data:/triangle.obj");
    ASSERT_TRUE(pending.valid());
    const auto mesh = pending.get();
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_EQ((*mesh)->triangleCount(), 1u);
    EXPECT_TRUE(pending.ready());

    // Once loaded, the asset is served from the cache, sync or async.
    EXPECT_EQ(assets.load<MeshData>("data:/triangle.obj")->get(), mesh->get());
    const auto again = assets.loadAsync<MeshData>("data:/triangle.obj");
    EXPECT_TRUE(again.ready());
    EXPECT_EQ(again.get()->get(), mesh->get());

    const auto missing = assets.loadAsync<MeshData>("data:/missing.obj").get();
    EXPECT_FALSE(missing.has_value());
}

TEST(AssetManager, ConcurrentRequestsShareOneLoad) {
    struct Slow {
        int value;
    };
    const TempDir dir;
    dir.write("slow.txt", "42");
    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    JobSystem jobs(4);
    AssetManager assets(vfs, &jobs);

    std::atomic<int> calls{0};
    assets.registerLoader<Slow>(".txt", [&calls](std::span<const u8> bytes) -> Result<Slow> {
        ++calls;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return Slow{std::stoi(std::string(bytes.begin(), bytes.end()))};
    });

    std::vector<AssetFuture<Slow>> requests;
    for (int i = 0; i < 8; ++i) {
        requests.push_back(assets.loadAsync<Slow>("data:/slow.txt"));
    }
    const auto direct = assets.load<Slow>("data:/slow.txt");  // joins the load in flight
    ASSERT_TRUE(direct.has_value()) << direct.error().message;
    for (const auto& request : requests) {
        EXPECT_EQ(request.get()->get(), direct->get());
    }
    EXPECT_EQ((*direct)->value, 42);
    EXPECT_EQ(calls.load(), 1);
}

TEST(AssetManager, FailedLoadsAreRetried) {
    const TempDir dir;
    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    JobSystem jobs(1);
    AssetManager assets(vfs, &jobs);

    EXPECT_FALSE(assets.loadAsync<MeshData>("data:/late.obj").get().has_value());
    dir.write("late.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    EXPECT_TRUE(assets.loadAsync<MeshData>("data:/late.obj").get().has_value());
}

TEST(AssetManager, AsyncLoadsWithoutJobSystemCompleteImmediately) {
    const TempDir dir;
    dir.write("triangle.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);

    const auto pending = assets.loadAsync<MeshData>("data:/triangle.obj");
    EXPECT_TRUE(pending.ready());
    EXPECT_TRUE(pending.get().has_value());
}

TEST(AssetManager, DestructionWaitsForLoadsInFlight) {
    const TempDir dir;
    for (int i = 0; i < 16; ++i) {
        dir.write("mesh" + std::to_string(i) + ".obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    }
    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    JobSystem jobs(4);
    std::vector<AssetFuture<MeshData>> requests;
    {
        AssetManager assets(vfs, &jobs);
        for (int i = 0; i < 16; ++i) {
            requests.push_back(assets.loadAsync<MeshData>("data:/mesh" + std::to_string(i) + ".obj"));
        }
    }  // must not leave jobs running against a destroyed manager
    for (const auto& request : requests) {
        EXPECT_TRUE(request.ready());
        EXPECT_TRUE(request.get().has_value());
    }
}

TEST(AssetManager, MatchesExtensionsRegardlessOfCase) {
    const TempDir dir;
    dir.write("TRIANGLE.OBJ", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    dir.write("notes.TxT", "hello");

    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);
    assets.registerLoader<std::string>(".TXT", [](std::span<const u8> bytes) -> Result<std::string> {
        return std::string(bytes.begin(), bytes.end());
    });

    const auto mesh = assets.load<MeshData>("data:/TRIANGLE.OBJ");
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_EQ((*mesh)->triangleCount(), 1u);

    const auto note = assets.load<std::string>("data:/notes.TxT");
    ASSERT_TRUE(note.has_value()) << note.error().message;
    EXPECT_EQ(**note, "hello");
}

TEST(AssetManager, ReportsMissingFilesLoadersAndMounts) {
    const TempDir dir;
    dir.write("notes.txt", "hello");

    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);

    EXPECT_FALSE(assets.load<MeshData>("data:/missing.obj").has_value());
    EXPECT_FALSE(assets.load<MeshData>("nowhere:/triangle.obj").has_value());

    const auto noLoader = assets.load<MeshData>("data:/notes.txt");
    ASSERT_FALSE(noLoader.has_value());
    EXPECT_NE(noLoader.error().message.find(".txt"), std::string::npos);
}

TEST(AssetManager, AcceptsCustomLoaders) {
    struct Note {
        std::string text;
    };
    const TempDir dir;
    dir.write("notes.txt", "hello");

    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);
    assets.registerLoader<Note>(".txt", [](std::span<const u8> bytes) -> Result<Note> {
        return Note{std::string(bytes.begin(), bytes.end())};
    });

    const auto note = assets.load<Note>("data:/notes.txt");
    ASSERT_TRUE(note.has_value()) << note.error().message;
    EXPECT_EQ((*note)->text, "hello");
}
