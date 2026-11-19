#include <bdex/assets/Zlib.hpp>

#include <bdex/core/Assert.hpp>

#include <algorithm>
#include <array>

namespace bdex::assets {
namespace {

constexpr int kMaxCodeBits = 15;
constexpr int kFastBits = 9;  // codes up to this length decode with one lookup
constexpr usize kLiteralCodes = 288;
constexpr usize kDistanceCodes = 32;

constexpr std::array<u16, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                             15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                             67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<u8, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<u16, 30> kDistanceBase = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<u8, 30> kDistanceExtra = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                               4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                               9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/// LSB-first bit reader over a byte span, as deflate requires.
class BitReader {
public:
    explicit BitReader(std::span<const u8> data) : data_(data) {}

    /// Loads bytes until at least `count` bits are buffered or the input ends.
    void fill(int count) {
        while (bitCount_ < count && position_ < data_.size()) {
            buffer_ |= static_cast<u64>(data_[position_++]) << bitCount_;
            bitCount_ += 8;
        }
    }

    /// Reads `count` (<= 32) bits; false at the end of the input.
    bool read(int count, u32& out) {
        fill(count);
        if (bitCount_ < count) {
            return false;
        }
        out = static_cast<u32>(buffer_ & ((u64{1} << count) - 1));
        consume(count);
        return true;
    }

    [[nodiscard]] u64 peek() const { return buffer_; }
    [[nodiscard]] int buffered() const { return bitCount_; }
    void consume(int count) {
        buffer_ >>= count;
        bitCount_ -= count;
    }

    /// Drops the bits left in the current byte.
    void alignToByte() { consume(bitCount_ % 8); }

    /// Reads whole bytes after alignToByte().
    bool readBytes(usize count, std::vector<u8>& out) {
        while (count > 0 && bitCount_ >= 8) {
            out.push_back(static_cast<u8>(buffer_));
            consume(8);
            --count;
        }
        if (data_.size() - position_ < count) {
            return false;
        }
        out.insert(out.end(), data_.begin() + static_cast<std::ptrdiff_t>(position_),
                   data_.begin() + static_cast<std::ptrdiff_t>(position_ + count));
        position_ += count;
        return true;
    }

private:
    std::span<const u8> data_;
    usize position_ = 0;
    u64 buffer_ = 0;
    int bitCount_ = 0;
};

/// Canonical Huffman decoding table.
class Huffman {
public:
    /// `lengths[symbol]` is the code length of each symbol, 0 when unused.
    /// Fails on over-subscribed lengths; incomplete codes are accepted (a
    /// missing code is reported when it is met).
    Result<> build(std::span<const u8> lengths) {
        counts_.fill(0);
        for (const u8 length : lengths) {
            ++counts_[length];
        }
        counts_[0] = 0;

        int left = 1;
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            left = (left << 1) - counts_[length];
            if (left < 0) {
                return fail("deflate: over-subscribed Huffman code");
            }
        }

        // Symbols sorted by code length, then by value: the canonical order.
        std::array<u16, kMaxCodeBits + 2> offsets{};
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            offsets[length + 1] = static_cast<u16>(offsets[length] + counts_[length]);
        }
        symbols_.assign(lengths.size(), 0);
        for (usize symbol = 0; symbol < lengths.size(); ++symbol) {
            if (lengths[symbol] != 0) {
                symbols_[offsets[lengths[symbol]]++] = static_cast<u16>(symbol);
            }
        }

        // Lookup table for short codes, indexed by the next kFastBits input
        // bits (deflate stores Huffman codes most significant bit first).
        fast_.fill(Entry{});
        u32 code = 0;
        usize index = 0;
        for (int length = 1; length <= kFastBits; ++length) {
            for (u16 i = 0; i < counts_[length]; ++i, ++code, ++index) {
                u32 reversed = 0;
                for (int bit = 0; bit < length; ++bit) {
                    reversed |= ((code >> bit) & 1u) << (length - 1 - bit);
                }
                for (u32 slot = reversed; slot < (1u << kFastBits); slot += 1u << length) {
                    fast_[slot] = Entry{symbols_[index], static_cast<u8>(length)};
                }
            }
            code <<= 1;
        }
        return {};
    }

