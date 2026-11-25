#include <phi/assets/Zlib.hpp>

#include <phi/core/Assert.hpp>

#include <algorithm>
#include <array>

namespace phi::assets {
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
                return symbols_[static_cast<usize>(index) + static_cast<usize>(code - first)];
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
        PHI_ASSERT(built, "the fixed Huffman tables are complete");
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
        PHI_ASSERT(built, "the fixed Huffman tables are complete");
        return huffman;
    }();
    return table;
}


// --- Compression ---------------------------------------------------------------

/// LSB-first bit writer.
class BitWriter {
public:
    void write(u32 bits, int count) {
        buffer_ |= static_cast<u64>(bits) << bitCount_;
        bitCount_ += count;
        while (bitCount_ >= 8) {
            out_.push_back(static_cast<u8>(buffer_));
            buffer_ >>= 8;
            bitCount_ -= 8;
        }
    }

    void alignToByte() {
        if (bitCount_ > 0) {
            out_.push_back(static_cast<u8>(buffer_));
            buffer_ = 0;
            bitCount_ = 0;
        }
    }

    [[nodiscard]] std::vector<u8>& bytes() { return out_; }

private:
    std::vector<u8> out_;
    u64 buffer_ = 0;
    int bitCount_ = 0;
};

/// A literal (distance == 0) or a back-reference.
struct Token {
    u16 value = 0;  ///< Literal byte, or match length (3..258).
    u16 distance = 0;
};

constexpr usize kMinMatch = 3;
constexpr usize kMaxMatch = 258;
constexpr usize kWindowSize = 32768;
constexpr int kHashBits = 15;
constexpr u32 kMaxChain = 128;     // candidates examined per position
constexpr usize kNiceMatch = 128;  // stop searching once a match is this long
constexpr usize kBlockTokens = 32768;

const std::array<u8, kMaxMatch + 1>& lengthCodes() {
    static const auto table = [] {
        std::array<u8, kMaxMatch + 1> codes{};
        for (usize code = 0; code < kLengthBase.size(); ++code) {
            const usize end = std::min<usize>(kMaxMatch, kLengthBase[code] + (1u << kLengthExtra[code]) - 1);
            for (usize length = kLengthBase[code]; length <= end; ++length) {
                codes[length] = static_cast<u8>(code);  // 258 ends up on code 285
            }
        }
        return codes;
    }();
    return table;
}

usize distanceCode(usize distance) {
    const auto it = std::upper_bound(kDistanceBase.begin(), kDistanceBase.end(), distance);
    return static_cast<usize>(it - kDistanceBase.begin()) - 1;
}

