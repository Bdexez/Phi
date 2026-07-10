#include <bdex/assets/Image.hpp>
#include <bdex/assets/Zlib.hpp>

#include <bdex/core/FileSystem.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>

namespace bdex::assets {
namespace {

u32 crc32(const u8* data, usize size, u32 crc = 0) {
    static const auto table = [] {
        std::array<u32, 256> values{};
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            values[n] = c;
        }
        return values;
    }();

    crc = ~crc;
    for (usize i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

void appendBigEndian(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value >> 24));
    out.push_back(static_cast<u8>(value >> 16));
    out.push_back(static_cast<u8>(value >> 8));
    out.push_back(static_cast<u8>(value));
}

void appendChunk(std::vector<u8>& out, const char type[4], const std::vector<u8>& payload) {
    appendBigEndian(out, static_cast<u32>(payload.size()));
    const usize crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    appendBigEndian(out, crc32(out.data() + crcStart, out.size() - crcStart));
}

}  // namespace

std::vector<u8> encodePng(const ImageData& image) {
    // Raw scanlines, each prefixed with filter type 0 (none).
    const usize rowBytes = static_cast<usize>(image.width) * 4;
    std::vector<u8> raw;
    raw.reserve((rowBytes + 1) * image.height);
    for (u32 y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const u8* row = image.pixels.data() + y * rowBytes;
        raw.insert(raw.end(), row, row + rowBytes);
    }

    // zlib stream made of "stored" deflate blocks (at most 65535 bytes each).
    std::vector<u8> zlib;
    zlib.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    usize offset = 0;
    do {
        const usize blockSize = std::min<usize>(raw.size() - offset, 65535);
        const bool last = offset + blockSize == raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<u8>(blockSize & 0xff));
        zlib.push_back(static_cast<u8>(blockSize >> 8));
        zlib.push_back(static_cast<u8>(~blockSize & 0xff));
        zlib.push_back(static_cast<u8>((~blockSize >> 8) & 0xff));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    } while (offset < raw.size());
    appendBigEndian(zlib, adler32(raw));

    std::vector<u8> header;
    appendBigEndian(header, image.width);
    appendBigEndian(header, image.height);
    header.insert(header.end(), {8 /*bit depth*/, 6 /*RGBA*/, 0, 0, 0});

    std::vector<u8> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    appendChunk(png, "IHDR", header);
    appendChunk(png, "IDAT", zlib);
    appendChunk(png, "IEND", {});
    return png;
}

Result<> writePng(const std::filesystem::path& path, const ImageData& image) {
    if (!image.valid()) {
        return fail("cannot write '{}': invalid image", path.string());
    }
    const std::vector<u8> png = encodePng(image);
    return fs::writeBinaryFile(path, png.data(), png.size());
}

// --- Decoding ------------------------------------------------------------------

