#pragma once

#include <phi/core/Types.hpp>

#include <optional>
#include <string_view>

namespace phi::platform {

/// Keyboard keys, named after their position on a US layout.
/// Values are engine-defined; window backends translate their native codes.
enum class Key : u16 {
    Unknown = 0,

    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon = 59,
    Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,

    Escape = 256,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Right,
    Left,
    Down,
    Up,
    PageUp,
    PageDown,
    Home,
    End,
    CapsLock = 280,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift = 340,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper,

    Count = 352,
};

enum class MouseButton : u8 { Left = 0, Right, Middle, Button4, Button5, Count };

enum class KeyAction : u8 { Release, Press, Repeat };

/// Buttons of a standard gamepad, named after the Xbox layout (A at the
/// bottom of the face buttons).
enum class GamepadButton : u8 {
    A, B, X, Y,
    LeftBumper, RightBumper,
    Back, Start, Guide,
    LeftThumb, RightThumb,
    DpadUp, DpadRight, DpadDown, DpadLeft,
    Count
};

/// Sticks go from -1 to 1 (+X right, +Y down); triggers from 0 (released)
/// to 1.
enum class GamepadAxis : u8 { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

/// Gamepads tracked by InputState.
inline constexpr u32 kMaxGamepads = 4;

/// Name of a key as written in configuration files: the enumerator's name
/// ("W", "Space", "LeftShift", "F3"), digits as "0".."9". Empty for Unknown.
[[nodiscard]] std::string_view keyName(Key key);
/// Inverse of keyName, ignoring letter case; also accepts "Num0".."Num9" and
/// "Ctrl" / "Shift" / "Alt" / "Super" for the left-hand modifiers.
[[nodiscard]] std::optional<Key> parseKey(std::string_view name);

/// "MouseLeft", "MouseRight", "MouseMiddle", "Mouse4", "Mouse5".
[[nodiscard]] std::string_view mouseButtonName(MouseButton button);
[[nodiscard]] std::optional<MouseButton> parseMouseButton(std::string_view name);

/// "GamepadA", "GamepadStart", "GamepadDpadUp"...
[[nodiscard]] std::string_view gamepadButtonName(GamepadButton button);
[[nodiscard]] std::optional<GamepadButton> parseGamepadButton(std::string_view name);

}  // namespace phi::platform
