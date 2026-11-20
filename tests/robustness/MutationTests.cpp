// Feeds corrupted versions of valid inputs to every decoder of files that
// come from outside: image, sound, mesh and glTF loaders, JSON and config files, scene files (then
// steps physics over what was loaded). Decoders may reject or accept them,
// but must never crash, throw, or touch memory they do not own; the ASan
// preset turns any slip into a failure. The mutations are deterministic, so
// a failure reproduces.
#include <bdex/assets/Audio.hpp>
#include <bdex/assets/Gltf.hpp>
#include <bdex/assets/Image.hpp>
#include <bdex/assets/Mesh.hpp>
#include <bdex/core/Config.hpp>
#include <bdex/core/Json.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/physics/Physics.hpp>
#include <bdex/render/Components.hpp>
#include <bdex/render/Serialization.hpp>
#include <bdex/scene/Scene.hpp>
#include <bdex/scene/Serialization.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace bdex;

namespace {

constexpr int kMutations = 20000;  // per decoder: under a second, under two with ASan

/// Corrupts `data` with a few random edits: byte changes, bit flips,
/// deletions, insertions, truncation, duplicated chunks, and for text,
/// tokens that stress number and structure parsing.
std::vector<u8> mutate(std::vector<u8> data, bool text, std::mt19937& rng) {
    const auto roll = [&](usize n) { return n == 0 ? usize{0} : static_cast<usize>(rng() % n); };
    static constexpr const char* kTokens[] = {"-", "nan", "inf", "-inf", "1e39", "99999999999999999999", "=",
                                              "[", "]", "\n", " ", "/", "//", "#", "0", ",", ".", "f", "v",
                                              "vt", "vn", "-1", "-99999", "4000000000", "18446744073709551615",
                                              "\"", "\\"};
    const usize edits = 1 + roll(6);
    for (usize edit = 0; edit < edits; ++edit) {
        const usize at = roll(data.size());
        switch (roll(text ? 7 : 6)) {
            case 0:
                if (!data.empty()) data[at] = static_cast<u8>(rng());
                break;
            case 1:
                if (!data.empty()) data[at] ^= static_cast<u8>(1u << roll(8));
                break;
            case 2:
                if (!data.empty()) {
                    data.erase(data.begin() + static_cast<std::ptrdiff_t>(at),
                               data.begin() + static_cast<std::ptrdiff_t>(std::min(data.size(), at + 1 + roll(16))));
                }
                break;
            case 3:
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), static_cast<u8>(rng()));
                break;
            case 4:
                data.resize(roll(data.size()));
                break;
            case 5: {
                if (data.empty()) break;
                const usize from = roll(data.size());
                const usize length = std::min(roll(64), data.size() - from);
                const std::vector<u8> chunk(data.begin() + static_cast<std::ptrdiff_t>(from),
                                            data.begin() + static_cast<std::ptrdiff_t>(from + length));
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), chunk.begin(), chunk.end());
                break;
            }
            default: {
                const char* token = kTokens[roll(std::size(kTokens))];
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), token, token + std::strlen(token));
                break;
            }
        }
    }
    return data;
}

std::vector<u8> bytesOf(std::string_view text) { return {text.begin(), text.end()}; }
std::string textOf(const std::vector<u8>& bytes) { return {bytes.begin(), bytes.end()}; }

/// Runs `decode` over `count` corruptions of `seed`.
template <class Decode>
void fuzz(const std::vector<u8>& seed, bool text, u32 salt, Decode decode, int count = kMutations) {
    std::mt19937 rng(0xbdeu + salt);
    const log::Level previous = log::level();
    log::setLevel(log::Level::Error);  // loaders warn about what they skip
    for (int i = 0; i < count; ++i) {
        const std::vector<u8> input = mutate(seed, text, rng);
        EXPECT_NO_THROW(decode(input)) << "mutation " << i;
    }
    log::setLevel(previous);
}

}  // namespace

TEST(Robustness, ImageDecodersSurviveCorruptFiles) {
    assets::ImageData image;
    image.width = 13;
    image.height = 7;
    image.pixels.resize(13 * 7 * 4);
    for (usize i = 0; i < image.pixels.size(); ++i) {
        image.pixels[i] = static_cast<u8>(i * 37);
    }
    fuzz(assets::encodePng(image), false, 1, [](const std::vector<u8>& input) { (void)assets::decodePng(input); });

    std::vector<u8> tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 2, 0, 32, 8};  // 4 x 2 BGRA
    for (int i = 0; i < 4 * 2 * 4; ++i) {
        tga.push_back(static_cast<u8>(i * 11));
    }
    ASSERT_TRUE(assets::decodeTga(tga).has_value());
    fuzz(tga, false, 2, [](const std::vector<u8>& input) { (void)assets::decodeTga(input); });
}

TEST(Robustness, WavDecoderSurvivesCorruptFiles) {
    assets::AudioData stereo = assets::makeTone(440.0f, 0.01f);
    stereo.channels = 2;  // the mono samples read as half as many stereo frames
    stereo.samples.resize(stereo.samples.size() / 2 * 2);
    const std::vector<u8> wav = assets::encodeWav(stereo);
    ASSERT_TRUE(assets::decodeWav(wav).has_value());
    fuzz(wav, false, 6, [](const std::vector<u8>& input) { (void)assets::decodeWav(input); });
}

