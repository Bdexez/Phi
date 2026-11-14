#include <phi/audio/Components.hpp>
#include <phi/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <numbers>
#include <vector>

using namespace phi;
using namespace phi::audio;

namespace {

std::shared_ptr<const assets::AudioData> constant(f32 value, usize frames) {
    auto clip = std::make_shared<assets::AudioData>();
    clip->sampleRate = 48000;
    clip->channels = 1;
    clip->samples.assign(frames, value);
    return clip;
}

/// One stereo frame of the mix.
std::pair<f32, f32> listen(Mixer& mixer) {
    std::vector<f32> out(2);
    mixer.mix(out);
    return {out[0], out[1]};
}

struct Rig {
    std::shared_ptr<Mixer> mixer = std::make_shared<Mixer>();
    AudioSystem system{mixer};
    scene::Scene scene{"test"};
    ecs::World& world = scene.world();

    void frame() {
        scene::updateWorldTransforms(world);
        system.update(world);
    }
};

}  // namespace

TEST(AudioComponents, SourcesPlayWhereTheirEntityIsHeardFromTheListener) {
    Rig rig;
    const ecs::Entity ears = rig.scene.createEntity("ears");
    rig.world.emplace<AudioListener>(ears);
    // A parent turned half a turn: the child's world position is on the
    // listener's left even though its local position says right.
    const ecs::Entity carrier = rig.scene.createEntity("carrier");
    rig.world.get<scene::Transform>(carrier).rotation = glm::angleAxis(std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f));
    const ecs::Entity radio = rig.scene.createEntity("radio");
    rig.world.emplace<scene::Parent>(radio, carrier);
    rig.world.get<scene::Transform>(radio).position = Vec3(0.5f, 0.0f, 0.0f);
    rig.world.emplace<AudioSource>(radio, constant(1.0f, 48000), PlayParams{.spatial = true});

    rig.frame();
    const VoiceId voice = rig.world.get<AudioSource>(radio).voice;
    ASSERT_NE(voice, 0u);
    EXPECT_TRUE(rig.mixer->playing(voice));
    auto [left, right] = listen(*rig.mixer);
    EXPECT_FLOAT_EQ(left, 1.0f);
    EXPECT_NEAR(right, 0.0f, 1e-6f);

    // The listener turns around: now it is on the right. Volume changes
    // apply to the playing voice.
    rig.world.get<scene::Transform>(ears).rotation = glm::angleAxis(std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f));
    rig.world.get<AudioSource>(radio).params.volume = 0.5f;
    rig.frame();
    std::tie(left, right) = listen(*rig.mixer);
    EXPECT_NEAR(left, 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(right, 0.5f);
    EXPECT_EQ(rig.world.get<AudioSource>(radio).voice, voice);  // same voice, not restarted
}

TEST(AudioComponents, VoicesEndWithTheirSource) {
    Rig rig;
    const auto clip = constant(0.5f, 48000);
    const ecs::Entity a = rig.scene.createEntity("a");
    const ecs::Entity b = rig.scene.createEntity("b");
    const ecs::Entity c = rig.scene.createEntity("c");
    for (const ecs::Entity entity : {a, b, c}) {
        rig.world.emplace<AudioSource>(entity, clip, PlayParams{});
    }
    rig.frame();
    EXPECT_EQ(rig.mixer->voiceCount(), 3u);

    // Destroyed entity, removed component, stop(): each voice goes.
    rig.world.destroy(a);
    rig.world.remove<AudioSource>(b);
    stop(rig.world.get<AudioSource>(c));
    rig.frame();
    EXPECT_EQ(rig.mixer->voiceCount(), 0u);
    EXPECT_EQ(rig.system.trackedVoices(), 0u);
    rig.frame();  // stopped sources stay stopped
    EXPECT_EQ(rig.mixer->voiceCount(), 0u);

    // Assigning a source to itself keeps its voice.
    restart(rig.world.get<AudioSource>(c));
    rig.frame();
    AudioSource& playing = rig.world.get<AudioSource>(c);
    const VoiceId before = playing.voice;
    AudioSource& alias = playing;
    playing = alias;
    EXPECT_EQ(rig.world.get<AudioSource>(c).voice, before);
    rig.frame();
    EXPECT_EQ(rig.mixer->voiceCount(), 1u);
    stop(rig.world.get<AudioSource>(c));
    rig.frame();

    // restart() plays again from the start.
    restart(rig.world.get<AudioSource>(c));
    rig.frame();
    EXPECT_EQ(rig.mixer->voiceCount(), 1u);

    // A copy plays its own voice; a clip played to its end lets go.
    const ecs::Entity copy = rig.world.clone(c);
    rig.frame();
    EXPECT_EQ(rig.mixer->voiceCount(), 2u);
    EXPECT_NE(rig.world.get<AudioSource>(copy).voice, rig.world.get<AudioSource>(c).voice);
    rig.mixer->advance(1.1);
    rig.frame();
    EXPECT_EQ(rig.world.get<AudioSource>(c).voice, 0u);
    EXPECT_EQ(rig.system.trackedVoices(), 0u);

    // Sounds of a world stop when another one is updated instead.
    restart(rig.world.get<AudioSource>(c));
    rig.frame();
    scene::Scene other("other");
    rig.system.update(other.world());
    EXPECT_EQ(rig.mixer->voiceCount(), 0u);
}

