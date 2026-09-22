#include <bdex/render/DebugDraw.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace bdex::render {

namespace {

// Stroke font on a 4 x 6 grid (capitals 6 high, baseline at 0). Each glyph
// is polylines of "x,y" points separated by spaces; '|' lifts the pen.
struct Glyph {
    char character;
    std::string_view strokes;
};
constexpr Glyph kGlyphs[] = {
        {'0', "0,0 4,0 4,6 0,6 0,0|0,0 4,6"},
        {'1', "1,5 2,6 2,0|1,0 3,0"},
        {'2', "0,5 1,6 3,6 4,5 4,4 0,0 4,0"},
        {'3', "0,6 4,6 2,4 4,3 4,1 3,0 0,0"},
        {'4', "3,0 3,6 0,2 4,2"},
        {'5', "4,6 0,6 0,3 3,3 4,2 4,1 3,0 0,0"},
        {'6', "4,6 1,6 0,5 0,0 4,0 4,3 0,3"},
        {'7', "0,6 4,6 1,0"},
        {'8', "0,0 4,0 4,6 0,6 0,0|0,3 4,3"},
        {'9', "0,0 3,0 4,1 4,6 0,6 0,3 4,3"},
        {'A', "0,0 0,4 2,6 4,4 4,0|0,3 4,3"},
        {'B', "0,0 0,6 3,6 4,5 4,4 3,3 0,3|3,3 4,2 4,1 3,0 0,0"},
        {'C', "4,6 0,6 0,0 4,0"},
        {'D', "0,0 0,6 2,6 4,4 4,2 2,0 0,0"},
        {'E', "4,6 0,6 0,0 4,0|0,3 3,3"},
        {'F', "4,6 0,6 0,0|0,3 3,3"},
        {'G', "4,6 0,6 0,0 4,0 4,3 2,3"},
        {'H', "0,0 0,6|4,0 4,6|0,3 4,3"},
        {'I', "1,6 3,6|2,6 2,0|1,0 3,0"},
        {'J', "4,6 4,0 0,0 0,2"},
        {'K', "0,0 0,6|4,6 0,3 4,0"},
        {'L', "0,6 0,0 4,0"},
        {'M', "0,0 0,6 2,3 4,6 4,0"},
        {'N', "0,0 0,6 4,0 4,6"},
        {'O', "0,0 4,0 4,6 0,6 0,0"},
        {'P', "0,0 0,6 4,6 4,3 0,3"},
        {'Q', "0,0 4,0 4,6 0,6 0,0|2,2 4,0"},
        {'R', "0,0 0,6 4,6 4,3 0,3 4,0"},
        {'S', "4,5 3,6 1,6 0,5 0,4 1,3 3,3 4,2 4,1 3,0 1,0 0,1"},
        {'T', "0,6 4,6|2,6 2,0"},
        {'U', "0,6 0,0 4,0 4,6"},
        {'V', "0,6 2,0 4,6"},
        {'W', "0,6 1,0 2,3 3,0 4,6"},
        {'X', "0,0 4,6|0,6 4,0"},
        {'Y', "0,6 2,3 4,6|2,3 2,0"},
        {'Z', "0,6 4,6 0,0 4,0"},
        {'.', "2,0 2,1"},
        {',', "2,1 1,-1"},
        {':', "2,1 2,2|2,4 2,5"},
        {';', "2,4 2,5|2,1 1,-1"},
        {'-', "1,3 3,3"},
        {'+', "1,3 3,3|2,2 2,4"},
        {'=', "1,2 3,2|1,4 3,4"},
        {'*', "0,1 4,5|0,5 4,1|2,6 2,0"},
        {'/', "0,0 4,6"},
        {'\\', "0,6 4,0"},
        {'_', "0,0 4,0"},
        {'(', "3,6 1,4 1,2 3,0"},
        {')', "1,6 3,4 3,2 1,0"},
        {'[', "3,6 1,6 1,0 3,0"},
        {']', "1,6 3,6 3,0 1,0"},
        {'<', "4,6 0,3 4,0"},
        {'>', "0,6 4,3 0,0"},
        {'%', "0,6 1,6 1,5 0,5 0,6|4,6 0,0|3,1 4,1 4,0 3,0 3,1"},
        {'#', "1,0 1,6|3,0 3,6|0,2 4,2|0,4 4,4"},
        {'!', "2,6 2,2|2,0 2,1"},
        {'?', "0,5 1,6 3,6 4,5 4,4 2,3 2,2|2,0 2,1"},
        {'\'', "2,6 2,5"},
        {'"', "1,6 1,5|3,6 3,5"},
        {'|', "2,6 2,0"},
};
constexpr f32 kGlyphHeight = 6.0f;
constexpr f32 kAdvance = 6.0f;      ///< Glyph width 4, then a gap of 2.
constexpr f32 kLineSpacing = 9.0f;  ///< Baseline to baseline.

std::string_view strokesOf(char character) {
    if (character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.character == character) {
            return glyph.strokes;
        }
    }
    return strokesOf('?');
}

}  // namespace