/// Greedy LZ77 with hash chains and one step of lazy matching.
std::vector<Token> findMatches(std::span<const u8> data) {
    std::vector<Token> tokens;
    tokens.reserve(data.size() / 2 + 16);
    std::vector<i32> head(usize{1} << kHashBits, -1);
    std::vector<i32> previous(kWindowSize, -1);

    const auto hashAt = [&](usize position) {
        const u32 key = u32{data[position]} | (u32{data[position + 1]} << 8) |
                        (u32{data[position + 2]} << 16);
        return (key * 2654435761u) >> (32 - kHashBits);
    };
    const auto insert = [&](usize position) {
        if (position + kMinMatch <= data.size()) {
            const u32 hash = hashAt(position);
            previous[position % kWindowSize] = head[hash];
            head[hash] = static_cast<i32>(position);
        }
    };
    // Longest earlier match for `position` (which must not be inserted yet).
    const auto longestMatch = [&](usize position, usize& distance) -> usize {
        if (position + kMinMatch > data.size()) {
            return 0;
        }
        const usize limit = std::min(kMaxMatch, data.size() - position);
        usize best = 0;
        u32 chain = kMaxChain;
        for (i32 candidate = head[hashAt(position)];
             candidate >= 0 && position - static_cast<usize>(candidate) <= kWindowSize && chain-- > 0;
             candidate = previous[static_cast<usize>(candidate) % kWindowSize]) {
            const u8* a = data.data() + candidate;
            const u8* b = data.data() + position;
            if (a[best] != b[best]) {
                continue;  // cannot beat the current best
            }
            usize length = 0;
            while (length < limit && a[length] == b[length]) {
                ++length;
            }
            if (length > best) {
                best = length;
                distance = position - static_cast<usize>(candidate);
                if (best >= std::min(limit, kNiceMatch)) {
                    break;
                }
            }
        }
        return best >= kMinMatch ? best : 0;
    };

    usize position = 0;
    while (position < data.size()) {
        usize distance = 0;
        const usize length = longestMatch(position, distance);
        insert(position);
        if (length == 0) {
            tokens.push_back({data[position], 0});
            ++position;
            continue;
        }
        // Lazy matching: a longer match one byte later is worth a literal.
        if (length < kNiceMatch) {
            usize nextDistance = 0;
            if (longestMatch(position + 1, nextDistance) > length) {
                tokens.push_back({data[position], 0});
                ++position;
                continue;
            }
        }
        tokens.push_back({static_cast<u16>(length), static_cast<u16>(distance)});
        for (usize i = 1; i < length; ++i) {
            insert(position + i);
        }
        position += length;
    }
    return tokens;
}

/// Gives at least two symbols a non-zero frequency. A tree with a single
/// code is incomplete: legal for distances, but rejected by some decoders
/// (zlib refuses it for the code-length alphabet).
void ensureTwoCodes(std::span<u32> frequencies) {
    usize used = static_cast<usize>(
        std::count_if(frequencies.begin(), frequencies.end(), [](u32 f) { return f != 0; }));
    for (usize i = 0; used < 2 && i < frequencies.size(); ++i) {
        if (frequencies[i] == 0) {
            frequencies[i] = 1;
            ++used;
        }
    }
}

/// Huffman code lengths for `frequencies`, none longer than `maxBits`.
/// Frequencies are flattened until the tree fits, which always terminates
/// (equal weights give a balanced tree).
std::vector<u8> codeLengths(std::span<const u32> frequencies, int maxBits) {
    std::vector<u8> lengths(frequencies.size(), 0);
    std::vector<u32> weights(frequencies.begin(), frequencies.end());
    for (;;) {
        struct Node {
            u64 weight;
            i32 left;
            i32 right;
        };
        std::vector<Node> nodes;
        std::vector<i32> symbolOfLeaf;
        for (usize symbol = 0; symbol < weights.size(); ++symbol) {
            if (weights[symbol] != 0) {
                nodes.push_back({weights[symbol], -1, -1});
                symbolOfLeaf.push_back(static_cast<i32>(symbol));
            }
        }
        if (nodes.size() == 1) {
            lengths[static_cast<usize>(symbolOfLeaf[0])] = 1;
            return lengths;
        }
        if (nodes.empty()) {
            return lengths;
        }

        // Min-heap of node indices; ties broken by index for determinism.
        const auto greater = [&nodes](i32 a, i32 b) {
            const Node& x = nodes[static_cast<usize>(a)];
            const Node& y = nodes[static_cast<usize>(b)];
            return x.weight != y.weight ? x.weight > y.weight : a > b;
        };
        std::vector<i32> heap;
        heap.reserve(nodes.size());
        for (usize i = 0; i < nodes.size(); ++i) {
            heap.push_back(static_cast<i32>(i));
        }
        std::make_heap(heap.begin(), heap.end(), greater);
        while (heap.size() > 1) {
            std::pop_heap(heap.begin(), heap.end(), greater);
            const i32 a = heap.back();
            heap.pop_back();
            std::pop_heap(heap.begin(), heap.end(), greater);
            const i32 b = heap.back();
            heap.pop_back();
            nodes.push_back({nodes[static_cast<usize>(a)].weight + nodes[static_cast<usize>(b)].weight, a, b});
            heap.push_back(static_cast<i32>(nodes.size() - 1));
            std::push_heap(heap.begin(), heap.end(), greater);
        }

        // Depth of every leaf.
        int deepest = 0;
        std::vector<std::pair<i32, int>> stack = {{heap.front(), 0}};
        while (!stack.empty()) {
            const auto [index, depth] = stack.back();
            stack.pop_back();
            const Node& node = nodes[static_cast<usize>(index)];
            if (node.left < 0) {
                lengths[static_cast<usize>(symbolOfLeaf[static_cast<usize>(index)])] = static_cast<u8>(depth);
                deepest = std::max(deepest, depth);
            } else {
                stack.push_back({node.left, depth + 1});
                stack.push_back({node.right, depth + 1});
            }
        }
        if (deepest <= maxBits) {
            return lengths;
        }
        for (u32& weight : weights) {
            if (weight != 0) {
                weight = (weight + 1) / 2;
            }
        }
    }
}

