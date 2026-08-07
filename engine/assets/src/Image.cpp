#include <bdex/assets/Image.hpp>

#include <bdex/core/Math.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bdex::assets {

ImageData makeSolidImage(u32 width, u32 height, Color8 color) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<usize>(width) * height * 4);
    for (usize i = 0; i < image.pixels.size(); i += 4) {
        std::copy(color.begin(), color.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return image;
}

ImageData makeCheckerboard(u32 size, u32 cells, Color8 first, Color8 second) {
    cells = std::max(cells, 1u);
    ImageData image = makeSolidImage(size, size, first);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const u32 cellX = x * cells / size;
            const u32 cellY = y * cells / size;
            if ((cellX + cellY) % 2 == 1) {
                std::copy(second.begin(), second.end(),
                          image.pixels.begin() +
                              static_cast<std::ptrdiff_t>((static_cast<usize>(y) * size + x) * 4));
            }
        }
    }
    return image;
}

// --- TGA -------------------------------------------------------------------------

ImageData makeNormalMap(std::span<const f32> heights, u32 width, u32 height, f32 strength) {
    ImageData image;
    if (width == 0 || height == 0 || heights.size() != static_cast<usize>(width) * height) {
        return image;
    }
    image.width = width;
    image.height = height;
    image.pixels.resize(heights.size() * 4);
    const auto at = [&](u32 x, u32 y) {
        return heights[static_cast<usize>(y % height) * width + x % width];
    };
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            // Central differences; texture "up" is the previous row.
            const f32 slopeRight = (at(x + 1, y) - at(x + width - 1, y)) * 0.5f;
            const f32 slopeUp = (at(x, y + height - 1) - at(x, y + 1)) * 0.5f;
            const Vec3 normal = glm::normalize(Vec3(-slopeRight * strength, -slopeUp * strength, 1.0f));
            u8* pixel = image.pixels.data() + (static_cast<usize>(y) * width + x) * 4;
            for (int c = 0; c < 3; ++c) {
                pixel[c] = static_cast<u8>(std::lround((normal[c] * 0.5f + 0.5f) * 255.0f));
            }
            pixel[3] = 255;
        }
    }
    return image;
}

Result<ImageData> decodeTga(std::span<const u8> bytes) {
    constexpr usize kHeaderSize = 18;
    if (bytes.size() < kHeaderSize) {
        return fail("tga: file too small");
    }

    const u8 idLength = bytes[0];
    const u8 colorMapType = bytes[1];
    const u8 imageType = bytes[2];
    const u32 width = bytes[12] | (static_cast<u32>(bytes[13]) << 8);
    const u32 height = bytes[14] | (static_cast<u32>(bytes[15]) << 8);
    const u8 bitsPerPixel = bytes[16];
    const bool topToBottom = (bytes[17] & 0x20) != 0;

    if (colorMapType != 0 || (imageType != 2 && imageType != 10)) {
        return fail("tga: only true-color images are supported (type {})", imageType);
    }
    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        return fail("tga: unsupported bit depth {}", bitsPerPixel);
    }
    if (width == 0 || height == 0) {
        return fail("tga: empty image");
    }

    const usize bytesPerPixel = bitsPerPixel / 8;
    const usize pixelCount = static_cast<usize>(width) * height;
    usize cursor = kHeaderSize + idLength;

    ImageData image;
    image.width = width;
    image.height = height;
    image.pixels.resize(pixelCount * 4);

    // Writes pixel `index` (in file order) from BGR(A) source bytes.
    const auto store = [&](usize index, const u8* source) {
        const usize row = index / width;
        const usize column = index % width;
        const usize targetRow = topToBottom ? row : height - 1 - row;
        u8* target = image.pixels.data() + (targetRow * width + column) * 4;
        target[0] = source[2];
        target[1] = source[1];
        target[2] = source[0];
        target[3] = bytesPerPixel == 4 ? source[3] : 255;
    };

    if (imageType == 2) {
        if (bytes.size() < cursor + pixelCount * bytesPerPixel) {
            return fail("tga: truncated pixel data");
        }
        for (usize i = 0; i < pixelCount; ++i) {
            store(i, bytes.data() + cursor + i * bytesPerPixel);
        }
        return image;
    }

    // Run-length encoded packets.
    usize written = 0;
    while (written < pixelCount) {
        if (cursor >= bytes.size()) {
            return fail("tga: truncated RLE data");
        }
        const u8 packet = bytes[cursor++];
        const usize count = static_cast<usize>(packet & 0x7f) + 1;
        if (written + count > pixelCount) {
            return fail("tga: RLE packet overruns the image");
        }
        if ((packet & 0x80) != 0) {
            if (cursor + bytesPerPixel > bytes.size()) {
                return fail("tga: truncated RLE data");
            }
            for (usize i = 0; i < count; ++i) {
                store(written + i, bytes.data() + cursor);
            }
            cursor += bytesPerPixel;
        } else {
            if (cursor + count * bytesPerPixel > bytes.size()) {
                return fail("tga: truncated RLE data");
            }
            for (usize i = 0; i < count; ++i) {
                store(written + i, bytes.data() + cursor + i * bytesPerPixel);
            }
            cursor += count * bytesPerPixel;
        }
        written += count;
    }
    return image;
}

}  // namespace bdex::assets