TEST(Robustness, GltfDecoderSurvivesCorruptFiles) {
    // A triangle with indices, a material, two nodes and a rotation
    // animation, its buffer inline.
    const std::string gltf = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"tri","mesh":0,"translation":[1,2,3],"children":[1]},{"rotation":[0,0,0,1],"scale":[2,2,2]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0.5,0,1]}}],"buffers":[{"byteLength":84,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAAAAAAAAADAPwAAAAAAAAAAAAAAAAAAgD8AAAAA9AQ1PwAAAAD0BDU/"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6},{"buffer":0,"byteOffset":44,"byteLength":8},{"buffer":0,"byteOffset":52,"byteLength":32}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},{"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR"},{"bufferView":3,"componentType":5126,"count":2,"type":"VEC4"}],"animations":[{"name":"turn","samplers":[{"input":2,"output":3,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}]}]})";
    const auto seed = assets::decodeGltf(bytesOf(gltf));
    ASSERT_TRUE(seed.has_value()) << seed.error().message;
    ASSERT_EQ(seed->meshes.size(), 1u);
    ASSERT_EQ(seed->animations.size(), 1u);
    fuzz(bytesOf(gltf), true, 8, [](const std::vector<u8>& input) {
        if (const auto model = assets::decodeGltf(input)) {
            for (const assets::ModelAnimation& animation : model->animations) {
                for (const assets::AnimationChannel& channel : animation.channels) {
                    (void)assets::sampleChannel(channel, animation.duration * 0.5f);
                }
            }
        }
    });
}

TEST(Robustness, ObjAndConfigParsersSurviveCorruptText) {
    const std::string obj =
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1\nf 2/2/1 4/1/1 3/3/1\nf -1 -2 -3\nf 1//1 2//1 3//1 4//1\n";
    ASSERT_TRUE(assets::parseObj(obj).has_value());
    fuzz(bytesOf(obj), true, 3, [](const std::vector<u8>& input) { (void)assets::parseObj(textOf(input)); });

    const std::string config =
        "# settings\n[window]\nwidth = 1280\ntitle = \"bdex\"  # comment\n[physics]\ngravity = 0, -9.81, 0\n";
    ASSERT_TRUE(Config::parse(config).has_value());

    const std::string gltfLike = R"({"asset":{"version":"2.0"},"nodes":[{"name":"n\u00e9","mesh":0,)"
                                 R"("translation":[1,-2.5e1,3E-2]}],"meshes":[{"primitives":[{"attributes":)"
                                 R"({"POSITION":0},"indices":1}]}],"extras":[true,false,null,"😀"]})";
    ASSERT_TRUE(json::parse(gltfLike).has_value());
    fuzz(bytesOf(gltfLike), true, 7, [](const std::vector<u8>& input) {
        if (const auto parsed = json::parse(textOf(input))) {
            (void)json::write(*parsed);
            (void)(*parsed)["nodes"][0]["translation"][1].asNumber();
        }
    });
    fuzz(bytesOf(config), true, 4, [](const std::vector<u8>& input) {
        if (const auto parsed = Config::parse(textOf(input))) {
            (void)parsed->getFloat("physics.gravity", 0.0);
            (void)parsed->toText();
        }
    });
}

TEST(Robustness, SceneFilesSurviveCorruptionAndSimulation) {
    scene::ComponentSerializers serializers = scene::ComponentSerializers::withSceneComponents();
    render::addComponentSerializers(serializers);
    physics::addComponentSerializers(serializers);

    scene::Scene source("source");
    ecs::World& world = source.world();
    const ecs::Entity floor = source.createEntity("Floor");
    world.emplace<physics::BoxCollider>(floor, Vec3(10.0f, 0.5f, 10.0f));
    const ecs::Entity ball = source.createEntity("Ball");
    world.get<scene::Transform>(ball).position = Vec3(0.0f, 3.0f, 0.0f);
    world.emplace<physics::SphereCollider>(ball, 0.5f);
    world.emplace<physics::RigidBody>(ball);
    const ecs::Entity crate = source.createEntity("Crate");
    world.get<scene::Transform>(crate).position = Vec3(1.0f, 2.0f, 0.0f);
    world.get<scene::Transform>(crate).rotation = glm::angleAxis(0.5f, glm::normalize(Vec3(1.0f, 1.0f, 0.0f)));
    world.emplace<physics::BoxCollider>(crate, Vec3(0.4f));
    world.emplace<physics::RigidBody>(crate).angularVelocity = Vec3(0.0f, 2.0f, 0.0f);
    const ecs::Entity zone = source.createEntity("Zone");
    world.emplace<physics::SphereCollider>(zone, 2.0f);
    world.emplace<physics::Trigger>(zone);
    world.emplace<scene::Parent>(zone, floor);
    world.emplace<render::Camera>(source.createEntity("Camera"));
    const std::string text = serializers.save(world);

    fuzz(bytesOf(text), true, 5, [&](const std::vector<u8>& input) {
        scene::Scene loaded("loaded");
        if (serializers.load(loaded.world(), textOf(input))) {
            scene::updateWorldTransforms(loaded.world());
            for (int step = 0; step < 3; ++step) {
                physics::step(loaded.world(), 1.0f / 60.0f);
            }
            (void)physics::raycast(loaded.world(), Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
        }
    }, kMutations / 4);  // loading and simulating costs more
}
