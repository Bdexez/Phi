#include <phi/audio/Mixer.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phi::audio {

namespace {
constexpr std::array<std::string_view, kBusCount> kBusNames = {"effects", "music", "voice", "ambience"};
}  // namespace

std::string_view busName(Bus bus) {
    return kBusNames[static_cast<usize>(bus) % kBusCount];
}

std::optional<Bus> busNamed(std::string_view name) {
    for (usize i = 0; i < kBusCount; ++i) {
        if (kBusNames[i] == name) {
            return static_cast<Bus>(i);
        }
    }
    return std::nullopt;
}

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
    const auto it = std::find_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) { return v.id == voice && !v.stopping(); });
    return it == voices_.end() ? nullptr : &*it;
}

namespace {

/// Frames a fade of `seconds` lasts, at least one; 0 for no fade.
usize fadeLength(f32 seconds, u32 rate) {
    return std::isfinite(seconds) && seconds > 0.0f
               ? std::max<usize>(1, static_cast<usize>(std::min(seconds, 60.0f) * static_cast<f32>(rate)))
               : 0;
}

/// Mixes one voice into interleaved stereo `out`, with channel layout and
/// stopping/looping behavior hoisted out of the per-sample loop.
template <bool Stereo, bool Stopping, bool Loop>
void mixVoiceSamples(const f32* samples, usize length, f64& cursor, f64 step,
                     usize& fadeLeft, f32 invFadeFrames, f32 gainLeft, f32 gainRight,
                     f32 stepLeft, f32 stepRight, f32* out, usize frames) {
    const f64 lengthF = static_cast<f64>(length);
    for (usize frame = 0; frame < frames; ++frame) {
        f32 fade = 1.0f;
        if constexpr (Stopping) {
            if (fadeLeft == 0) {
                break;
            }
            fade = static_cast<f32>(fadeLeft) * invFadeFrames;
            --fadeLeft;
        }
        if (cursor >= lengthF) {
            if constexpr (!Loop) {
                break;
            } else {
                cursor = std::fmod(cursor, lengthF);
            }
        }
        // Linear interpolation between the two clip frames around the cursor;
        // past the end it blends toward the start when looping.
        const usize index = static_cast<usize>(cursor);
        const usize next = index + 1 < length ? index + 1 : (Loop ? 0 : index);
        const f32 t = static_cast<f32>(cursor - static_cast<f64>(index));
        gainLeft += stepLeft;
        gainRight += stepRight;
        if constexpr (Stereo) {
            const usize i0 = index * 2;
            const usize i1 = next * 2;
            const f32 left = samples[i0] + (samples[i1] - samples[i0]) * t;
            const f32 right = samples[i0 + 1] + (samples[i1 + 1] - samples[i0 + 1]) * t;
            out[frame * 2] += left * gainLeft * fade;
            out[frame * 2 + 1] += right * gainRight * fade;
        } else {
            const f32 value = (samples[index] + (samples[next] - samples[index]) * t) * fade;
            out[frame * 2] += value * gainLeft;
            out[frame * 2 + 1] += value * gainRight;
        }
        cursor += step;
    }
}

void dispatchMixVoice(const assets::AudioData& clip, f64& cursor, f64 step,
                      bool stopping, usize& fadeLeft, usize fadeFrames, bool loop,
                      f32 gainLeft, f32 gainRight, f32 stepLeft, f32 stepRight,
                      f32* out, usize frames) {
    const f32* samples = clip.samples.data();
    const usize length = clip.frameCount();
    const f32 invFade = stopping ? 1.0f / static_cast<f32>(fadeFrames) : 0.0f;
    const auto dispatchLoop = [&]<bool Stereo, bool Stopping>() {
        if (loop) {
            mixVoiceSamples<Stereo, Stopping, true>(samples, length, cursor, step, fadeLeft, invFade,
                                                    gainLeft, gainRight, stepLeft, stepRight, out, frames);
        } else {
            mixVoiceSamples<Stereo, Stopping, false>(samples, length, cursor, step, fadeLeft, invFade,
                                                     gainLeft, gainRight, stepLeft, stepRight, out, frames);
        }
    };
    const auto dispatchStopping = [&]<bool Stereo>() {
        if (stopping) {
            dispatchLoop.template operator()<Stereo, true>();
        } else {
            dispatchLoop.template operator()<Stereo, false>();
        }
    };
    if (clip.channels == 2) {
        dispatchStopping.template operator()<true>();
    } else {
        dispatchStopping.template operator()<false>();
    }
}

}  // namespace

void Mixer::stop(VoiceId voice, f32 fade) {
    std::shared_ptr<const assets::AudioData> dropped;
    {
        const std::lock_guard lock(mutex_);
        const usize frames = fadeLength(fade, sampleRate_);
        std::erase_if(voices_, [&](Voice& v) {
            if (v.id != voice || v.stopping()) {
                return false;
            }
            if (frames == 0 || !v.mixed) {
                dropped = std::move(v.clip);
                return true;  // cut at once, or never heard yet
            }
            v.fadeFrames = v.fadeLeft = frames;
            return false;
        });
    }
}

