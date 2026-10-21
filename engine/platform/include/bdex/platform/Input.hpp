#pragma once

#include <bdex/core/EventBus.hpp>
#include <bdex/core/Math.hpp>
#include <bdex/platform/KeyCodes.hpp>

#include <array>
#include <bitset>

namespace bdex::platform {

/// Polled view of keyboard and mouse state, built from the input events on
/// the EventBus. It is backend-agnostic: any window module (or a test, or a
/// replay system) that publishes KeyEvent / MouseMoved / ... drives it.
///
/// Window modules provide it as a service and call beginFrame() right before
/// pumping OS events.
///
/// When a window loses focus, every held key and button is released (and
/// reported as released that frame): the matching release events would go to
/// another application, which would leave them stuck down otherwise.
class InputState {
public:
    explicit InputState(EventBus& events);

    /// Clears the per-frame transitions (pressed/released, deltas).
    void beginFrame();

    [[nodiscard]] bool keyDown(Key key) const;
    /// True only during the frame the key went down.
    [[nodiscard]] bool keyPressed(Key key) const;
    /// True only during the frame the key went up.
    [[nodiscard]] bool keyReleased(Key key) const;

    [[nodiscard]] bool mouseButtonDown(MouseButton button) const;
    /// True only during the frame the button went down.
    [[nodiscard]] bool mouseButtonPressed(MouseButton button) const;
    /// True only during the frame the button went up.
    [[nodiscard]] bool mouseButtonReleased(MouseButton button) const;

    // Gamepads are numbered from 0 (the first connected) to kMaxGamepads - 1.
    [[nodiscard]] bool gamepadConnected(u32 gamepad = 0) const;
    [[nodiscard]] bool gamepadButtonDown(GamepadButton button, u32 gamepad = 0) const;
    [[nodiscard]] bool gamepadButtonPressed(GamepadButton button, u32 gamepad = 0) const;
    [[nodiscard]] bool gamepadButtonReleased(GamepadButton button, u32 gamepad = 0) const;
    /// Raw value, without dead zone (see InputActions for one).
    [[nodiscard]] f32 gamepadAxis(GamepadAxis axis, u32 gamepad = 0) const;

    /// In framebuffer pixels (see MouseMoved).
    [[nodiscard]] Vec2 mousePosition() const { return mousePosition_; }
    /// Cursor motion accumulated since beginFrame().
    [[nodiscard]] Vec2 mouseDelta() const { return mouseDelta_; }
    [[nodiscard]] Vec2 scrollDelta() const { return scrollDelta_; }

private:
    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);
    static constexpr usize kButtonCount = static_cast<usize>(MouseButton::Count);

    std::bitset<kKeyCount> keysDown_;
    std::bitset<kKeyCount> keysPressed_;
    std::bitset<kKeyCount> keysReleased_;
    std::bitset<kButtonCount> buttonsDown_;
    std::bitset<kButtonCount> buttonsPressed_;
    std::bitset<kButtonCount> buttonsReleased_;
    struct Gamepad {
        static constexpr usize kButtons = static_cast<usize>(GamepadButton::Count);
        bool connected = false;
        std::bitset<kButtons> down;
        std::bitset<kButtons> pressed;
        std::bitset<kButtons> released;
        std::array<f32, static_cast<usize>(GamepadAxis::Count)> axes{};
    };
    [[nodiscard]] const Gamepad* gamepad(u32 index) const;

    std::array<Gamepad, kMaxGamepads> gamepads_{};
    Vec2 mousePosition_{0.0f};
    Vec2 mouseDelta_{0.0f};
    Vec2 scrollDelta_{0.0f};
    bool hasMousePosition_ = false;

    Subscription keySubscription_;
    Subscription buttonSubscription_;
    Subscription moveSubscription_;
    Subscription scrollSubscription_;
    Subscription focusSubscription_;
    Subscription gamepadConnectionSubscription_;
    Subscription gamepadButtonSubscription_;
    Subscription gamepadAxisSubscription_;
};

}  // namespace bdex::platform