/// Canonical codes for `lengths`, bit-reversed for an LSB-first writer.
std::vector<u16> canonicalCodes(std::span<const u8> lengths) {
    std::array<u16, kMaxCodeBits + 2> counts{};
    for (const u8 length : lengths) {
        ++counts[length];
    }
    counts[0] = 0;
    std::array<u16, kMaxCodeBits + 2> next{};
    u16 code = 0;
    for (int length = 1; length <= kMaxCodeBits; ++length) {
        code = static_cast<u16>((code + counts[length - 1]) << 1);
        next[length] = code;
    }
    std::vector<u16> codes(lengths.size(), 0);
    for (usize symbol = 0; symbol < lengths.size(); ++symbol) {
        const u8 length = lengths[symbol];
        if (length == 0) {
            continue;
        }
        const u16 value = next[length]++;
        u16 reversed = 0;
        for (int bit = 0; bit < length; ++bit) {
            reversed = static_cast<u16>(reversed | (((value >> bit) & 1u) << (length - 1 - bit)));
        }
        codes[symbol] = reversed;
    }
    return codes;
}

/// Literal/length and distance codes of one Huffman block.
struct BlockCodes {
    std::vector<u8> literalLengths;
    std::vector<u8> distanceLengths;
};

u64 tokenBits(std::span<const Token> tokens, const BlockCodes& codes) {
    const auto& lengthTable = lengthCodes();
    u64 bits = codes.literalLengths[256];
    for (const Token& token : tokens) {
        if (token.distance == 0) {
            bits += codes.literalLengths[token.value];
        } else {
            const usize lengthCode = lengthTable[token.value];
            const usize distance = distanceCode(token.distance);
            bits += codes.literalLengths[257 + lengthCode] + kLengthExtra[lengthCode] +
                    codes.distanceLengths[distance] + kDistanceExtra[distance];
        }
    }
    return bits;
}

void writeTokens(BitWriter& writer, std::span<const Token> tokens, const BlockCodes& codes) {
    const auto& lengthTable = lengthCodes();
    const std::vector<u16> literalCodes = canonicalCodes(codes.literalLengths);
    const std::vector<u16> distanceCodes = canonicalCodes(codes.distanceLengths);
    for (const Token& token : tokens) {
        if (token.distance == 0) {
            writer.write(literalCodes[token.value], codes.literalLengths[token.value]);
            continue;
        }
        const usize lengthCode = lengthTable[token.value];
        writer.write(literalCodes[257 + lengthCode], codes.literalLengths[257 + lengthCode]);
        writer.write(token.value - kLengthBase[lengthCode], kLengthExtra[lengthCode]);
        const usize distance = distanceCode(token.distance);
        writer.write(distanceCodes[distance], codes.distanceLengths[distance]);
        writer.write(token.distance - kDistanceBase[distance], kDistanceExtra[distance]);
    }
    writer.write(literalCodes[256], codes.literalLengths[256]);
}