    /// Returns the next symbol, or -1 on a missing code or truncated input.
    int decode(BitReader& reader) const {
        reader.fill(kFastBits);
        const Entry entry = fast_[reader.peek() & ((1u << kFastBits) - 1)];
        if (entry.length != 0 && entry.length <= reader.buffered()) {
            reader.consume(entry.length);
            return entry.symbol;
        }

        // Longer code: walk the canonical code one bit at a time.
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            u32 bit = 0;
            if (!reader.read(1, bit)) {
                return -1;
            }
            code |= static_cast<int>(bit);
            const int count = counts_[length];
            if (code - count < first) {
                return symbols_[static_cast<usize>(index + (code - first))];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }

private:
    struct Entry {
        u16 symbol = 0;
        u8 length = 0;  // 0: not in the table
    };

    std::array<u16, kMaxCodeBits + 1> counts_{};
    std::vector<u16> symbols_;
    std::array<Entry, 1u << kFastBits> fast_{};
};

Result<> inflateBlock(BitReader& reader, const Huffman& literals, const Huffman& distances,
                      std::vector<u8>& out, usize maxSize) {
    for (;;) {
        const int symbol = literals.decode(reader);
        if (symbol < 0) {
            return fail("deflate: invalid literal/length code");
        }
        if (symbol < 256) {
            if (out.size() >= maxSize) {
                return fail("deflate: output exceeds {} bytes", maxSize);
            }
            out.push_back(static_cast<u8>(symbol));
            continue;
        }
        if (symbol == 256) {
            return {};
        }

        const usize lengthCode = static_cast<usize>(symbol - 257);
        if (lengthCode >= kLengthBase.size()) {
            return fail("deflate: invalid length symbol {}", symbol);
        }
        u32 extra = 0;
        if (!reader.read(kLengthExtra[lengthCode], extra)) {
            return fail("deflate: truncated stream");
        }
        const usize length = kLengthBase[lengthCode] + extra;

        const int distanceCode = distances.decode(reader);
        if (distanceCode < 0 || static_cast<usize>(distanceCode) >= kDistanceBase.size()) {
            return fail("deflate: invalid distance code");
        }
        if (!reader.read(kDistanceExtra[static_cast<usize>(distanceCode)], extra)) {
            return fail("deflate: truncated stream");
        }
        const usize distance = kDistanceBase[static_cast<usize>(distanceCode)] + extra;
        if (distance > out.size()) {
            return fail("deflate: distance {} reaches before the start of the output", distance);
        }
        if (length > maxSize - out.size()) {
            return fail("deflate: output exceeds {} bytes", maxSize);
        }
        // Byte by byte: the copy may overlap the bytes it produces.
        const usize from = out.size() - distance;
        for (usize i = 0; i < length; ++i) {
            out.push_back(out[from + i]);
        }
    }
}

Result<> readDynamicTables(BitReader& reader, Huffman& literals, Huffman& distances) {
    u32 literalCount = 0;
    u32 distanceCount = 0;
    u32 codeLengthCount = 0;
    if (!reader.read(5, literalCount) || !reader.read(5, distanceCount) ||
        !reader.read(4, codeLengthCount)) {
        return fail("deflate: truncated block header");
    }
    literalCount += 257;
    distanceCount += 1;
    codeLengthCount += 4;
    if (literalCount > 286 || distanceCount > 30) {
        return fail("deflate: too many codes in a dynamic block");
    }

    constexpr std::array<u8, 19> kOrder = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                           11, 4,  12, 3, 13, 2, 14, 1, 15};
    std::array<u8, 19> codeLengthLengths{};
    for (u32 i = 0; i < codeLengthCount; ++i) {
        u32 value = 0;
        if (!reader.read(3, value)) {
            return fail("deflate: truncated block header");
        }
        codeLengthLengths[kOrder[i]] = static_cast<u8>(value);
    }
    Huffman codeLengths;
    if (auto built = codeLengths.build(codeLengthLengths); !built) {
        return built;
    }

    // Literal/length and distance code lengths form one run-length coded
    // sequence (a repeat may cross from one table to the other).
    std::array<u8, 286 + 30> lengths{};
    const u32 total = literalCount + distanceCount;
    for (u32 i = 0; i < total;) {
        const int symbol = codeLengths.decode(reader);
        if (symbol < 0) {
            return fail("deflate: invalid code length code");
        }
        if (symbol < 16) {
            lengths[i++] = static_cast<u8>(symbol);
            continue;
        }
        u8 value = 0;
        u32 repeat = 0;
        u32 extra = 0;
        if (symbol == 16) {
            if (i == 0) {
                return fail("deflate: length repeat with no previous length");
            }
            value = lengths[i - 1];
            if (!reader.read(2, extra)) return fail("deflate: truncated block header");
            repeat = 3 + extra;
        } else if (symbol == 17) {
            if (!reader.read(3, extra)) return fail("deflate: truncated block header");
            repeat = 3 + extra;
        } else {
            if (!reader.read(7, extra)) return fail("deflate: truncated block header");
            repeat = 11 + extra;
        }
        if (i + repeat > total) {
            return fail("deflate: code lengths overflow the tables");
        }
        for (u32 r = 0; r < repeat; ++r) {
            lengths[i++] = value;
        }
    }
    if (lengths[256] == 0) {
        return fail("deflate: dynamic block without an end-of-block code");
    }

