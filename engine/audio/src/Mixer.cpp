#include <bdex/audio/Mixer.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bdex::audio {

Mixer::Mixer(u32 sampleRate) : sampleRate_(std::max(sampleRate, 1u)) {}

VoiceId Mixer::play(std::shared_ptr<const assets::AudioData> clip, const PlayParams& params) {
    if (clip == nullptr || !clip->valid() || clip->frameCount() == 0) {
        return 0;
    }
    const std::lock_guard lock(mutex_);
    const VoiceId id = nextId_++;
    voices_.push_back({id, std::move(clip), params, 0.0});
    return id;
}

Mixer::Voice* Mixer::find(VoiceId voice) {
    const auto it = std::find_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == voice; });
    return it == voices_.end() ? nullptr : &*it;
}

void Mixer::stop(VoiceId voice) {
    const std::lock_guard lock(mutex_);
    std::erase_if(voices_, [&](const Voice& v) { return v.id == voice; });
}

void Mixer::stopAll() {
    const std::lock_guard lock(mutex_);
    voices_.clear();
}

bool Mixer::playing(VoiceId voice) const {
    const std::lock_guard lock(mutex_);
    return std::any_of(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == voice; });
}

usize Mixer::voiceCount() const {
    const std::lock_guard lock(mutex_);
    return voices_.size();
}

void Mixer::setVolume(VoiceId voice, f32 volume) {
    const std::lock_guard lock(mutex_);
    if (Voice* found = find(voice)) {
        found->params.volume = volume;
    }
}

void Mixer::setPitch(VoiceId voice, f32 pitch) {
    const std::lock_guard lock(mutex_);
    if (Voice* found = find(voice)) {
        found->params.pitch = pitch;
    }
}

void Mixer::setPosition(VoiceId voice, const Vec3& position) {
    const std::lock_guard lock(mutex_);
    if (Voice* found = find(voice)) {
        found->params.position = position;
    }
}

void Mixer::update(VoiceId voice, const PlayParams& params) {
    const std::lock_guard lock(mutex_);
    if (Voice* found = find(voice)) {
        const bool loop = found->params.loop;
        found->params = params;
        found->params.loop = loop;
    }
}

void Mixer::setListener(const Vec3& position, const Quat& orientation) {
    const std::lock_guard lock(mutex_);
    listenerPosition_ = position;
    listenerOrientation_ = orientation;
}

void Mixer::setMasterVolume(f32 volume) {
    const std::lock_guard lock(mutex_);
    masterVolume_ = volume;
}

std::pair<f32, f32> Mixer::gains(const Voice& voice) const {
    const PlayParams& params = voice.params;
    f32 volume = std::isfinite(params.volume) ? std::max(params.volume, 0.0f) : 0.0f;
    f32 pan = params.pan;
    if (params.spatial) {
        const Vec3 offset = params.position - listenerPosition_;
        const f32 distance = glm::length(offset);
        if (!std::isfinite(distance) || distance > params.maxDistance) {
            return {0.0f, 0.0f};
        }
        const f32 near = std::max(params.minDistance, 1e-3f);
        volume *= near / std::max(distance, near);
        // How far to the listener's right the sound is, from -1 to 1.
        pan = distance > 1e-4f ? glm::dot(offset / distance, listenerOrientation_ * Vec3(1.0f, 0.0f, 0.0f)) : 0.0f;
    }
    pan = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0.0f;
    if (voice.clip->channels == 2) {
        // Stereo clips are balanced: centered, both channels play as is.
        return {volume * std::min(1.0f, 1.0f - pan), volume * std::min(1.0f, 1.0f + pan)};
    }
    // Mono: equal-power panning keeps loudness constant across the field.
    const f32 angle = (pan + 1.0f) * std::numbers::pi_v<f32> / 4.0f;
    return {volume * std::cos(angle), volume * std::sin(angle)};
}

void Mixer::mix(std::span<f32> out) {
    std::fill(out.begin(), out.end(), 0.0f);
    const usize frames = out.size() / 2;
    const std::lock_guard lock(mutex_);
    for (Voice& voice : voices_) {
        const assets::AudioData& clip = *voice.clip;
        const auto [left, right] = gains(voice);
        const f32 pitch = std::isfinite(voice.params.pitch) ? std::clamp(voice.params.pitch, 0.01f, 16.0f) : 1.0f;
        const f64 step = static_cast<f64>(clip.sampleRate) / sampleRate_ * pitch;
        const usize length = clip.frameCount();
        const auto sample = [&](usize frame, u32 channel) {
            return clip.samples[frame * clip.channels + std::min(channel, clip.channels - 1)];
        };
        for (usize frame = 0; frame < frames; ++frame) {
            if (voice.cursor >= static_cast<f64>(length)) {
                if (!voice.params.loop) {
                    break;
                }
                voice.cursor = std::fmod(voice.cursor, static_cast<f64>(length));
            }
            // Linear interpolation between the two clip frames around the
            // cursor; past the end it blends toward the start when looping.
            const usize index = static_cast<usize>(voice.cursor);
            const usize next = index + 1 < length ? index + 1 : (voice.params.loop ? 0 : index);
            const f32 t = static_cast<f32>(voice.cursor - static_cast<f64>(index));
            for (u32 channel = 0; channel < 2; ++channel) {
                const f32 value = sample(index, channel) + (sample(next, channel) - sample(index, channel)) * t;
                out[frame * 2 + channel] += value * (channel == 0 ? left : right);
            }
            voice.cursor += step;
        }
    }
    std::erase_if(voices_, [](const Voice& voice) {
        return !voice.params.loop && voice.cursor >= static_cast<f64>(voice.clip->frameCount());
    });
    const f32 master = std::isfinite(masterVolume_) ? std::max(masterVolume_, 0.0f) : 0.0f;
    for (f32& value : out) {
        value = std::clamp(value * master, -1.0f, 1.0f);
    }
}

void Mixer::advance(f64 seconds) {
    if (!(seconds > 0.0)) {
        return;
    }
    const f64 frames = seconds * sampleRate_ + advanceRemainder_;
    usize remaining = static_cast<usize>(std::min(frames, 1e9));
    advanceRemainder_ = frames - static_cast<f64>(remaining);
    constexpr usize kChunk = 1024;
    while (remaining > 0) {
        const usize chunk = std::min(remaining, kChunk);
        scratch_.resize(chunk * 2);
        mix(scratch_);
        remaining -= chunk;
    }
}

}  // namespace bdex::audio
