#pragma once

#include <phi/core/Result.hpp>
#include <phi/core/Types.hpp>

#include <span>
#include <vector>

namespace phi::assets {

/// CPU-side sound: interleaved 32-bit float samples in [-1, 1], one per
/// channel per frame (left then right for stereo).
struct AudioData {
    u32 sampleRate = 0;
    u32 channels = 0;  ///< 1 (mono) or 2 (stereo).
    std::vector<f32> samples;

    [[nodiscard]] usize frameCount() const { return channels == 0 ? 0 : samples.size() / channels; }
    [[nodiscard]] f64 seconds() const {
        return sampleRate == 0 ? 0.0 : static_cast<f64>(frameCount()) / sampleRate;
    }
    [[nodiscard]] bool valid() const {
        return sampleRate != 0 && (channels == 1 || channels == 2) && samples.size() % channels == 0;
    }
};

/// Decodes a RIFF/WAVE file: integer PCM (8, 16, 24 or 32 bits) or 32-bit
/// float, mono or stereo, including WAVE_FORMAT_EXTENSIBLE headers.
/// Unknown chunks are skipped; a truncated last frame is dropped.
[[nodiscard]] Result<AudioData> decodeWav(std::span<const u8> bytes);

/// Encodes `audio` as 16-bit PCM WAV (samples are clamped to [-1, 1]).
[[nodiscard]] std::vector<u8> encodeWav(const AudioData& audio);

/// A sine wave, mono: a sound to use without asset files. The first and
/// last few milliseconds fade, so it starts and stops without a click.
[[nodiscard]] AudioData makeTone(f32 frequency, f32 seconds, u32 sampleRate = 48000, f32 amplitude = 0.5f);

}  // namespace phi::assets
