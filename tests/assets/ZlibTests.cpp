#include <bdex/assets/Image.hpp>
#include <bdex/assets/Zlib.hpp>

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

using namespace bdex;
using namespace bdex::assets;

namespace {

std::vector<u8> bytesOf(std::string_view text) {
    return {text.begin(), text.end()};
}

// Reference streams produced by zlib 1.3 (Python's zlib module).

// "hello hello hello hello", one fixed-Huffman block with back-references.
const std::vector<u8> kFixedStream = {0x78, 0x01, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57,
                                      0xc8, 0x40, 0x27, 0x01, 0x68, 0x03, 0x08, 0xb1};

// kDynamicText at level 9: one dynamic-Huffman block.
constexpr std::string_view kDynamicText =
    "t etneedoetie oeeeaaeeeoaedie enni eiiteeeoletaeoeitodneeiineteoseieieanoahtai atteresheeitoautsatie";
const std::vector<u8> kDynamicStream = {
    0x78, 0xda, 0x15, 0x8c, 0x01, 0x0a, 0xc0, 0x30, 0x0c, 0x02, 0xbf, 0xd2,
    0xaf, 0x09, 0x3d, 0x98, 0x30, 0x12, 0x58, 0xdd, 0xff, 0x97, 0x21, 0x08,
    0x1e, 0x6a, 0x16, 0x29, 0xd8, 0x4d, 0xcc, 0x6a, 0x40, 0x1a, 0x6b, 0xb1,
    0x27, 0x53, 0xe5, 0x85, 0x9d, 0x1f, 0xdd, 0x44, 0x34, 0x4e, 0xef, 0x59,
    0xd8, 0x45, 0xe8, 0x83, 0x47, 0xaa, 0xd6, 0x15, 0x79, 0x29, 0xe1, 0xe1,
    0x5c, 0xfc, 0x35, 0xbd, 0x39, 0x9a, 0xdb, 0x0f, 0xb5, 0xa4, 0x27, 0xcc,
};

}  // namespace

TEST(Zlib, Adler32MatchesTheReference) {
    EXPECT_EQ(adler32({}), 1u);
    EXPECT_EQ(adler32(bytesOf("Wikipedia")), 0x11e60398u);
    // Long inputs exercise the deferred modulo.
    const std::vector<u8> ones(100000, 0xff);
    u32 incremental = 1;
    for (usize offset = 0; offset < ones.size(); offset += 7777) {
        const usize count = std::min<usize>(7777, ones.size() - offset);
        incremental = adler32(std::span(ones).subspan(offset, count), incremental);
    }
    EXPECT_EQ(incremental, adler32(ones));
}

TEST(Zlib, InflatesFixedAndDynamicHuffmanBlocks) {
    const auto fixed = zlibDecompress(kFixedStream);
    ASSERT_TRUE(fixed.has_value()) << fixed.error().message;
    EXPECT_EQ(*fixed, bytesOf("hello hello hello hello"));

    const auto dynamic = zlibDecompress(kDynamicStream);
    ASSERT_TRUE(dynamic.has_value()) << dynamic.error().message;
    EXPECT_EQ(*dynamic, bytesOf(kDynamicText));
}

TEST(Zlib, InflatesStoredBlocks) {
    // More than one stored block (65535 bytes at most each).
    std::vector<u8> data(150000);
    for (usize i = 0; i < data.size(); ++i) {
        data[i] = static_cast<u8>(i * 7 + i / 300);
    }
    std::vector<u8> stream = {0x78, 0x01};
    for (usize offset = 0; offset < data.size();) {
        const usize size = std::min<usize>(data.size() - offset, 65535);
        const bool last = offset + size == data.size();
        stream.insert(stream.end(), {static_cast<u8>(last ? 1 : 0), static_cast<u8>(size),
                                     static_cast<u8>(size >> 8), static_cast<u8>(~size),
                                     static_cast<u8>(~size >> 8)});
        stream.insert(stream.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                      data.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    }
    const u32 checksum = adler32(data);
    for (int shift = 24; shift >= 0; shift -= 8) {
        stream.push_back(static_cast<u8>(checksum >> shift));
    }

    const auto raw = zlibDecompress(stream);
    ASSERT_TRUE(raw.has_value()) << raw.error().message;
    EXPECT_EQ(*raw, data);
}

TEST(Zlib, CompressionRoundTrips) {
    std::vector<std::vector<u8>> inputs;
    inputs.emplace_back();                      // empty
    inputs.push_back(bytesOf("a"));             // single byte
    inputs.push_back(std::vector<u8>(100000, 'x'));  // one long run
    inputs.push_back(bytesOf(kDynamicText));
    std::vector<u8> noise(70000);
    u32 state = 12345;
    for (u8& byte : noise) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<u8>(state >> 24);
    }
    inputs.push_back(noise);                     // incompressible: stored blocks
    std::vector<u8> mixed;
    for (int i = 0; i < 2000; ++i) {
        const std::string_view words[] = {"render ", "frame ", "entity ", "vulkan "};
        const std::string_view word = words[(i * 7 + i / 13) % 4];
        mixed.insert(mixed.end(), word.begin(), word.end());
        mixed.push_back(static_cast<u8>(i));
    }
    inputs.push_back(mixed);                     // dynamic Huffman blocks

    for (const std::vector<u8>& input : inputs) {
        const std::vector<u8> compressed = zlibCompress(input);
        const auto restored = zlibDecompress(compressed);
        ASSERT_TRUE(restored.has_value()) << restored.error().message;
        EXPECT_EQ(*restored, input);
        // Incompressible data costs at most the stored-block framing.
        EXPECT_LE(compressed.size(), input.size() + input.size() / 1000 + 64);
    }
    EXPECT_LT(zlibCompress(inputs[2]).size(), 1000u);
    EXPECT_LT(zlibCompress(mixed).size(), mixed.size() / 3);
}

TEST(Zlib, RejectsCorruptedStreams) {
    EXPECT_FALSE(zlibDecompress({}).has_value());

    std::vector<u8> badHeader = kFixedStream;
    badHeader[1] ^= 0x01;  // header checksum no longer divisible by 31
    EXPECT_FALSE(zlibDecompress(badHeader).has_value());

    std::vector<u8> badChecksum = kFixedStream;
    badChecksum.back() ^= 0xff;
    EXPECT_FALSE(zlibDecompress(badChecksum).has_value());

    const std::vector<u8> truncated(kDynamicStream.begin(), kDynamicStream.end() - 10);
    EXPECT_FALSE(zlibDecompress(truncated).has_value());

    // Flipping any bit of the payload must never crash nor silently produce
    // different data. (A flip in the padding bits after the last block is
    // harmless and decodes to the original.)
    usize rejected = 0;
    for (usize i = 2; i < kDynamicStream.size(); ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            std::vector<u8> corrupted = kDynamicStream;
            corrupted[i] ^= static_cast<u8>(1u << bit);
            const auto result = zlibDecompress(corrupted);
            if (result.has_value()) {
                EXPECT_EQ(*result, bytesOf(kDynamicText)) << "byte " << i << ", bit " << bit;
            } else {
                ++rejected;
            }
        }
    }
    EXPECT_GT(rejected, (kDynamicStream.size() - 2) * 8 - 8);
}

TEST(Zlib, EnforcesTheOutputLimit) {
    EXPECT_TRUE(zlibDecompress(kFixedStream, 23).has_value());
    const auto limited = zlibDecompress(kFixedStream, 22);
    ASSERT_FALSE(limited.has_value());
    EXPECT_NE(limited.error().message.find("exceeds"), std::string::npos);
}
