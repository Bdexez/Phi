#include <phi/platform/Actions.hpp>

#include <algorithm>
#include <iterator>

namespace phi::platform {
namespace {

std::string_view trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t";
    const usize begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(whitespace) - begin + 1);
}

struct AxisName {
    std::string_view name;
    GamepadAxis axis;
    f32 sign;
};
constexpr AxisName kAxisNames[] = {
    {"LeftStickUp", GamepadAxis::LeftY, -1.0f},   {"LeftStickDown", GamepadAxis::LeftY, 1.0f},
    {"LeftStickLeft", GamepadAxis::LeftX, -1.0f}, {"LeftStickRight", GamepadAxis::LeftX, 1.0f},
    {"RightStickUp", GamepadAxis::RightY, -1.0f}, {"RightStickDown", GamepadAxis::RightY, 1.0f},
    {"RightStickLeft", GamepadAxis::RightX, -1.0f}, {"RightStickRight", GamepadAxis::RightX, 1.0f},
    {"LeftTrigger", GamepadAxis::LeftTrigger, 1.0f}, {"RightTrigger", GamepadAxis::RightTrigger, 1.0f},
};

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) { return lower(x) == lower(y); });
}

}  // namespace

Result<> InputActions::bind(std::string_view action, std::string_view inputs) {
    if (trim(action).empty()) {
        return fail("input action with an empty name");
    }
    std::vector<Binding> bindings;
    while (!inputs.empty()) {
        const usize comma = inputs.find(',');
        const std::string_view name = trim(inputs.substr(0, comma));
        inputs = comma == std::string_view::npos ? std::string_view{} : inputs.substr(comma + 1);
        if (name.empty()) {
            continue;
        }
        using Kind = Binding::Kind;
        const auto axis = std::find_if(std::begin(kAxisNames), std::end(kAxisNames),
                                       [&](const AxisName& entry) { return equalsIgnoringCase(entry.name, name); });
        if (const auto key = parseKey(name)) {
            bindings.push_back({Kind::Key, static_cast<u16>(*key)});
        } else if (const auto button = parseMouseButton(name)) {
            bindings.push_back({Kind::Mouse, static_cast<u16>(*button)});
        } else if (const auto padButton = parseGamepadButton(name)) {
            bindings.push_back({Kind::GamepadButton, static_cast<u16>(*padButton)});
        } else if (axis != std::end(kAxisNames)) {
            bindings.push_back({Kind::GamepadAxis, static_cast<u16>(axis->axis), axis->sign});
        } else {
            return fail("input action '{}': unknown key or mouse button '{}'", action, name);
        }
    }
    actions_.insert_or_assign(std::string(trim(action)), std::move(bindings));
    return {};
}

Result<> InputActions::load(const Config& config, std::string_view section) {
    const std::string prefix = std::string(section) + ".";
    Result<> first;
    for (const auto& [key, value] : config.entries()) {
        if (!key.starts_with(prefix)) {
            continue;
        }
        if (auto bound = bind(std::string_view(key).substr(prefix.size()), value); !bound && first) {
            first = std::move(bound);
        }
    }
    return first;
}

bool InputActions::has(std::string_view action) const {
    return actions_.find(action) != actions_.end();
}

bool InputActions::any(std::string_view action, bool (InputActions::*test)(const Binding&) const) const {
    const auto it = actions_.find(action);
    if (it == actions_.end()) {
        return false;
    }
    for (const Binding& binding : it->second) {
        if ((this->*test)(binding)) {
            return true;
        }
    }
    return false;
}

namespace {

// Any gamepad drives the action. (A disconnected one has no button down
// and centered axes, but still reports the releases unplugging caused.)
template <class Test>
bool anyGamepad(Test&& test) {
    for (u32 pad = 0; pad < kMaxGamepads; ++pad) {
        if (test(pad)) {
            return true;
        }
    }
    return false;
}

}  // namespace

