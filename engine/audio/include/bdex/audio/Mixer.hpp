#pragma once

#include <bdex/assets/Audio.hpp>
#include <bdex/core/Math.hpp>
#include <bdex/core/Types.hpp>

#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace bdex::audio {

/// Identifies a playing sound; 0 is never used.
using VoiceId = u64;

struct PlayParams {
    f32 volume = 1.0f;
    /// Playback speed: 2 plays an octave higher and twice as fast.
    f32 pitch = 1.0f;
    /// Without a position: -1 for left, 1 for right. Ignored for 3D voices.
    f32 pan = 0.0f;
    bool loop = false;
    /// 3D voices are attenuated with their distance to the listener and
    /// panned by their direction from it.
    bool spatial = false;
    Vec3 position{0.0f};
    /// Full volume up to this distance, then volume * minDistance / distance...
    f32 minDistance = 1.0f;
    /// ...and silence beyond this one.
    f32 maxDistance = 50.0f;
};

/// Software mixer: sums any number of voices into one stereo stream, at
/// its own sample rate, whatever the rate of each clip.
///
/// An audio backend calls mix() from its own thread to fill the device's
/// buffers; the game controls voices from any other thread. Both take a
/// mutex, held by mix() only for the time it takes to mix one buffer.
class Mixer {
public:
    explicit Mixer(u32 sampleRate = 48000);

    [[nodiscard]] u32 sampleRate() const { return sampleRate_; }

    /// Starts playing `clip` (shared, so the mixer keeps it alive while it
    /// plays). Returns 0, playing nothing, for an invalid or empty clip.
    VoiceId play(std::shared_ptr<const assets::AudioData> clip, const PlayParams& params = {});
    /// Stops a voice; unknown or finished ones are ignored.
    void stop(VoiceId voice);
    void stopAll();
    /// Whether the voice is still playing (a finished one is not).
    [[nodiscard]] bool playing(VoiceId voice) const;
    [[nodiscard]] usize voiceCount() const;

    void setVolume(VoiceId voice, f32 volume);
    void setPitch(VoiceId voice, f32 pitch);
    void setPosition(VoiceId voice, const Vec3& position);

    /// Where the listener is and which way it faces (its -Z is forward, +X
    /// right, as for cameras).
    void setListener(const Vec3& position, const Quat& orientation);
    void setMasterVolume(f32 volume);

    /// Mixes the next `out.size() / 2` frames into `out` as interleaved
    /// left / right samples in [-1, 1], replacing its contents, and advances
    /// every voice; finished voices are dropped.
    void mix(std::span<f32> out);

private:
    struct Voice {
        VoiceId id = 0;
        std::shared_ptr<const assets::AudioData> clip;
        PlayParams params;
        f64 cursor = 0.0;  ///< Position in the clip, in its frames.
    };

    /// Left and right gains of a voice for the current listener.
    [[nodiscard]] std::pair<f32, f32> gains(const Voice& voice) const;
    [[nodiscard]] Voice* find(VoiceId voice);

    mutable std::mutex mutex_;
    u32 sampleRate_;
    std::vector<Voice> voices_;
    VoiceId nextId_ = 1;
    Vec3 listenerPosition_{0.0f};
    Quat listenerOrientation_{1.0f, 0.0f, 0.0f, 0.0f};
    f32 masterVolume_ = 1.0f;
};

}  // namespace bdex::audio
