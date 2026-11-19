#pragma once

#include <bdex/core/Result.hpp>
#include <bdex/core/Types.hpp>

#include <limits>
#include <span>
#include <vector>

namespace bdex::assets {

/// Adler-32 checksum, as used by the zlib format.
[[nodiscard]] u32 adler32(std::span<const u8> data, u32 adler = 1);

/// Decompresses a zlib stream (RFC 1950 wrapping RFC 1951 deflate data):
/// stored, fixed-Huffman and dynamic-Huffman blocks. The Adler-32 trailer is
/// verified. Decompression fails once the output would exceed `maxSize`,
/// which bounds the memory a malicious stream can claim.
[[nodiscard]] Result<std::vector<u8>> zlibDecompress(
    std::span<const u8> stream, usize maxSize = std::numeric_limits<usize>::max());

/// Compresses `data` into a zlib stream: LZ77 with a 32 KiB window, then for
/// each block the smallest of stored, fixed- and dynamic-Huffman encoding.
[[nodiscard]] std::vector<u8> zlibCompress(std::span<const u8> data);

}  // namespace bdex::assets