namespace {

constexpr std::array<u8, 8> kSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
// Decoded images are bounded so a forged header cannot claim gigabytes.
constexpr u64 kMaxPixels = u64{1} << 28;

u32 readBigEndian(std::span<const u8> bytes, usize offset) {
    return (static_cast<u32>(bytes[offset]) << 24) | (static_cast<u32>(bytes[offset + 1]) << 16) |
           (static_cast<u32>(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

struct PngHeader {
    u32 width = 0;
    u32 height = 0;
    u8 bitDepth = 0;
    u8 colorType = 0;
    bool interlaced = false;
};

u32 channelCount(u8 colorType) {
    switch (colorType) {
        case 0: return 1;  // gray
        case 2: return 3;  // RGB
        case 3: return 1;  // palette index
        case 4: return 2;  // gray + alpha
        case 6: return 4;  // RGBA
        default: return 0;
    }
}

bool validBitDepth(u8 colorType, u8 bitDepth) {
    switch (colorType) {
        case 0: return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16;
        case 3: return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8;
        case 2:
        case 4:
        case 6: return bitDepth == 8 || bitDepth == 16;
        default: return false;
    }
}

/// One reduced image of the Adam7 sequence (or the whole image).
struct Pass {
    u32 x0, y0, dx, dy;
};
constexpr std::array<Pass, 7> kAdam7 = {{
    {0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2},
}};
constexpr std::array<Pass, 1> kSinglePass = {{{0, 0, 1, 1}}};

u32 passExtent(u32 size, u32 start, u32 step) {
    return size > start ? (size - start + step - 1) / step : 0;
}

usize rowBytes(const PngHeader& header, u32 width) {
    const u64 bits = u64{width} * channelCount(header.colorType) * header.bitDepth;
    return static_cast<usize>((bits + 7) / 8);
}

u8 paeth(u8 left, u8 up, u8 upLeft) {
    const int estimate = int{left} + int{up} - int{upLeft};
    const int toLeft = std::abs(estimate - int{left});
    const int toUp = std::abs(estimate - int{up});
    const int toUpLeft = std::abs(estimate - int{upLeft});
    if (toLeft <= toUp && toLeft <= toUpLeft) {
        return left;
    }
    return toUp <= toUpLeft ? up : upLeft;
}

/// Reverses the per-row filters of one pass, in place. `rows` holds `count`
/// rows of one filter byte followed by `stride` bytes.
Result<> unfilter(std::span<u8> rows, usize stride, usize count, usize pixelBytes) {
    for (usize y = 0; y < count; ++y) {
        u8* row = rows.data() + y * (stride + 1);
        const u8 filter = row[0];
        u8* current = row + 1;
        const u8* previous = y > 0 ? current - (stride + 1) : nullptr;
        for (usize i = 0; i < stride; ++i) {
            const u8 left = i >= pixelBytes ? current[i - pixelBytes] : 0;
            const u8 up = previous != nullptr ? previous[i] : 0;
            const u8 upLeft = previous != nullptr && i >= pixelBytes ? previous[i - pixelBytes] : 0;
            switch (filter) {
                case 0: break;
                case 1: current[i] = static_cast<u8>(current[i] + left); break;
                case 2: current[i] = static_cast<u8>(current[i] + up); break;
                case 3: current[i] = static_cast<u8>(current[i] + ((int{left} + int{up}) >> 1)); break;
                case 4: current[i] = static_cast<u8>(current[i] + paeth(left, up, upLeft)); break;
                default: return fail("png: invalid filter type {}", filter);
            }
        }
    }
    return {};
}

/// Sample `channel` of pixel `x` at the image's native bit depth.
u32 sampleAt(const u8* row, const PngHeader& header, u32 x, u32 channel) {
    const u32 index = x * channelCount(header.colorType) + channel;
    switch (header.bitDepth) {
        case 8: return row[index];
        case 16: return (u32{row[index * 2]} << 8) | row[index * 2 + 1];
        default: {
            const u32 bit = index * header.bitDepth;
            const u32 shift = 8 - header.bitDepth - bit % 8;
            return (row[bit / 8] >> shift) & ((1u << header.bitDepth) - 1);
        }
    }
}

u8 toEightBits(u32 sample, u8 bitDepth) {
    if (bitDepth == 16) {
        return static_cast<u8>(sample >> 8);
    }
    return static_cast<u8>(sample * 255 / ((1u << bitDepth) - 1));
}

}  // namespace

Result<ImageData> decodePng(std::span<const u8> bytes) {
    if (bytes.size() < kSignature.size() ||
        !std::equal(kSignature.begin(), kSignature.end(), bytes.begin())) {
        return fail("png: missing signature");
    }

    PngHeader header;
    bool hasHeader = false;
    std::vector<Color8> palette;
    std::vector<u8> paletteAlpha;          // tRNS for indexed images
    std::array<u32, 3> colorKey{};         // tRNS for gray / RGB images
    bool hasColorKey = false;
    std::vector<u8> compressed;

    for (usize offset = kSignature.size();;) {
        if (bytes.size() - offset < 12) {
            if (!compressed.empty()) {
                break;  // tolerate a missing IEND
            }
            return fail("png: truncated chunk");
        }
        const u32 length = readBigEndian(bytes, offset);
        if (length > bytes.size() - offset - 12) {
            return fail("png: truncated chunk");
        }
        const std::span<const u8> type = bytes.subspan(offset + 4, 4);
        const std::span<const u8> data = bytes.subspan(offset + 8, length);
        if (readBigEndian(bytes, offset + 8 + length) != crc32(type.data(), length + 4)) {
            return fail("png: CRC mismatch in a '{}' chunk",
                        std::string_view(reinterpret_cast<const char*>(type.data()), 4));
        }
        offset += 12 + length;

        const auto is = [&](const char (&name)[5]) {
            return std::equal(type.begin(), type.end(), name);
        };
        if (!hasHeader && !is("IHDR")) {
            return fail("png: IHDR must come first");
        }
        if (is("IHDR")) {
            if (hasHeader || length != 13) {
                return fail("png: malformed IHDR");
            }
            header.width = readBigEndian(data, 0);
            header.height = readBigEndian(data, 4);
            header.bitDepth = data[8];
            header.colorType = data[9];
            header.interlaced = data[12] == 1;
            if (header.width == 0 || header.height == 0) {
                return fail("png: empty image");
            }
            if (u64{header.width} * header.height > kMaxPixels) {
                return fail("png: {}x{} exceeds the supported size", header.width, header.height);
            }
            if (!validBitDepth(header.colorType, header.bitDepth)) {
                return fail("png: unsupported color type {} with bit depth {}", header.colorType,
                            header.bitDepth);
            }
            if (data[10] != 0 || data[11] != 0 || data[12] > 1) {
                return fail("png: unknown compression, filter or interlace method");
            }
            hasHeader = true;
        } else if (is("PLTE")) {
            if (length % 3 != 0 || length / 3 > 256 || length == 0) {
                return fail("png: malformed palette");
            }
            palette.resize(length / 3);
            for (usize i = 0; i < palette.size(); ++i) {
                palette[i] = {data[i * 3], data[i * 3 + 1], data[i * 3 + 2], 255};
            }
        } else if (is("tRNS")) {
            if (header.colorType == 3) {
                paletteAlpha.assign(data.begin(), data.end());
            } else if (header.colorType == 0 && length == 2) {
                colorKey[0] = (u32{data[0]} << 8) | data[1];
                hasColorKey = true;
            } else if (header.colorType == 2 && length == 6) {
                for (usize c = 0; c < 3; ++c) {
                    colorKey[c] = (u32{data[c * 2]} << 8) | data[c * 2 + 1];
                }
                hasColorKey = true;
            }
        } else if (is("IDAT")) {
            compressed.insert(compressed.end(), data.begin(), data.end());
        } else if (is("IEND")) {
            break;
        } else if ((type[0] & 0x20) == 0) {
            return fail("png: unsupported critical chunk '{}'",
                        std::string_view(reinterpret_cast<const char*>(type.data()), 4));
        }
        // Other ancillary chunks (gamma, text, physical size...) are ignored.
    }

    if (compressed.empty()) {
        return fail("png: no image data");
    }
    if (header.colorType == 3 && palette.empty()) {
        return fail("png: indexed image without a palette");
    }

    const std::span<const Pass> passes =
        header.interlaced ? std::span<const Pass>(kAdam7) : std::span<const Pass>(kSinglePass);
    usize expected = 0;
    for (const Pass& pass : passes) {
        const u32 width = passExtent(header.width, pass.x0, pass.dx);
        const u32 height = passExtent(header.height, pass.y0, pass.dy);
        if (width != 0 && height != 0) {
            expected += height * (rowBytes(header, width) + 1);
        }
    }
    auto raw = zlibDecompress(compressed, expected);
    if (!raw) {
        return fail("png: {}", raw.error().message);
    }
    if (raw->size() != expected) {
        return fail("png: image data holds {} bytes, {} expected", raw->size(), expected);
    }

    ImageData image;
    image.width = header.width;
    image.height = header.height;
    image.pixels.resize(static_cast<usize>(header.width) * header.height * 4);
    const usize pixelBytes =
        std::max<usize>(1, channelCount(header.colorType) * header.bitDepth / 8);

    usize offset = 0;
    for (const Pass& pass : passes) {
        const u32 width = passExtent(header.width, pass.x0, pass.dx);
        const u32 height = passExtent(header.height, pass.y0, pass.dy);
        if (width == 0 || height == 0) {
            continue;
        }
        const usize stride = rowBytes(header, width);
        const std::span<u8> rows = std::span(*raw).subspan(offset, height * (stride + 1));
        offset += rows.size();
        if (auto unfiltered = unfilter(rows, stride, height, pixelBytes); !unfiltered) {
            return std::unexpected(unfiltered.error());
        }

        for (u32 y = 0; y < height; ++y) {
            const u8* row = rows.data() + y * (stride + 1) + 1;
            for (u32 x = 0; x < width; ++x) {
                Color8 color{0, 0, 0, 255};
                switch (header.colorType) {
                    case 0: {
                        const u32 gray = sampleAt(row, header, x, 0);
                        const u8 value = toEightBits(gray, header.bitDepth);
                        color = {value, value, value,
                                 static_cast<u8>(hasColorKey && gray == colorKey[0] ? 0 : 255)};
                        break;
                    }
                    case 2: {
                        bool keyed = hasColorKey;
                        for (u32 c = 0; c < 3; ++c) {
                            const u32 sample = sampleAt(row, header, x, c);
                            color[c] = toEightBits(sample, header.bitDepth);
                            keyed = keyed && sample == colorKey[c];
                        }
                        color[3] = keyed ? 0 : 255;
                        break;
                    }
                    case 3: {
                        const u32 index = sampleAt(row, header, x, 0);
                        if (index >= palette.size()) {
                            return fail("png: palette index {} out of range", index);
                        }
                        color = palette[index];
                        if (index < paletteAlpha.size()) {
                            color[3] = paletteAlpha[index];
                        }
                        break;
                    }
                    case 4: {
                        const u8 value = toEightBits(sampleAt(row, header, x, 0), header.bitDepth);
                        color = {value, value, value,
                                 toEightBits(sampleAt(row, header, x, 1), header.bitDepth)};
                        break;
                    }
                    default:  // 6
                        for (u32 c = 0; c < 4; ++c) {
                            color[c] = toEightBits(sampleAt(row, header, x, c), header.bitDepth);
                        }
                        break;
                }
                const usize target = (static_cast<usize>(pass.y0 + y * pass.dy) * header.width +
                                      pass.x0 + x * pass.dx) * 4;
                std::copy(color.begin(), color.end(), image.pixels.begin() +
                                                          static_cast<std::ptrdiff_t>(target));
            }
        }
    }
    return image;
}

}  // namespace bdex::assets
