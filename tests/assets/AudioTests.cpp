#include <phi/assets/Audio.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace phi;
using namespace phi::assets;

namespace {

/// Builds a WAV file by hand, so the decoder is checked against the format
/// rather than against our own encoder.
struct WavBuilder {
    std::vector<u8> bytes;

    void tag(const char* text) { bytes.insert(bytes.end(), text, text + 4); }
    void u32le(u32 value) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<u8>(value >> shift));
    }
    void u16le(u16 value) {
        bytes.push_back(static_cast<u8>(value));
        bytes.push_back(static_cast<u8>(value >> 8));
    }
    void format(u16 tagValue, u16 channels, u32 rate, u16 bits, bool extensible = false) {
        tag("fmt ");
        u32le(extensible ? 40 : 16);
        u16le(extensible ? 0xfffe : tagValue);
        u16le(channels);
        u32le(rate);
        u32le(rate * channels * bits / 8);
        u16le(static_cast<u16>(channels * bits / 8));
        u16le(bits);
        if (extensible) {
            u16le(22);  // extension size
            u16le(bits);
            u32le(3);   // channel mask
            u16le(tagValue);
            for (int i = 0; i < 14; ++i) bytes.push_back(0);  // rest of the GUID
        }
    }
    void data(const std::vector<u8>& payload, u32 declared = 0) {
        tag("data");
        u32le(declared != 0 ? declared : static_cast<u32>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    std::vector<u8> finish() {
        std::vector<u8> file = {'R', 'I', 'F', 'F'};
        const u32 size = static_cast<u32>(bytes.size() + 4);
        for (int shift = 0; shift < 32; shift += 8) file.push_back(static_cast<u8>(size >> shift));
        file.insert(file.end(), {'W', 'A', 'V', 'E'});
        file.insert(file.end(), bytes.begin(), bytes.end());
        return file;
    }
};

}  // namespace

TEST(Wav, DecodesEveryIntegerWidth) {
    // The same three samples, -1, 0 and almost +1, at each width.
    struct Case {
        u16 bits;
        std::vector<u8> payload;
    };
    const std::vector<Case> cases = {
        {8, {0x00, 0x80, 0xff}},  // unsigned
        {16, {0x00, 0x80, 0x00, 0x00, 0xff, 0x7f}},
        {24, {0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0x7f}},
        {32, {0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x7f}},
    };
    for (const Case& c : cases) {
        WavBuilder wav;
        wav.format(1, 1, 22050, c.bits);
        wav.data(c.payload);
        const auto audio = decodeWav(wav.finish());
        ASSERT_TRUE(audio.has_value()) << c.bits << ": " << audio.error().message;
        EXPECT_EQ(audio->sampleRate, 22050u);
        EXPECT_EQ(audio->channels, 1u);
        ASSERT_EQ(audio->samples.size(), 3u) << c.bits;
        EXPECT_FLOAT_EQ(audio->samples[0], -1.0f) << c.bits;
        EXPECT_FLOAT_EQ(audio->samples[1], 0.0f) << c.bits;
        EXPECT_NEAR(audio->samples[2], 1.0f, 0.01f) << c.bits;
    }
}

TEST(Wav, DecodesFloatStereoExtensibleAndSkipsOtherChunks) {
    const std::vector<f32> values = {0.25f, -0.5f, 2.0f, -0.125f};  // 2.0 is clamped
    std::vector<u8> payload(values.size() * 4);
    std::memcpy(payload.data(), values.data(), payload.size());
    WavBuilder wav;
    wav.tag("LIST");  // metadata before fmt, odd-sized: padded to even
    wav.u32le(3);
    wav.bytes.insert(wav.bytes.end(), {'a', 'b', 'c', 0});
    wav.format(3, 2, 48000, 32, true);
    wav.data(payload);
    const auto audio = decodeWav(wav.finish());
    ASSERT_TRUE(audio.has_value()) << audio.error().message;
    EXPECT_EQ(audio->channels, 2u);
    EXPECT_EQ(audio->frameCount(), 2u);
    EXPECT_EQ(audio->samples, (std::vector<f32>{0.25f, -0.5f, 1.0f, -0.125f}));
}

TEST(Wav, ToleratesTruncatedDataButRejectsWhatItCannotPlay) {
    // Declared longer than the file (as streaming writers do), with half a
    // frame at the end: the whole frames are kept.
    WavBuilder truncated;
    truncated.format(1, 2, 8000, 16);
    truncated.data({1, 0, 2, 0, 3, 0, 4, 0, 5, 0}, 0xffffffffu);
    const auto audio = decodeWav(truncated.finish());
    ASSERT_TRUE(audio.has_value()) << audio.error().message;
    EXPECT_EQ(audio->frameCount(), 2u);

    EXPECT_FALSE(decodeWav(std::vector<u8>{'R', 'I', 'F', 'F'}).has_value());
    WavBuilder noFormat;
    noFormat.data({0, 0});
    EXPECT_FALSE(decodeWav(noFormat.finish()).has_value());
    WavBuilder surround;
    surround.format(1, 6, 48000, 16);
    surround.data(std::vector<u8>(12));
    const auto refused = decodeWav(surround.finish());
    ASSERT_FALSE(refused.has_value());
    EXPECT_NE(refused.error().message.find("6 channels"), std::string::npos);
    WavBuilder compressed;
    compressed.format(2, 1, 48000, 4);  // ADPCM
    compressed.data({0, 0});
    EXPECT_FALSE(decodeWav(compressed.finish()).has_value());
}

TEST(Wav, EncodingRoundTripsAndTonesSoundRight) {
    const AudioData tone = makeTone(440.0f, 0.5f, 48000, 0.5f);
    ASSERT_TRUE(tone.valid());
    EXPECT_EQ(tone.frameCount(), 24000u);
    EXPECT_DOUBLE_EQ(tone.seconds(), 0.5);
    // Fades in and out: no click at either end.
    EXPECT_FLOAT_EQ(tone.samples.front(), 0.0f);
    EXPECT_NEAR(tone.samples.back(), 0.0f, 1e-6f);
    // 440 Hz: 440 rising zero crossings per second.
    int crossings = 0;
    f32 peak = 0.0f;
    for (usize i = 1; i < tone.samples.size(); ++i) {
        crossings += tone.samples[i - 1] < 0.0f && tone.samples[i] >= 0.0f;
        peak = std::max(peak, std::abs(tone.samples[i]));
    }
    EXPECT_NEAR(crossings, 220, 1);
    EXPECT_NEAR(peak, 0.5f, 1e-3f);

    const auto decoded = decodeWav(encodeWav(tone));
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->sampleRate, 48000u);
    ASSERT_EQ(decoded->samples.size(), tone.samples.size());
    for (usize i = 0; i < tone.samples.size(); i += 97) {
        EXPECT_NEAR(decoded->samples[i], tone.samples[i], 1.0f / 32767.0f) << i;  // 16-bit
    }
}