f32 InputActions::strength(const Binding& binding) const {
    switch (binding.kind) {
        case Binding::Kind::Key:
            return state_->keyDown(static_cast<Key>(binding.code)) ? 1.0f : 0.0f;
        case Binding::Kind::Mouse:
            return state_->mouseButtonDown(static_cast<MouseButton>(binding.code)) ? 1.0f : 0.0f;
        case Binding::Kind::GamepadButton:
            return anyGamepad([&](u32 pad) {
                       return state_->gamepadButtonDown(static_cast<GamepadButton>(binding.code), pad);
                   })
                       ? 1.0f
                       : 0.0f;
        case Binding::Kind::GamepadAxis: {
            f32 strongest = 0.0f;
            for (u32 pad = 0; pad < kMaxGamepads; ++pad) {
                const f32 travel = binding.sign * state_->gamepadAxis(static_cast<GamepadAxis>(binding.code), pad);
                if (travel > deadZone_) {
                    strongest = std::max(strongest, std::min((travel - deadZone_) / (1.0f - deadZone_), 1.0f));
                }
            }
            return strongest;
        }
    }
    return 0.0f;
}

bool InputActions::isDown(const Binding& binding) const {
    return strength(binding) > 0.5f;
}

bool InputActions::wasPressed(const Binding& binding) const {
    switch (binding.kind) {
        case Binding::Kind::Key: return state_->keyPressed(static_cast<Key>(binding.code));
        case Binding::Kind::Mouse:
            return state_->mouseButtonPressed(static_cast<MouseButton>(binding.code));
        case Binding::Kind::GamepadButton:
            return anyGamepad([&](u32 pad) {
                return state_->gamepadButtonPressed(static_cast<GamepadButton>(binding.code), pad);
            });
        case Binding::Kind::GamepadAxis: return false;
    }
    return false;
}

bool InputActions::wasReleased(const Binding& binding) const {
    switch (binding.kind) {
        case Binding::Kind::Key: return state_->keyReleased(static_cast<Key>(binding.code));
        case Binding::Kind::Mouse:
            return state_->mouseButtonReleased(static_cast<MouseButton>(binding.code));
        case Binding::Kind::GamepadButton:
            return anyGamepad([&](u32 pad) {
                return state_->gamepadButtonReleased(static_cast<GamepadButton>(binding.code), pad);
            });
        case Binding::Kind::GamepadAxis: return false;
    }
    return false;
}

f32 InputActions::value(std::string_view action) const {
    f32 strongest = 0.0f;
    if (const auto it = actions_.find(action); it != actions_.end()) {
        for (const Binding& binding : it->second) {
            strongest = std::max(strongest, strength(binding));
        }
    }
    return strongest;
}

void InputActions::setDeadZone(f32 deadZone) {
    deadZone_ = std::clamp(deadZone, 0.0f, 0.95f);
}

bool InputActions::down(std::string_view action) const {
    return any(action, &InputActions::isDown);
}

bool InputActions::pressed(std::string_view action) const {
    return any(action, &InputActions::wasPressed);
}

bool InputActions::released(std::string_view action) const {
    // Releasing one binding while another is still held keeps the action down.
    return any(action, &InputActions::wasReleased) && !down(action);
}

std::vector<std::string> InputActions::bindings(std::string_view action) const {
    std::vector<std::string> names;
    if (const auto it = actions_.find(action); it != actions_.end()) {
        for (const Binding& binding : it->second) {
            switch (binding.kind) {
                case Binding::Kind::Key: names.emplace_back(keyName(static_cast<Key>(binding.code))); break;
                case Binding::Kind::Mouse:
                    names.emplace_back(mouseButtonName(static_cast<MouseButton>(binding.code)));
                    break;
                case Binding::Kind::GamepadButton:
                    names.emplace_back(gamepadButtonName(static_cast<GamepadButton>(binding.code)));
                    break;
                case Binding::Kind::GamepadAxis:
                    for (const AxisName& entry : kAxisNames) {
                        if (static_cast<u16>(entry.axis) == binding.code && entry.sign == binding.sign) {
                            names.emplace_back(entry.name);
                        }
                    }
                    break;
            }
        }
    }
    return names;
}

}  // namespace phi::platform
