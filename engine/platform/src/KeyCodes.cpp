#include <phi/platform/KeyCodes.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace phi::platform {
namespace {

using Entry = std::pair<Key, std::string_view>;

// Every named key, in enum order.
constexpr std::array kKeyNames = std::to_array<Entry>({
    {Key::Space, "Space"},
    {Key::Apostrophe, "Apostrophe"},
    {Key::Comma, "Comma"},
    {Key::Minus, "Minus"},
    {Key::Period, "Period"},
    {Key::Slash, "Slash"},
    {Key::Num0, "0"}, {Key::Num1, "1"}, {Key::Num2, "2"}, {Key::Num3, "3"}, {Key::Num4, "4"},
    {Key::Num5, "5"}, {Key::Num6, "6"}, {Key::Num7, "7"}, {Key::Num8, "8"}, {Key::Num9, "9"},
    {Key::Semicolon, "Semicolon"},
    {Key::Equal, "Equal"},
    {Key::A, "A"}, {Key::B, "B"}, {Key::C, "C"}, {Key::D, "D"}, {Key::E, "E"}, {Key::F, "F"},
    {Key::G, "G"}, {Key::H, "H"}, {Key::I, "I"}, {Key::J, "J"}, {Key::K, "K"}, {Key::L, "L"},
    {Key::M, "M"}, {Key::N, "N"}, {Key::O, "O"}, {Key::P, "P"}, {Key::Q, "Q"}, {Key::R, "R"},
    {Key::S, "S"}, {Key::T, "T"}, {Key::U, "U"}, {Key::V, "V"}, {Key::W, "W"}, {Key::X, "X"},
    {Key::Y, "Y"}, {Key::Z, "Z"},
    {Key::LeftBracket, "LeftBracket"},
    {Key::Backslash, "Backslash"},
    {Key::RightBracket, "RightBracket"},
    {Key::GraveAccent, "GraveAccent"},
    {Key::Escape, "Escape"},
    {Key::Enter, "Enter"},
    {Key::Tab, "Tab"},
    {Key::Backspace, "Backspace"},
    {Key::Insert, "Insert"},
    {Key::Delete, "Delete"},
    {Key::Right, "Right"},
    {Key::Left, "Left"},
    {Key::Down, "Down"},
    {Key::Up, "Up"},
    {Key::PageUp, "PageUp"},
    {Key::PageDown, "PageDown"},
    {Key::Home, "Home"},
    {Key::End, "End"},
    {Key::CapsLock, "CapsLock"},
    {Key::F1, "F1"}, {Key::F2, "F2"}, {Key::F3, "F3"}, {Key::F4, "F4"}, {Key::F5, "F5"},
    {Key::F6, "F6"}, {Key::F7, "F7"}, {Key::F8, "F8"}, {Key::F9, "F9"}, {Key::F10, "F10"},
    {Key::F11, "F11"}, {Key::F12, "F12"},
    {Key::LeftShift, "LeftShift"},
    {Key::LeftControl, "LeftControl"},
    {Key::LeftAlt, "LeftAlt"},
    {Key::LeftSuper, "LeftSuper"},
    {Key::RightShift, "RightShift"},
    {Key::RightControl, "RightControl"},
    {Key::RightAlt, "RightAlt"},
    {Key::RightSuper, "RightSuper"},
});

constexpr std::array kAliases = std::to_array<Entry>({
    {Key::Num0, "Num0"}, {Key::Num1, "Num1"}, {Key::Num2, "Num2"}, {Key::Num3, "Num3"},
    {Key::Num4, "Num4"}, {Key::Num5, "Num5"}, {Key::Num6, "Num6"}, {Key::Num7, "Num7"},
    {Key::Num8, "Num8"}, {Key::Num9, "Num9"},
    {Key::LeftControl, "Ctrl"}, {Key::LeftControl, "LeftCtrl"}, {Key::RightControl, "RightCtrl"},
    {Key::LeftShift, "Shift"}, {Key::LeftAlt, "Alt"}, {Key::LeftSuper, "Super"},
    {Key::Enter, "Return"}, {Key::Escape, "Esc"},
});

constexpr std::array kButtonNames = std::to_array<std::pair<MouseButton, std::string_view>>({
    {MouseButton::Left, "MouseLeft"},
    {MouseButton::Right, "MouseRight"},
    {MouseButton::Middle, "MouseMiddle"},
    {MouseButton::Button4, "Mouse4"},
    {MouseButton::Button5, "Mouse5"},
});

constexpr std::array kGamepadButtonNames = std::to_array<std::pair<GamepadButton, std::string_view>>({
    {GamepadButton::A, "GamepadA"},
    {GamepadButton::B, "GamepadB"},
    {GamepadButton::X, "GamepadX"},
    {GamepadButton::Y, "GamepadY"},
    {GamepadButton::LeftBumper, "GamepadLeftBumper"},
    {GamepadButton::RightBumper, "GamepadRightBumper"},
    {GamepadButton::Back, "GamepadBack"},
    {GamepadButton::Start, "GamepadStart"},
    {GamepadButton::Guide, "GamepadGuide"},
    {GamepadButton::LeftThumb, "GamepadLeftThumb"},
    {GamepadButton::RightThumb, "GamepadRightThumb"},
    {GamepadButton::DpadUp, "GamepadDpadUp"},
    {GamepadButton::DpadRight, "GamepadDpadRight"},
    {GamepadButton::DpadDown, "GamepadDpadDown"},
    {GamepadButton::DpadLeft, "GamepadDpadLeft"},
});

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) { return lower(x) == lower(y); });
}

}  // namespace

std::string_view keyName(Key key) {
    for (const auto& [candidate, name] : kKeyNames) {
        if (candidate == key) {
            return name;
        }
    }
    return {};
}

std::optional<Key> parseKey(std::string_view name) {
    for (const std::span<const Entry> table : {std::span<const Entry>(kKeyNames), std::span<const Entry>(kAliases)}) {
        for (const auto& [key, candidate] : table) {
            if (equalsIgnoringCase(candidate, name)) {
                return key;
            }
        }
    }
    return std::nullopt;
}

std::string_view mouseButtonName(MouseButton button) {
    for (const auto& [candidate, name] : kButtonNames) {
        if (candidate == button) {
            return name;
        }
    }
    return {};
}

std::optional<MouseButton> parseMouseButton(std::string_view name) {
    for (const auto& [button, candidate] : kButtonNames) {
        if (equalsIgnoringCase(candidate, name)) {
            return button;
        }
    }
    return std::nullopt;
}

std::string_view gamepadButtonName(GamepadButton button) {
    for (const auto& [candidate, name] : kGamepadButtonNames) {
        if (candidate == button) {
            return name;
        }
    }
    return {};
}

std::optional<GamepadButton> parseGamepadButton(std::string_view name) {
    for (const auto& [button, candidate] : kGamepadButtonNames) {
        if (equalsIgnoringCase(candidate, name)) {
            return button;
        }
    }
    return std::nullopt;
}

}  // namespace phi::platform
