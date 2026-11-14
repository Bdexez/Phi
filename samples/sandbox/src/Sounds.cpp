#include "Sounds.hpp"

#include <phi/audio/Mixer.hpp>
#include <phi/physics/Physics.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sandbox {

using namespace phi;

void Sounds::start(Engine& engine) {
    if (!engine.services().has<audio::Mixer>()) {
        return;
    }
    constexpr u32 kRate = 48000;
    const auto shaped = [&](f32 seconds, auto&& sample) {
        auto sound = std::make_shared<assets::AudioData>();
        sound->sampleRate = kRate;
        sound->channels = 1;
        sound->samples.resize(static_cast<usize>(seconds * kRate));
        for (usize i = 0; i < sound->samples.size(); ++i) {
            sound->samples[i] = sample(static_cast<f32>(i) / kRate);
        }
        return sound;
    };
    const f32 tau = 2.0f * std::numbers::pi_v<f32>;
    // A rising chirp, faded in and out.
    throw_ = shaped(0.16f, [&](f32 t) {
        const f32 envelope = std::sin(std::numbers::pi_v<f32> * t / 0.16f);
        return 0.3f * envelope * std::sin(tau * (250.0f * t + 1400.0f * t * t));
    });
    // A low tone with a click of noise, dying away quickly.
    u32 noise = 12345;
    impact_ = shaped(0.3f, [&](f32 t) {
        noise = noise * 1664525u + 1013904223u;
        const f32 grain = static_cast<f32>(noise >> 8) / static_cast<f32>(1u << 24) * 2.0f - 1.0f;
        return std::exp(-t * 16.0f) * (0.7f * std::sin(tau * 150.0f * t) + 0.3f * grain * std::exp(-t * 60.0f));
    });
    collisions_ = engine.events().subscribe<physics::Collision>([this, &engine](const physics::Collision& event) {
        auto* mixer = engine.services().tryGet<audio::Mixer>();
        if (mixer == nullptr || mixer->voiceCount() >= 24) {
            return;  // a pile of balls settling would make a racket
        }
        const physics::Contact& contact = event.contact;
        mixer->play(impact_, {.volume = std::clamp(contact.impulse / 6.0f, 0.05f, 1.0f),
                              // Heavier hits sound a little lower.
                              .pitch = std::clamp(1.3f - contact.impulse / 30.0f, 0.7f, 1.3f),
                              .spatial = true,
                              .position = contact.point,
                              .minDistance = 3.0f,
                              .maxDistance = 60.0f});
    });
}

void Sounds::playThrow(Engine& engine) const {
    if (auto* mixer = engine.services().tryGet<audio::Mixer>(); mixer != nullptr && throw_) {
        mixer->play(throw_, {.volume = 0.4f});
    }
}

}  // namespace sandbox
