#include <bdex/assets/Image.hpp>
#include <bdex/assets/Zlib.hpp>

#include <bdex/core/FileSystem.hpp>

#include <algorithm>
#include <array>

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

}  // namespace bdex::assets
