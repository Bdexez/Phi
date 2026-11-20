#pragma once

#include <bdex/assets/Audio.hpp>
#include <bdex/core/Math.hpp>
#include <bdex/core/Types.hpp>

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bdex::audio {

/// Identifies a playing sound; 0 is never used.
using VoiceId = u64;

/// Groups of voices sharing a volume, as a game's sound settings offer.
enum class Bus : u8 { Effects, Music, Voice, Ambience };
inline constexpr usize kBusCount = 4;

/// "effects", "music", "voice", "ambience".
[[nodiscard]] std::string_view busName(Bus bus);
[[nodiscard]] std::optional<Bus> busNamed(std::string_view name);

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
    Bus bus = Bus::Effects;
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
    /// Cutting a sound mid-wave clicks: stopping fades it out over this.
    static constexpr f32 kStopFade = 0.005f;

    /// Stops a voice, fading it out over `fade` seconds (0 cuts it at
    /// once); unknown or finished ones are ignored.
    void stop(VoiceId voice, f32 fade = kStopFade);
    void stopAll(f32 fade = kStopFade);
    /// Whether the voice is still playing: not finished, not stopped (even
    /// while it fades out).
    [[nodiscard]] bool playing(VoiceId voice) const;
    /// Voices playing, those fading out after stop() excluded.
    [[nodiscard]] usize voiceCount() const;

    void setVolume(VoiceId voice, f32 volume);
    void setPitch(VoiceId voice, f32 pitch);
    void setPosition(VoiceId voice, const Vec3& position);
    /// Replaces a playing voice's volume, pitch, pan, position and distances
    /// at once (its looping is kept).
    void update(VoiceId voice, const PlayParams& params);

    /// Where the listener is and which way it faces (its -Z is forward, +X
    /// right, as for cameras).
    void setListener(const Vec3& position, const Quat& orientation);
    void setMasterVolume(f32 volume);
    /// Scales every voice of a bus, on top of their own volume (1 by
    /// default); changes glide like any other.
    void setBusVolume(Bus bus, f32 volume);
    [[nodiscard]] f32 busVolume(Bus bus) const;
    /// Plays every voice of a bus this many times faster, on top of their
    /// own pitch (1 by default), so it can follow slow motion: 0.5 is half
    /// speed and an octave down. 0 pauses the bus: its voices fade out over
    /// a buffer and hold their place, then fade back in when it resumes.
    void setBusSpeed(Bus bus, f32 speed);
    [[nodiscard]] f32 busSpeed(Bus bus) const;

    /// Mixes the next `out.size() / 2` frames into `out` as interleaved
    /// left / right samples in [-1, 1], replacing its contents, and advances
    /// every voice; finished voices are dropped. A voice whose gains changed
    /// (volume, position, listener) glides to them across the buffer rather
    /// than jumping, which would crackle.
    void mix(std::span<f32> out);
    /// Plays `seconds` of audio into the void: voices advance and finish as
    /// if heard. For when no device consumes the mix (headless runs); call
    /// it from one thread at a time.
    void advance(f64 seconds);

private:
    struct Voice {
        VoiceId id = 0;
        std::shared_ptr<const assets::AudioData> clip;
        PlayParams params;
        f64 cursor = 0.0;  ///< Position in the clip, in its frames.
        f32 gainLeft = 0.0f;  ///< Gains at the end of the last buffer...
        f32 gainRight = 0.0f;
        bool mixed = false;   ///< ...once there was one.
        usize fadeFrames = 0;  ///< Stopping: frames the fade lasts, and
        usize fadeLeft = 0;    ///< frames left before silence.
        [[nodiscard]] bool stopping() const { return fadeFrames != 0; }
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
    std::array<f32, kBusCount> busVolumes_{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<f32, kBusCount> busSpeeds_{1.0f, 1.0f, 1.0f, 1.0f};
    f64 advanceRemainder_ = 0.0;  ///< Fraction of a frame carried between advance() calls.
    std::vector<f32> scratch_;
};

}  // namespace bdex::audio
