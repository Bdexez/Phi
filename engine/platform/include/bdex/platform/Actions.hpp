#pragma once

#include <bdex/core/Config.hpp>
#include <bdex/core/Result.hpp>
#include <bdex/platform/Input.hpp>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bdex::platform {

/// Named gameplay actions bound to keys and mouse buttons, so code asks for
/// "jump" rather than for Space, and players can rebind from configuration:
///
///   [input]
///   move_forward = W, Up
///   fire = MouseLeft, F
///
///   if (actions.pressed("fire")) { ... }
///
/// Names are those of parseKey and parseMouseButton. An action with several
/// bindings is down while any of them is.
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
    /// True during the frame the action stopped being down.
    [[nodiscard]] bool released(std::string_view action) const;
    /// The action's bindings, by name (for display in menus or hints).
    [[nodiscard]] std::vector<std::string> bindings(std::string_view action) const;

private:
    struct Binding {
        bool mouse = false;
        u16 code = 0;  ///< Key or MouseButton value.
    };

    [[nodiscard]] bool any(std::string_view action, bool (InputActions::*test)(const Binding&) const) const;
    [[nodiscard]] bool isDown(const Binding& binding) const;
    [[nodiscard]] bool wasPressed(const Binding& binding) const;
    [[nodiscard]] bool wasReleased(const Binding& binding) const;

    const InputState* state_;
    std::map<std::string, std::vector<Binding>, std::less<>> actions_;
};

}  // namespace bdex::platform
