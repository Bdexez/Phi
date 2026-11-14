#pragma once

#include <phi/platform/Input.hpp>
#include <phi/render/DebugDraw.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace phi::ui {

/// The pointer as the interface sees it, in overlay pixels.
struct Input {
    Vec2 pointer{-1.0f};
    bool down = false;      ///< The primary button is held.
    bool pressed = false;   ///< It went down this frame.
    bool released = false;  ///< It came up this frame.
};

/// The left mouse button and the cursor, from a window's input.
[[nodiscard]] Input fromInputState(const platform::InputState& input);

/// A small immediate-mode interface drawn on the overlay: describe it every
/// frame, and each widget reports whether it changed.
///
///   ui.begin(ui::fromInputState(input), debugDraw);
///   ui.panel("Sun", {12, 200}, 220);
///   ui.slider("height", sunHeight, 0.0f, 90.0f);
///   if (ui.button("reset")) { ... }
///   ui.endPanel();
///   ui.end();
///
/// Widgets are told apart by their panel's title and their label.
class Context {
public:
    void begin(const Input& input, render::DebugDraw& draw);
    void end();

    /// Starts a panel whose top-left corner is at `at`; widgets stack in it.
    void panel(std::string_view title, const Vec2& at, f32 width);
    void endPanel();

    void label(std::string_view text);
    /// True once, when released over it after being pressed on it.
    bool button(std::string_view text);
    /// Flips `value` when clicked; true then.
    bool checkbox(std::string_view text, bool& value);
    /// Drag along it to set `value` within [min, max]; true while it changes.
    bool slider(std::string_view text, f32& value, f32 min, f32 max);

    /// Whether the pointer is over the interface or dragging one of its
    /// widgets: then the game should leave its clicks alone.
    [[nodiscard]] bool wantsPointer() const;

    static constexpr f32 kRowHeight = 20.0f;
    static constexpr f32 kPadding = 6.0f;
    static constexpr f32 kTextHeight = 11.0f;

private:
    struct Rect {
        Vec2 min;
        Vec2 max;
        [[nodiscard]] bool contains(const Vec2& point) const {
            return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
        }
    };

    [[nodiscard]] u64 idOf(std::string_view text) const;
    /// The next row of the current panel.
    [[nodiscard]] Rect row();
    void text(const Vec2& at, std::string_view text, const Vec4& color);

    Input input_;
    render::DebugDraw* draw_ = nullptr;
    std::string panel_;
    Vec2 panelAt_{0.0f};
    f32 panelWidth_ = 0.0f;
    f32 cursor_ = 0.0f;  ///< Where the next row starts.
    u64 active_ = 0;     ///< The widget being pressed or dragged.
    std::unordered_map<std::string, f32> heights_;  ///< Each panel's, last frame: its background.
    std::vector<Rect> panels_;      ///< This frame's.
    std::vector<Rect> lastPanels_;  ///< Last frame's, for wantsPointer.
};

}  // namespace phi::ui
