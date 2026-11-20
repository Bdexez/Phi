#include <bdex/audio/Mixer.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

using namespace bdex;
using namespace bdex::audio;

namespace {

std::shared_ptr<const assets::AudioData> constant(f32 value, usize frames, u32 rate = 48000, u32 channels = 1) {
    auto clip = std::make_shared<assets::AudioData>();
    clip->sampleRate = rate;
    clip->channels = channels;
    clip->samples.assign(frames * channels, value);
    return clip;
}

/// Mixes `frames` frames and returns them, interleaved.
std::vector<f32> render(Mixer& mixer, usize frames) {
    std::vector<f32> out(frames * 2);
    mixer.mix(out);
    return out;
}

/// Rising zero crossings of one channel, per second.
f64 frequencyOf(const std::vector<f32>& stereo, u32 channel, u32 rate) {
    int crossings = 0;
    for (usize i = 2 + channel; i < stereo.size(); i += 2) {
        crossings += stereo[i - 2] < 0.0f && stereo[i] >= 0.0f;
    }
    return crossings * static_cast<f64>(rate) / static_cast<f64>(stereo.size() / 2);
}

const f32 kCenter = std::cos(std::numbers::pi_v<f32> / 4.0f);  // equal-power center gain

}  // namespace

TEST(Mixer, PansMonoWithEqualPowerAndBalancesStereo) {
    Mixer mixer;
    mixer.play(constant(0.5f, 100));
    std::vector<f32> out = render(mixer, 4);
    EXPECT_FLOAT_EQ(out[0], 0.5f * kCenter);
    EXPECT_FLOAT_EQ(out[1], 0.5f * kCenter);

    mixer.stopAll(0.0f);  // at once: the next checks hear only the new voice
    mixer.play(constant(0.5f, 100), {.pan = -1.0f});
    out = render(mixer, 4);
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_NEAR(out[1], 0.0f, 1e-7f);

    // Stereo clips keep both channels as they are when centered.
    mixer.stopAll(0.0f);  // at once: the next checks hear only the new voice
    auto stereo = std::make_shared<assets::AudioData>();
    stereo->sampleRate = 48000;
    stereo->channels = 2;
    stereo->samples = {0.25f, -0.5f, 0.25f, -0.5f};
    mixer.play(stereo, {.volume = 2.0f});
    out = render(mixer, 1);
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[1], -1.0f);
}

TEST(Mixer, VoicesSumThenClipAndFinishedOnesGo) {
    Mixer mixer;
    const VoiceId a = mixer.play(constant(0.4f, 10), {.pan = -1.0f});
    const VoiceId b = mixer.play(constant(0.4f, 20), {.pan = -1.0f});
    EXPECT_NE(a, 0u);
    EXPECT_NE(a, b);
    EXPECT_EQ(mixer.voiceCount(), 2u);
    std::vector<f32> out = render(mixer, 15);
    EXPECT_FLOAT_EQ(out[0], 0.8f);
    EXPECT_FLOAT_EQ(out[2 * 12], 0.4f);  // the first one ended at frame 10
    EXPECT_FALSE(mixer.playing(a));
    EXPECT_TRUE(mixer.playing(b));
    out = render(mixer, 10);
    EXPECT_FLOAT_EQ(out[2 * 4], 0.4f);
    EXPECT_FLOAT_EQ(out[2 * 6], 0.0f);
    EXPECT_EQ(mixer.voiceCount(), 0u);

    mixer.play(constant(0.9f, 10), {.pan = -1.0f});
    mixer.play(constant(0.9f, 10), {.pan = -1.0f});
    mixer.setMasterVolume(0.5f);
    out = render(mixer, 2);
    EXPECT_FLOAT_EQ(out[0], 0.9f);  // master volume, then clipping
    mixer.setMasterVolume(1.0f);
    mixer.play(constant(0.9f, 10), {.pan = -1.0f});
    out = render(mixer, 2);
    EXPECT_FLOAT_EQ(out[0], 1.0f);

    EXPECT_EQ(mixer.play(nullptr), 0u);
    EXPECT_EQ(mixer.play(std::make_shared<assets::AudioData>()), 0u);
}