    if (auto built = literals.build(std::span(lengths).first(literalCount)); !built) {
        return built;
    }
    return distances.build(std::span(lengths).subspan(literalCount, distanceCount));
}

const Huffman& fixedLiterals() {
    static const Huffman table = [] {
        std::array<u8, kLiteralCodes> lengths{};
        for (usize i = 0; i < kLiteralCodes; ++i) {
            lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        }
        Huffman huffman;
        const bool built = huffman.build(lengths).has_value();
        BDEX_ASSERT(built, "the fixed Huffman tables are complete");
        return huffman;
    }();
    return table;
}

const Huffman& fixedDistances() {
    static const Huffman table = [] {
        std::array<u8, kDistanceCodes> lengths{};
        lengths.fill(5);
        Huffman huffman;
        const bool built = huffman.build(lengths).has_value();
        BDEX_ASSERT(built, "the fixed Huffman tables are complete");
        return huffman;
    }();
    return table;
}

}  // namespace

u32 adler32(std::span<const u8> data, u32 adler) {
    // Sums are reduced every 5552 bytes, the most that cannot overflow u32.
    constexpr usize kBlock = 5552;
    u32 a = adler & 0xffff;
    u32 b = adler >> 16;
    while (!data.empty()) {
        const usize count = std::min(data.size(), kBlock);
        for (usize i = 0; i < count; ++i) {
            a += data[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        data = data.subspan(count);
    }
    return (b << 16) | a;
}

Result<std::vector<u8>> zlibDecompress(std::span<const u8> stream, usize maxSize) {
    if (stream.size() < 6) {
        return fail("zlib: stream too short");
    }
    const u8 method = stream[0];
    const u8 flags = stream[1];
    if ((method & 0x0f) != 8 || (method >> 4) > 7) {
        return fail("zlib: unsupported compression method");
    }
    if (((static_cast<u32>(method) << 8) | flags) % 31 != 0) {
        return fail("zlib: corrupted header");
    }
    if ((flags & 0x20) != 0) {
        return fail("zlib: preset dictionaries are not supported");
    }

    BitReader reader(stream.subspan(2));
    std::vector<u8> out;
    for (bool last = false; !last;) {
        u32 header = 0;
        if (!reader.read(3, header)) {
            return fail("deflate: truncated stream");
        }
        last = (header & 1u) != 0;
        const u32 type = header >> 1;

        if (type == 0) {
            reader.alignToByte();
            u32 length = 0;
            u32 complement = 0;
            if (!reader.read(16, length) || !reader.read(16, complement)) {
                return fail("deflate: truncated stored block");
            }
            if ((length ^ 0xffffu) != complement) {
                return fail("deflate: corrupted stored block length");
            }
            if (length > maxSize - out.size()) {
                return fail("deflate: output exceeds {} bytes", maxSize);
            }
            if (!reader.readBytes(length, out)) {
                return fail("deflate: truncated stored block");
            }
        } else if (type == 1) {
            if (auto inflated = inflateBlock(reader, fixedLiterals(), fixedDistances(), out, maxSize);
                !inflated) {
                return std::unexpected(inflated.error());
            }
        } else if (type == 2) {
            Huffman literals;
            Huffman distances;
            if (auto tables = readDynamicTables(reader, literals, distances); !tables) {
                return std::unexpected(tables.error());
            }
            if (auto inflated = inflateBlock(reader, literals, distances, out, maxSize);
                !inflated) {
                return std::unexpected(inflated.error());
            }
        } else {
            return fail("deflate: invalid block type");
        }
    }

    reader.alignToByte();
    u32 checksum = 0;
    for (int i = 0; i < 4; ++i) {
        u32 byte = 0;
        if (!reader.read(8, byte)) {
            return fail("zlib: missing Adler-32 trailer");
        }
        checksum = (checksum << 8) | byte;
    }
    if (checksum != adler32(out)) {
        return fail("zlib: Adler-32 mismatch");
    }
    return out;
}

}  // namespace bdex::assets
