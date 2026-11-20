#include <bdex/assets/Audio.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string_view>

namespace bdex::assets {

namespace {

u32 readU32(std::span<const u8> bytes, usize offset) {
    return static_cast<u32>(bytes[offset]) | static_cast<u32>(bytes[offset + 1]) << 8 |
           static_cast<u32>(bytes[offset + 2]) << 16 | static_cast<u32>(bytes[offset + 3]) << 24;
}

u16 readU16(std::span<const u8> bytes, usize offset) {
    return static_cast<u16>(bytes[offset] | bytes[offset + 1] << 8);
}

bool tagIs(std::span<const u8> bytes, usize offset, std::string_view tag) {
    return std::memcmp(bytes.data() + offset, tag.data(), 4) == 0;
}

void writeU32(std::vector<u8>& out, u32 value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<u8>(value >> shift));
    }
}

void writeU16(std::vector<u8>& out, u16 value) {
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
}

constexpr u16 kFormatPcm = 1;
constexpr u16 kFormatFloat = 3;
constexpr u16 kFormatExtensible = 0xfffe;

}  // namespace

Result<AudioData> decodeWav(std::span<const u8> bytes) {
    if (bytes.size() < 12 || !tagIs(bytes, 0, "RIFF") || !tagIs(bytes, 8, "WAVE")) {
        return fail("wav: not a RIFF/WAVE file");
    }

    u16 format = 0;
    u16 channels = 0;
    u32 sampleRate = 0;
    u16 bits = 0;
    std::span<const u8> data;
    bool haveFormat = false;
    bool haveData = false;
    // Chunks: 4-byte tag, 4-byte size, payload padded to an even size.
    for (usize offset = 12; offset + 8 <= bytes.size();) {
        const u32 size = readU32(bytes, offset + 4);
        const usize payload = offset + 8;
        const usize available = bytes.size() - payload;
        if (tagIs(bytes, offset, "fmt ")) {
            if (size < 16 || size > available) {
                return fail("wav: malformed fmt chunk");
            }
            format = readU16(bytes, payload);
            channels = readU16(bytes, payload + 2);
            sampleRate = readU32(bytes, payload + 4);
            bits = readU16(bytes, payload + 14);
            if (format == kFormatExtensible) {
                // The real format is the first two bytes of the sub-format GUID.
                if (size < 26) {
                    return fail("wav: malformed extensible fmt chunk");
                }
                format = readU16(bytes, payload + 24);
            }
            haveFormat = true;
        } else if (tagIs(bytes, offset, "data")) {
            // Some writers leave the size at its maximum when streaming: take
            // what the file holds.
            data = bytes.subspan(payload, std::min<usize>(size, available));
            haveData = true;
        }
        if (size > available) {
            break;
        }
        offset = payload + size + (size & 1u);
    }
    if (!haveFormat || !haveData) {
        return fail("wav: missing {} chunk", haveFormat ? "data" : "fmt");
    }
    if (channels != 1 && channels != 2) {
        return fail("wav: {} channels (only mono and stereo are supported)", channels);
    }
    if (sampleRate == 0 || sampleRate > 768000) {
        return fail("wav: invalid sample rate {}", sampleRate);
    }
    const bool integer = format == kFormatPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool floating = format == kFormatFloat && bits == 32;
    if (!integer && !floating) {
        return fail("wav: unsupported sample format {} with {} bits", format, bits);
    }

    AudioData audio;
    audio.sampleRate = sampleRate;
    audio.channels = channels;
    const usize width = bits / 8u;
    const usize frames = data.size() / (width * channels);  // a partial frame is dropped
    audio.samples.resize(frames * channels);
    for (usize i = 0; i < audio.samples.size(); ++i) {
        const u8* sample = data.data() + i * width;
        f32 value = 0.0f;
        if (floating) {
            u32 raw = readU32(data, i * width);
            std::memcpy(&value, &raw, sizeof(value));
            value = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
        } else if (bits == 8) {
            value = (static_cast<f32>(sample[0]) - 128.0f) / 128.0f;  // 8-bit is unsigned
        } else {
            // Little-endian signed integers: build the top bits of an i32.
            u32 raw = 0;
            for (usize byte = 0; byte < width; ++byte) {
                raw |= static_cast<u32>(sample[byte]) << (8 * (4 - width + byte));
            }
            value = static_cast<f32>(static_cast<f64>(static_cast<i32>(raw)) / 2147483648.0);
        }
        audio.samples[i] = value;
    }
    return audio;
}

std::vector<u8> encodeWav(const AudioData& audio) {
    const u32 channels = audio.channels == 0 ? 1 : audio.channels;
    const u32 dataSize = static_cast<u32>(audio.samples.size() * 2);
    std::vector<u8> out;
    out.reserve(44 + dataSize);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    writeU32(out, 36 + dataSize);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    writeU32(out, 16);
    writeU16(out, kFormatPcm);
    writeU16(out, static_cast<u16>(channels));
    writeU32(out, audio.sampleRate);
    writeU32(out, audio.sampleRate * channels * 2);  // bytes per second
    writeU16(out, static_cast<u16>(channels * 2));   // bytes per frame
    writeU16(out, 16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    writeU32(out, dataSize);
    for (const f32 sample : audio.samples) {
        const f32 clamped = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
        writeU16(out, static_cast<u16>(static_cast<i16>(std::lround(clamped * 32767.0f))));
    }
    return out;
}

AudioData makeTone(f32 frequency, f32 seconds, u32 sampleRate, f32 amplitude) {
    AudioData audio;
    audio.sampleRate = sampleRate;
    audio.channels = 1;
    const usize frames = static_cast<usize>(std::max(seconds, 0.0f) * static_cast<f32>(sampleRate));
    audio.samples.resize(frames);
    const usize fade = std::min<usize>(sampleRate / 200, frames / 2);  // 5 ms
    const f64 step = 2.0 * std::numbers::pi * frequency / sampleRate;
    for (usize i = 0; i < frames; ++i) {
        f32 envelope = 1.0f;
        if (fade > 0) {
            envelope = std::min({1.0f, static_cast<f32>(i) / fade, static_cast<f32>(frames - 1 - i) / fade});
        }
        audio.samples[i] = amplitude * envelope * static_cast<f32>(std::sin(step * static_cast<f64>(i)));
    }
    return audio;
}

}  // namespace bdex::assets
