#include <phi/platform/Input.hpp>

#include <phi/platform/Events.hpp>

namespace phi::platform {

InputState::InputState(EventBus& events) {
    keySubscription_ = events.subscribe<KeyEvent>([this](const KeyEvent& event) {
        const usize index = static_cast<usize>(event.key);
        if (event.key == Key::Unknown || index >= kKeyCount) {
            return;
        }
        if (event.action == KeyAction::Press) {
            keysDown_.set(index);
            keysPressed_.set(index);
        } else if (event.action == KeyAction::Release) {
            keysDown_.reset(index);
            keysReleased_.set(index);
        }
    });

    buttonSubscription_ = events.subscribe<MouseButtonEvent>([this](const MouseButtonEvent& event) {
        const usize index = static_cast<usize>(event.button);
        if (index >= kButtonCount) {
            return;
        }
        buttonsDown_.set(index, event.pressed);
        if (event.pressed) {
            buttonsPressed_.set(index);
        } else {
            buttonsReleased_.set(index);
        }
    });

    moveSubscription_ = events.subscribe<MouseMoved>([this](const MouseMoved& event) {
        const Vec2 position(static_cast<f32>(event.x), static_cast<f32>(event.y));
        if (hasMousePosition_) {
            mouseDelta_ += position - mousePosition_;
        }
        // The first sample only establishes the origin: no jump on startup.
        mousePosition_ = position;
        hasMousePosition_ = true;
    });

    scrollSubscription_ = events.subscribe<MouseScrolled>([this](const MouseScrolled& event) {
        scrollDelta_ += Vec2(static_cast<f32>(event.deltaX), static_cast<f32>(event.deltaY));
    });

    focusSubscription_ = events.subscribe<WindowFocusChanged>([this](const WindowFocusChanged& event) {
        if (event.focused) {
            return;
        }
        keysReleased_ |= keysDown_;
        keysDown_.reset();
        buttonsReleased_ |= buttonsDown_;
        buttonsDown_.reset();
        // The cursor may come back anywhere: do not turn that into a jump.
        hasMousePosition_ = false;
    });

    gamepadConnectionSubscription_ =
        events.subscribe<GamepadConnected>([this](const GamepadConnected& event) {
            if (event.gamepad >= kMaxGamepads) {
                return;
            }
            Gamepad& pad = gamepads_[event.gamepad];
            if (!event.connected) {
                pad.released |= pad.down;  // unplugging releases what was held
                pad.down.reset();
                pad.axes.fill(0.0f);
            }
            pad.connected = event.connected;
        });
    gamepadButtonSubscription_ =
        events.subscribe<GamepadButtonEvent>([this](const GamepadButtonEvent& event) {
            const usize index = static_cast<usize>(event.button);
            if (event.gamepad >= kMaxGamepads || index >= Gamepad::kButtons) {
                return;
            }
            Gamepad& pad = gamepads_[event.gamepad];
            pad.down.set(index, event.pressed);
            (event.pressed ? pad.pressed : pad.released).set(index);
        });
    gamepadAxisSubscription_ = events.subscribe<GamepadAxisMoved>([this](const GamepadAxisMoved& event) {
        const usize index = static_cast<usize>(event.axis);
        if (event.gamepad < kMaxGamepads && index < gamepads_[event.gamepad].axes.size()) {
            gamepads_[event.gamepad].axes[index] = event.value;
        }
    });
}

void InputState::beginFrame() {
    keysPressed_.reset();
    keysReleased_.reset();
    buttonsPressed_.reset();
    buttonsReleased_.reset();
    for (Gamepad& pad : gamepads_) {
        pad.pressed.reset();
        pad.released.reset();
    }
    mouseDelta_ = Vec2(0.0f);
    scrollDelta_ = Vec2(0.0f);
}

bool InputState::keyDown(Key key) const {
    const usize index = static_cast<usize>(key);
    return index < kKeyCount && keysDown_.test(index);
}

bool InputState::keyPressed(Key key) const {
    const usize index = static_cast<usize>(key);
    return index < kKeyCount && keysPressed_.test(index);
}

bool InputState::keyReleased(Key key) const {
    const usize index = static_cast<usize>(key);
    return index < kKeyCount && keysReleased_.test(index);
}

bool InputState::mouseButtonDown(MouseButton button) const {
    const usize index = static_cast<usize>(button);
    return index < kButtonCount && buttonsDown_.test(index);
}

bool InputState::mouseButtonPressed(MouseButton button) const {
    const usize index = static_cast<usize>(button);
    return index < kButtonCount && buttonsPressed_.test(index);
}

bool InputState::mouseButtonReleased(MouseButton button) const {
    const usize index = static_cast<usize>(button);
    return index < kButtonCount && buttonsReleased_.test(index);
}

const InputState::Gamepad* InputState::gamepad(u32 index) const {
    return index < kMaxGamepads ? &gamepads_[index] : nullptr;
}

bool InputState::gamepadConnected(u32 gamepad) const {
    const Gamepad* pad = this->gamepad(gamepad);
    return pad != nullptr && pad->connected;
}

bool InputState::gamepadButtonDown(GamepadButton button, u32 gamepad) const {
    const Gamepad* pad = this->gamepad(gamepad);
    const usize index = static_cast<usize>(button);
    return pad != nullptr && index < Gamepad::kButtons && pad->down.test(index);
}

bool InputState::gamepadButtonPressed(GamepadButton button, u32 gamepad) const {
    const Gamepad* pad = this->gamepad(gamepad);
    const usize index = static_cast<usize>(button);
    return pad != nullptr && index < Gamepad::kButtons && pad->pressed.test(index);
}

bool InputState::gamepadButtonReleased(GamepadButton button, u32 gamepad) const {
    const Gamepad* pad = this->gamepad(gamepad);
    const usize index = static_cast<usize>(button);
    return pad != nullptr && index < Gamepad::kButtons && pad->released.test(index);
}

f32 InputState::gamepadAxis(GamepadAxis axis, u32 gamepad) const {
    const Gamepad* pad = this->gamepad(gamepad);
    const usize index = static_cast<usize>(axis);
    return pad != nullptr && index < pad->axes.size() ? pad->axes[index] : 0.0f;
}

}  // namespace phi::platform