TEST(Mixer, ResamplesClipsAndAppliesPitch) {
    Mixer mixer(48000);
    // A 24 kHz clip lasts as long on a 48 kHz mixer, at the same pitch.
    const auto tone = std::make_shared<assets::AudioData>(assets::makeTone(440.0f, 1.0f, 24000));
    const VoiceId voice = mixer.play(tone);
    std::vector<f32> out = render(mixer, 47000);
    EXPECT_TRUE(mixer.playing(voice));
    EXPECT_NEAR(frequencyOf(out, 0, 48000), 440.0, 3.0);
    render(mixer, 2000);
    EXPECT_FALSE(mixer.playing(voice));

    // Pitch 2: an octave up, half as long.
    const VoiceId fast = mixer.play(tone, {.pitch = 2.0f});
    out = render(mixer, 23000);
    EXPECT_NEAR(frequencyOf(out, 1, 48000), 880.0, 6.0);
    render(mixer, 2000);
    EXPECT_FALSE(mixer.playing(fast));
}

TEST(Mixer, LoopsUntilStopped) {
    Mixer mixer;
    auto ramp = std::make_shared<assets::AudioData>();
    ramp->sampleRate = 48000;
    ramp->channels = 1;
    ramp->samples = {0.0f, 0.25f, 0.5f, 0.75f};
    const VoiceId voice = mixer.play(ramp, {.pan = -1.0f, .loop = true});
    const std::vector<f32> out = render(mixer, 10);
    for (usize frame = 0; frame < 10; ++frame) {
        EXPECT_FLOAT_EQ(out[frame * 2], ramp->samples[frame % 4]) << frame;
    }
    EXPECT_TRUE(mixer.playing(voice));
    mixer.stop(voice, 0.0f);
    EXPECT_FALSE(mixer.playing(voice));
    EXPECT_FLOAT_EQ(render(mixer, 1)[0], 0.0f);
}

TEST(Mixer, GainsGlideAndStopsFadeInsteadOfClicking) {
    Mixer mixer(48000);
    const VoiceId voice = mixer.play(constant(1.0f, 48000), {.pan = -1.0f, .loop = true});
    EXPECT_FLOAT_EQ(render(mixer, 100)[0], 1.0f);  // a new voice starts at its gain

    // Dropping the volume to 0 does not jump: it ramps over the buffer.
    mixer.setVolume(voice, 0.0f);
    std::vector<f32> out = render(mixer, 100);
    EXPECT_NEAR(out[0], 0.99f, 1e-5f);
    EXPECT_NEAR(out[2 * 49], 0.5f, 1e-5f);
    EXPECT_NEAR(out[2 * 99], 0.0f, 1e-6f);
    for (usize frame = 1; frame < 100; ++frame) {
        EXPECT_LT(out[frame * 2], out[(frame - 1) * 2]) << frame;
    }

    // Stopping fades out over 5 ms (240 frames at 48 kHz), then the voice
    // is gone; it no longer counts as playing from the moment it stops.
    mixer.setVolume(voice, 1.0f);
    render(mixer, 100);
    mixer.stop(voice);
    EXPECT_FALSE(mixer.playing(voice));
    EXPECT_EQ(mixer.voiceCount(), 0u);
    mixer.setVolume(voice, 1.0f);  // a stopping voice takes no more changes
    out = render(mixer, 300);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_NEAR(out[2 * 120], 0.5f, 0.01f);
    for (usize frame = 1; frame < 240; ++frame) {
        EXPECT_LT(out[frame * 2], out[(frame - 1) * 2]) << frame;
    }
    EXPECT_FLOAT_EQ(out[2 * 240], 0.0f);
    EXPECT_FLOAT_EQ(out[2 * 299], 0.0f);

    // A voice never heard yet stops at once: there is nothing to fade.
    const VoiceId unheard = mixer.play(constant(1.0f, 100));
    mixer.stop(unheard);
    EXPECT_FLOAT_EQ(render(mixer, 1)[0], 0.0f);
}

