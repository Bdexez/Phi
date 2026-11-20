#pragma once

#include <bdex/core/Types.hpp>
#include <bdex/platform/KeyCodes.hpp>

namespace bdex::platform {

class IWindow;

// Events published on the engine EventBus by window modules.

struct WindowCloseRequested {
    IWindow* window = nullptr;
};

struct WindowResized {
    IWindow* window = nullptr;
    Extent2D framebufferSize;
};

struct WindowFocusChanged {
    IWindow* window = nullptr;
    bool focused = false;
};

struct KeyEvent {
    Key key = Key::Unknown;
    KeyAction action = KeyAction::Press;
};

struct MouseButtonEvent {
    MouseButton button = MouseButton::Left;
    bool pressed = false;
};

/// Cursor position in framebuffer pixels from the window's top-left corner,
/// the units rendering, picking and overlays use (on a high-DPI display a
/// window's own coordinates are scaled down from these).
struct MouseMoved {
    f64 x = 0.0;
    f64 y = 0.0;
};

struct MouseScrolled {
    f64 deltaX = 0.0;
    f64 deltaY = 0.0;
};

struct GamepadConnected {
    u32 gamepad = 0;  ///< Slot, below kMaxGamepads.
    bool connected = false;
};

struct GamepadButtonEvent {
    u32 gamepad = 0;
    GamepadButton button = GamepadButton::A;
    bool pressed = false;
};

/// Published when an axis value changes.
struct GamepadAxisMoved {
    u32 gamepad = 0;
    GamepadAxis axis = GamepadAxis::LeftX;
    f32 value = 0.0f;
};

}  // namespace bdex::platform