/// Header of a dynamic block: the code lengths, run-length and Huffman coded.
struct DynamicHeader {
    u32 literalCount = 0;
    u32 distanceCount = 0;
    u32 codeLengthCount = 0;
    std::vector<u8> codeLengthLengths;  // 19 entries
    std::vector<std::pair<u8, u8>> runs;  // (symbol 0..18, extra bits value)

    [[nodiscard]] u64 bits() const {
        u64 total = 5 + 5 + 4 + 3 * u64{codeLengthCount};
        for (const auto& [symbol, extra] : runs) {
            total += codeLengthLengths[symbol] + (symbol == 16 ? 2 : symbol == 17 ? 3 : symbol == 18 ? 7 : 0);
        }
        return total;
    }
};

constexpr std::array<u8, 19> kCodeLengthOrder = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                 11, 4,  12, 3, 13, 2, 14, 1, 15};

DynamicHeader makeDynamicHeader(const BlockCodes& codes) {
    DynamicHeader header;
    header.literalCount = 286;
    while (header.literalCount > 257 && codes.literalLengths[header.literalCount - 1] == 0) {
        --header.literalCount;
    }
    header.distanceCount = 30;
    while (header.distanceCount > 1 && codes.distanceLengths[header.distanceCount - 1] == 0) {
        --header.distanceCount;
    }

    std::vector<u8> all(codes.literalLengths.begin(),
                        codes.literalLengths.begin() + header.literalCount);
    all.insert(all.end(), codes.distanceLengths.begin(),
               codes.distanceLengths.begin() + header.distanceCount);
    for (usize i = 0; i < all.size();) {
        const u8 value = all[i];
        usize run = 1;
        while (i + run < all.size() && all[i + run] == value) {
            ++run;
        }
        usize left = run;
        if (value == 0) {
            while (left >= 11) {
                const usize count = std::min<usize>(left, 138);
                header.runs.push_back({18, static_cast<u8>(count - 11)});
                left -= count;
            }
            if (left >= 3) {
                header.runs.push_back({17, static_cast<u8>(left - 3)});
                left = 0;
            }
        } else {
            header.runs.push_back({value, 0});
            --left;
            while (left >= 3) {
                const usize count = std::min<usize>(left, 6);
                header.runs.push_back({16, static_cast<u8>(count - 3)});
                left -= count;
            }
        }
        for (; left > 0; --left) {
            header.runs.push_back({value, 0});
        }
        i += run;
    }

    std::array<u32, 19> frequencies{};
    for (const auto& [symbol, extra] : header.runs) {
        ++frequencies[symbol];
    }
    ensureTwoCodes(frequencies);
    header.codeLengthLengths = codeLengths(frequencies, 7);
    header.codeLengthCount = 19;
    while (header.codeLengthCount > 4 &&
           header.codeLengthLengths[kCodeLengthOrder[header.codeLengthCount - 1]] == 0) {
        --header.codeLengthCount;
    }
    return header;
}

void writeDynamicHeader(BitWriter& writer, const DynamicHeader& header) {
    writer.write(header.literalCount - 257, 5);
    writer.write(header.distanceCount - 1, 5);
    writer.write(header.codeLengthCount - 4, 4);
    for (u32 i = 0; i < header.codeLengthCount; ++i) {
        writer.write(header.codeLengthLengths[kCodeLengthOrder[i]], 3);
    }
    const std::vector<u16> codes = canonicalCodes(header.codeLengthLengths);
    for (const auto& [symbol, extra] : header.runs) {
        writer.write(codes[symbol], header.codeLengthLengths[symbol]);
        if (symbol >= 16) {
            writer.write(extra, symbol == 16 ? 2 : symbol == 17 ? 3 : 7);
        }
    }
}