void DebugDraw::line(const Vec3& from, const Vec3& to, const Vec4& color) {
    vertices_.push_back({from, color});
    vertices_.push_back({to, color});
}

void DebugDraw::box(const Vec3& min, const Vec3& max, const Vec4& color) {
    const auto corner = [&](int i) {
        return Vec3((i & 1) != 0 ? max.x : min.x, (i & 2) != 0 ? max.y : min.y,
                    (i & 4) != 0 ? max.z : min.z);
    };
    // The 12 edges join corners differing in exactly one coordinate.
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if ((i & bit) == 0) {
                line(corner(i), corner(i | bit), color);
            }
        }
    }
}

void DebugDraw::box(const Mat4& transform, const Vec3& halfExtents, const Vec4& color) {
    const auto corner = [&](int i) {
        const Vec3 local((i & 1) != 0 ? halfExtents.x : -halfExtents.x, (i & 2) != 0 ? halfExtents.y : -halfExtents.y,
                         (i & 4) != 0 ? halfExtents.z : -halfExtents.z);
        return Vec3(transform * Vec4(local, 1.0f));
    };
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if ((i & bit) == 0) {
                line(corner(i), corner(i | bit), color);
            }
        }
    }
}

void DebugDraw::text(const Vec3& origin, std::string_view text, f32 height, const Vec4& color, const Vec3& right,
                     const Vec3& up) {
    const f32 unit = height / kGlyphHeight;
    Vec3 lineStart = origin;
    Vec3 pen = origin;
    for (const char character : text) {
        if (character == '\n') {
            lineStart -= up * (kLineSpacing * unit);
            pen = lineStart;
            continue;
        }
        if (character != ' ') {
            // Walk the polylines: each point after the first draws a line.
            const std::string_view strokes = strokesOf(character);
            bool penDown = false;
            Vec3 previous(0.0f);
            usize at = 0;
            while (at < strokes.size()) {
                if (strokes[at] == '|') {
                    penDown = false;
                    ++at;
                    continue;
                }
                if (strokes[at] == ' ') {
                    ++at;
                    continue;
                }
                // "x,y" with optional minus signs; single digits keep it simple.
                const auto number = [&]() {
                    f32 sign = 1.0f;
                    if (strokes[at] == '-') {
                        sign = -1.0f;
                        ++at;
                    }
                    const f32 value = static_cast<f32>(strokes[at] - '0');
                    ++at;
                    return sign * value;
                };
                const f32 x = number();
                ++at;  // ','
                const f32 y = number();
                const Vec3 point = pen + right * (x * unit) + up * (y * unit);
                if (penDown) {
                    line(previous, point, color);
                }
                previous = point;
                penDown = true;
            }
        }
        pen += right * (kAdvance * unit);
    }
}

f32 DebugDraw::textWidth(std::string_view text, f32 height) {
    const f32 unit = height / kGlyphHeight;
    usize longest = 0;
    usize current = 0;
    for (const char character : text) {
        current = character == '\n' ? 0 : current + 1;
        longest = std::max(longest, current);
    }
    // The last glyph's trailing gap is not part of the text.
    return longest == 0 ? 0.0f : (static_cast<f32>(longest) * kAdvance - (kAdvance - 4.0f)) * unit;
}

void DebugDraw::sphere(const Vec3& center, f32 radius, const Vec4& color, u32 segments) {
    segments = std::max(segments, 3u);
    const f32 step = 2.0f * std::numbers::pi_v<f32> / static_cast<f32>(segments);
    const std::array<std::pair<Vec3, Vec3>, 3> planes = {{
        {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)},
        {Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)},
        {Vec3(0.0f, 0.0f, 1.0f), Vec3(1.0f, 0.0f, 0.0f)},
    }};
    for (const auto& [a, b] : planes) {
        for (u32 i = 0; i < segments; ++i) {
            const f32 t0 = step * static_cast<f32>(i);
            const f32 t1 = step * static_cast<f32>(i + 1);
            line(center + (a * std::cos(t0) + b * std::sin(t0)) * radius,
                 center + (a * std::cos(t1) + b * std::sin(t1)) * radius, color);
        }
    }
}

void DebugDraw::axes(const Mat4& transform, f32 length) {
    const Vec3 origin(transform[3]);
    for (int axis = 0; axis < 3; ++axis) {
        Vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
        color[axis] = 1.0f;
        line(origin, origin + Vec3(transform[axis]) * length, color);
    }
}

void DebugDraw::frustum(const Mat4& viewProjection, const Vec4& color) {
    const Mat4 inverse = glm::inverse(viewProjection);
    std::array<Vec3, 8> corners;
    for (int i = 0; i < 8; ++i) {
        const Vec4 clip((i & 1) != 0 ? 1.0f : -1.0f, (i & 2) != 0 ? 1.0f : -1.0f,
                        (i & 4) != 0 ? 1.0f : 0.0f, 1.0f);
        const Vec4 world = inverse * clip;
        corners[static_cast<usize>(i)] = Vec3(world) / world.w;
    }
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if ((i & bit) == 0) {
                line(corners[static_cast<usize>(i)], corners[static_cast<usize>(i | bit)], color);
            }
        }
    }
}

}  // namespace bdex::render