void Mixer::stopAll(f32 fade) {
    std::vector<std::shared_ptr<const assets::AudioData>> dropped;
    {
        const std::lock_guard lock(mutex_);
        const usize frames = fadeLength(fade, sampleRate_);
        std::erase_if(voices_, [&](Voice& v) {
            if (frames == 0 || !v.mixed) {
                dropped.push_back(std::move(v.clip));
                return true;
            }
            if (!v.stopping()) {
                v.fadeFrames = v.fadeLeft = frames;
            }
            return false;
        });
    }
}

bool Mixer::playing(VoiceId voice) const {
    const std::lock_guard lock(mutex_);
    return std::any_of(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == voice && !v.stopping(); });
}

usize Mixer::voiceCount() const {
    const std::lock_guard lock(mutex_);
    return static_cast<usize>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return !v.stopping(); }));
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

void Mixer::setBusVolume(Bus bus, f32 volume) {
    const std::lock_guard lock(mutex_);
    busVolumes_[static_cast<usize>(bus) % kBusCount] = volume;
}

f32 Mixer::busVolume(Bus bus) const {
    const std::lock_guard lock(mutex_);
    return busVolumes_[static_cast<usize>(bus) % kBusCount];
}

void Mixer::setBusSpeed(Bus bus, f32 speed) {
    const std::lock_guard lock(mutex_);
    busSpeeds_[static_cast<usize>(bus) % kBusCount] = std::isfinite(speed) ? std::clamp(speed, 0.0f, 16.0f) : 1.0f;
}

f32 Mixer::busSpeed(Bus bus) const {
    const std::lock_guard lock(mutex_);
    return busSpeeds_[static_cast<usize>(bus) % kBusCount];
}

std::pair<f32, f32> Mixer::gains(const Voice& voice) const {
    const PlayParams& params = voice.params;
    const f32 bus = busVolumes_[static_cast<usize>(params.bus) % kBusCount];
    f32 volume = std::isfinite(params.volume) && std::isfinite(bus) ? std::max(params.volume * bus, 0.0f) : 0.0f;
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
    std::vector<std::shared_ptr<const assets::AudioData>> finishedClips;
    f32 master = 0.0f;
    {
        const std::lock_guard lock(mutex_);
        if (voices_.empty()) {
            return;
        }
        for (Voice& voice : voices_) {
            const assets::AudioData& clip = *voice.clip;
            const f32 busSpeed = busSpeeds_[static_cast<usize>(voice.params.bus) % kBusCount];
            const bool paused = busSpeed <= 0.0f;
            // A paused voice glides to silence over this buffer, holding its place.
            const auto [left, right] = paused ? std::pair(0.0f, 0.0f) : gains(voice);
            // Glide from the last buffer's gains to these over this buffer.
            const f32 startLeft = voice.mixed ? voice.gainLeft : left;
            const f32 startRight = voice.mixed ? voice.gainRight : right;
            voice.gainLeft = left;
            voice.gainRight = right;
            voice.mixed = true;
            const f32 stepLeft = (left - startLeft) / static_cast<f32>(std::max<usize>(frames, 1));
            const f32 stepRight = (right - startRight) / static_cast<f32>(std::max<usize>(frames, 1));
            const f32 pitch =
                std::isfinite(voice.params.pitch) ? std::clamp(voice.params.pitch * busSpeed, 0.01f, 16.0f) : 1.0f;
            const f64 step = paused ? 0.0 : static_cast<f64>(clip.sampleRate) / sampleRate_ * pitch;
            if (paused && startLeft == 0.0f && startRight == 0.0f) {
                if (voice.stopping()) {
                    voice.fadeLeft = 0;  // stopped while silent: gone at once
                }
                continue;  // silent already: nothing to fade
            }
            dispatchMixVoice(clip, voice.cursor, step, voice.stopping(), voice.fadeLeft, voice.fadeFrames,
                             voice.params.loop, startLeft, startRight, stepLeft, stepRight, out.data(), frames);
        }
        // Release finished clips after unlocking mutex_ so deallocation never
        // blocks the game thread or stalls the audio lock.
        std::erase_if(voices_, [&](Voice& voice) {
            const bool done = (voice.stopping() && voice.fadeLeft == 0) ||
                              (!voice.params.loop && voice.cursor >= static_cast<f64>(voice.clip->frameCount()));
            if (done) {
                finishedClips.push_back(std::move(voice.clip));
            }
            return done;
        });
        master = std::isfinite(masterVolume_) ? std::max(masterVolume_, 0.0f) : 0.0f;
    }
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

}  // namespace phi::audio