TEST(Mixer, PlacesVoicesAroundTheListener) {
    Mixer mixer;
    const auto clip = constant(1.0f, 1000);
    PlayParams params;
    params.spatial = true;
    params.minDistance = 2.0f;
    params.maxDistance = 20.0f;

    // To the right, within the full-volume radius: right channel only.
    params.position = Vec3(1.0f, 0.0f, 0.0f);
    VoiceId voice = mixer.play(clip, params);
    std::vector<f32> out = render(mixer, 1);
    EXPECT_NEAR(out[0], 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(out[1], 1.0f);

    // Ahead at twice the radius: half as loud, centered.
    mixer.setPosition(voice, Vec3(0.0f, 0.0f, -4.0f));
    out = render(mixer, 1);
    EXPECT_FLOAT_EQ(out[0], 0.5f * kCenter);
    EXPECT_FLOAT_EQ(out[1], 0.5f * kCenter);

    // Beyond the maximum distance: silent, though still playing.
    mixer.setPosition(voice, Vec3(0.0f, 0.0f, -25.0f));
    EXPECT_FLOAT_EQ(render(mixer, 1)[0], 0.0f);
    EXPECT_TRUE(mixer.playing(voice));

    // The listener turns around: what was on its right is on its left.
    mixer.setPosition(voice, Vec3(1.0f, 0.0f, 0.0f));
    mixer.setListener(Vec3(0.0f), glm::angleAxis(std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f)));
    out = render(mixer, 1);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_NEAR(out[1], 0.0f, 1e-6f);
}

TEST(Mixer, IsDrivenFromAnotherThreadWhileMixing) {
    Mixer mixer;
    const auto clip = constant(0.01f, 64);
    std::atomic<bool> done = false;
    std::thread audioThread([&] {
        std::vector<f32> buffer(256 * 2);
        while (!done) {
            mixer.mix(buffer);
            std::this_thread::sleep_for(std::chrono::microseconds(200));  // as a device asks for buffers
        }
    });
    std::vector<VoiceId> voices;
    for (int i = 0; i < 300; ++i) {
        voices.push_back(mixer.play(clip, {.volume = 0.5f, .loop = i % 3 == 0}));
        mixer.setVolume(voices[static_cast<usize>(i) / 2], 0.25f);
        if (i % 5 == 0) {
            mixer.stop(voices[static_cast<usize>(i) / 3]);
        }
    }
    done = true;
    audioThread.join();
    mixer.stopAll(0.0f);  // at once: the next checks hear only the new voice
    EXPECT_EQ(mixer.voiceCount(), 0u);
}

TEST(Mixer, BusesScaleTheirVoices) {
    Mixer mixer;
    mixer.play(constant(0.5f, 1000), {.pan = -1.0f, .bus = Bus::Music});
    mixer.play(constant(0.25f, 1000), {.pan = 1.0f});  // effects, by default
    mixer.setBusVolume(Bus::Music, 0.5f);
    std::vector<f32> out = render(mixer, 1);
    EXPECT_FLOAT_EQ(out[0], 0.25f);  // music, halved
    EXPECT_FLOAT_EQ(out[1], 0.25f);  // effects, untouched
    EXPECT_FLOAT_EQ(mixer.busVolume(Bus::Music), 0.5f);
    EXPECT_FLOAT_EQ(mixer.busVolume(Bus::Effects), 1.0f);

    mixer.setBusVolume(Bus::Effects, 0.0f);  // muted
    out = render(mixer, 1);
    EXPECT_FLOAT_EQ(out[1], 0.0f);

    EXPECT_EQ(busName(Bus::Ambience), "ambience");
    EXPECT_EQ(busNamed("voice"), Bus::Voice);
    EXPECT_FALSE(busNamed("drums").has_value());
}
