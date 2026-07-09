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
    // encodePng wraps its scanlines in stored blocks: extract its IDAT payload.
    const ImageData image = makeCheckerboard(200, 8, {255, 0, 0, 255}, {0, 0, 255, 255});
    const std::vector<u8> png = encodePng(image);
    const auto readU32 = [&](usize offset) {
        return (static_cast<u32>(png[offset]) << 24) | (static_cast<u32>(png[offset + 1]) << 16) |
               (static_cast<u32>(png[offset + 2]) << 8) | png[offset + 3];
    };
    const usize idatLength = readU32(33);
    const std::span<const u8> idat = std::span(png).subspan(41, idatLength);

    const auto raw = zlibDecompress(idat);
    ASSERT_TRUE(raw.has_value()) << raw.error().message;
    ASSERT_EQ(raw->size(), (200u * 4 + 1) * 200);  // more than one 64 KiB block
    EXPECT_EQ((*raw)[0], 0);                         // filter byte of the first row
    EXPECT_EQ((*raw)[1], 255);                       // first pixel: red
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