TEST(AudioComponents, SourcesCanWaitToBePlayed) {
    Rig rig;
    const ecs::Entity door = rig.scene.createEntity("door");
    rig.world.emplace<AudioSource>(door, constant(0.5f, 100), PlayParams{}, false);
    rig.frame();
    EXPECT_EQ(rig.mixer->voiceCount(), 0u);
    restart(rig.world.get<AudioSource>(door));  // the door opens
    rig.frame();
    EXPECT_EQ(rig.mixer->voiceCount(), 1u);
}

TEST(AudioComponents, ListenersSurviveSceneFiles) {
    scene::ComponentSerializers serializers = scene::ComponentSerializers::withSceneComponents();
    addComponentSerializers(serializers);
    scene::Scene source("source");
    source.world().emplace<AudioListener>(source.createEntity("ears"));
    const std::string text = serializers.save(source.world());
    scene::Scene loaded("loaded");
    const auto entities = serializers.load(loaded.world(), text);
    ASSERT_TRUE(entities.has_value()) << entities.error().message;
    EXPECT_TRUE(loaded.world().has<AudioListener>(entities->front()));
}

TEST(AudioComponents, SourcesRoundTripThroughSceneFilesBySoundName) {
    const auto rain = constant(0.25f, 48000);
    SoundNames sounds;
    sounds.name(rain, "sounds/rain");
    scene::ComponentSerializers serializers = scene::ComponentSerializers::withSceneComponents();
    addComponentSerializers(serializers);
    addAudioSourceSerializer(serializers, sounds);

    Rig rig;
    const ecs::Entity roof = rig.scene.createEntity("roof");
    // Every setting off its default.
    AudioSource& source = rig.world.emplace<AudioSource>(
        roof, rain,
        PlayParams{.volume = 0.5f, .pitch = 1.5f, .pan = -0.25f, .loop = true, .spatial = false,
                   .minDistance = 2.0f, .maxDistance = 30.0f, .bus = Bus::Ambience},
        false);
    source.started = true;  // runtime state: not saved
    const std::string text = serializers.save(rig.world);
    EXPECT_NE(text.find("AudioSource.sound = sounds/rain"), std::string::npos);
    EXPECT_NE(text.find("AudioSource.bus = ambience"), std::string::npos);

    Rig loaded;
    ASSERT_TRUE(serializers.load(loaded.world, text).has_value());
    AudioSource& copy = loaded.world.get<AudioSource>(loaded.scene.find("roof"));
    EXPECT_EQ(copy.clip, rain);
    EXPECT_EQ(copy.params.volume, 0.5f);
    EXPECT_EQ(copy.params.pitch, 1.5f);
    EXPECT_EQ(copy.params.pan, -0.25f);
    EXPECT_TRUE(copy.params.loop);
    EXPECT_FALSE(copy.params.spatial);
    EXPECT_EQ(copy.params.minDistance, 2.0f);
    EXPECT_EQ(copy.params.maxDistance, 30.0f);
    EXPECT_EQ(copy.params.bus, Bus::Ambience);
    EXPECT_FALSE(copy.autoplay);
    EXPECT_FALSE(copy.started);
    EXPECT_EQ(copy.voice, 0u);

    // Told to play, the loaded source is heard, panned a little left.
    restart(copy);
    loaded.frame();
    EXPECT_EQ(loaded.mixer->voiceCount(), 1u);
    const auto [left, right] = listen(*loaded.mixer);
    EXPECT_GT(left, right);
    EXPECT_GT(right, 0.0f);

    // A sound nobody named, a name nobody gave, a bus that does not exist.
    Rig stray;
    stray.world.emplace<AudioSource>(stray.scene.createEntity("stray"), constant(0.1f, 10));
    Rig broken;
    EXPECT_FALSE(serializers.load(broken.world, serializers.save(stray.world)).has_value());
    SoundNames none;
    scene::ComponentSerializers strangers = scene::ComponentSerializers::withSceneComponents();
    addAudioSourceSerializer(strangers, none);
    const auto unknown = strangers.load(broken.world, text);
    ASSERT_FALSE(unknown.has_value());
    EXPECT_NE(unknown.error().message.find("sounds/rain"), std::string::npos);
    std::string badBus = text;
    badBus.replace(badBus.find("= ambience"), 10, "= drums");
    const auto drums = serializers.load(broken.world, badBus);
    ASSERT_FALSE(drums.has_value());
    EXPECT_NE(drums.error().message.find("drums"), std::string::npos);
    EXPECT_EQ(broken.world.entityCount(), 0u);
}
