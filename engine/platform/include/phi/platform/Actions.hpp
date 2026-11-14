#pragma once

#include <phi/core/Config.hpp>
#include <phi/core/Result.hpp>
#include <phi/platform/Input.hpp>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace phi::platform {

/// Named gameplay actions bound to keys and mouse buttons, so code asks for
/// "jump" rather than for Space, and players can rebind from configuration:
///
///   [input]
///   move_forward = W, Up
///   fire = MouseLeft, F
///
///   if (actions.pressed("fire")) { ... }
///
/// Bindings are keys and mouse buttons (parseKey, parseMouseButton), gamepad
/// buttons (parseGamepadButton) and gamepad axis directions:
/// "LeftStickUp/Down/Left/Right", "RightStick..." and "LeftTrigger" /
/// "RightTrigger", read on any connected gamepad. An action is down while
/// any binding is (an axis counts once pushed past halfway), and value()
/// gives its analog strength.
///
///   move_forward = W, Up, LeftStickUp
///   if (f32 throttle = actions.value("move_forward"); throttle > 0) ...
class InputActions {
public:
    explicit InputActions(const InputState& state) : state_(&state) {}

    /// Binds `action` to a comma-separated list of key and mouse button
    /// names, replacing its bindings. Fails, changing nothing, if a name is
    /// unknown.
    Result<> bind(std::string_view action, std::string_view inputs);
    /// Binds every "<section>.<action> = <inputs>" entry of `config`. The
    /// valid entries are bound even if another one fails; the first failure
    /// is reported.
    Result<> load(const Config& config, std::string_view section = "input");

    [[nodiscard]] bool has(std::string_view action) const;
    /// Unknown actions are never down, pressed or released.
    [[nodiscard]] bool down(std::string_view action) const;
    /// True during the frame one of its bindings went down.
    [[nodiscard]] bool pressed(std::string_view action) const;
    /// True during the frame the action stopped being down. Like pressed(),
    /// only reported for digital bindings (keys and buttons).
    [[nodiscard]] bool released(std::string_view action) const;
    /// Strength in [0, 1]: 1 for a held key or button, how far an axis is
    /// pushed past the dead zone otherwise; the strongest binding wins.
    [[nodiscard]] f32 value(std::string_view action) const;

    /// Axis travel ignored around rest, in [0, 1) (default 0.2), so worn
    /// sticks do not drift.
    void setDeadZone(f32 deadZone);
    /// The action's bindings, by name (for display in menus or hints).
    [[nodiscard]] std::vector<std::string> bindings(std::string_view action) const;

private:
    struct Binding {
        enum class Kind : u8 { Key, Mouse, GamepadButton, GamepadAxis };
        Kind kind = Kind::Key;
        u16 code = 0;    ///< Key, MouseButton, GamepadButton or GamepadAxis value.
        f32 sign = 1.0f;  ///< Direction of travel, for axes.
    };

    [[nodiscard]] bool any(std::string_view action, bool (InputActions::*test)(const Binding&) const) const;
    [[nodiscard]] bool isDown(const Binding& binding) const;
    [[nodiscard]] bool wasPressed(const Binding& binding) const;
    [[nodiscard]] bool wasReleased(const Binding& binding) const;
    [[nodiscard]] f32 strength(const Binding& binding) const;

    const InputState* state_;
    std::map<std::string, std::vector<Binding>, std::less<>> actions_;
    f32 deadZone_ = 0.2f;
};

}  // namespace phi::platform