const BlockCodes& fixedCodes() {
    static const BlockCodes codes = [] {
        BlockCodes fixed;
        fixed.literalLengths.resize(kLiteralCodes);
        for (usize i = 0; i < kLiteralCodes; ++i) {
            fixed.literalLengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        }
        fixed.distanceLengths.assign(kDistanceCodes, 5);
        return fixed;
    }();
    return codes;
}

/// Writes one block of tokens covering `raw`, choosing whichever of the
/// stored, fixed and dynamic encodings is smallest.
void writeBlock(BitWriter& writer, std::span<const Token> tokens, std::span<const u8> raw,
                bool last) {
    std::vector<u32> literalFrequencies(286, 0);
    std::vector<u32> distanceFrequencies(30, 0);
    literalFrequencies[256] = 1;
    const auto& lengthTable = lengthCodes();
    for (const Token& token : tokens) {
        if (token.distance == 0) {
            ++literalFrequencies[token.value];
        } else {
            ++literalFrequencies[257 + lengthTable[token.value]];
            ++distanceFrequencies[distanceCode(token.distance)];
        }
    }
    ensureTwoCodes(literalFrequencies);
    ensureTwoCodes(distanceFrequencies);

    BlockCodes dynamic;
    dynamic.literalLengths = codeLengths(literalFrequencies, kMaxCodeBits);
    dynamic.distanceLengths = codeLengths(distanceFrequencies, kMaxCodeBits);
    const DynamicHeader header = makeDynamicHeader(dynamic);

    const u64 dynamicBits = 3 + header.bits() + tokenBits(tokens, dynamic);
    const u64 fixedBits = 3 + tokenBits(tokens, fixedCodes());
    const usize storedBlocks = std::max<usize>(1, (raw.size() + 65534) / 65535);
    const u64 storedBits = (u64{raw.size()} + 5 * u64{storedBlocks}) * 8 + 7;

    if (storedBits <= dynamicBits && storedBits <= fixedBits) {
        usize offset = 0;
        do {
            const usize size = std::min<usize>(raw.size() - offset, 65535);
            const bool final = last && offset + size == raw.size();
            writer.write(final ? 1 : 0, 3);
            writer.alignToByte();
            writer.write(static_cast<u32>(size), 16);
            writer.write(static_cast<u32>(~size & 0xffff), 16);
            auto& bytes = writer.bytes();
            bytes.insert(bytes.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                         raw.begin() + static_cast<std::ptrdiff_t>(offset + size));
            offset += size;
        } while (offset < raw.size());
    } else if (fixedBits <= dynamicBits) {
        writer.write((last ? 1 : 0) | (1u << 1), 3);
        writeTokens(writer, tokens, fixedCodes());
    } else {
        writer.write((last ? 1 : 0) | (2u << 1), 3);
        writeDynamicHeader(writer, header);
        writeTokens(writer, tokens, dynamic);
    }
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

std::vector<u8> zlibCompress(std::span<const u8> data) {
    BitWriter writer;
    writer.write(0x78, 8);  // deflate, 32 KiB window
    writer.write(0x9c, 8);  // default level, header checksum

    const std::vector<Token> tokens = findMatches(data);
    usize rawOffset = 0;
    usize first = 0;
    do {
        const usize count = std::min(kBlockTokens, tokens.size() - first);
        const std::span<const Token> block = std::span(tokens).subspan(first, count);
        usize rawSize = 0;
        for (const Token& token : block) {
            rawSize += token.distance == 0 ? 1 : token.value;
        }
        writeBlock(writer, block, data.subspan(rawOffset, rawSize), first + count == tokens.size());
        first += count;
        rawOffset += rawSize;
    } while (first < tokens.size());

    writer.alignToByte();
    const u32 checksum = adler32(data);
    for (int shift = 24; shift >= 0; shift -= 8) {
        writer.write((checksum >> shift) & 0xff, 8);
    }
    return std::move(writer.bytes());
}

}  // namespace phi::assets
