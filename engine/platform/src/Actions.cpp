#include <bdex/platform/Actions.hpp>

namespace bdex::platform {
namespace {

std::string_view trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t";
    const usize begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(whitespace) - begin + 1);
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
        if (const auto key = parseKey(name)) {
            bindings.push_back({false, static_cast<u16>(*key)});
        } else if (const auto button = parseMouseButton(name)) {
            bindings.push_back({true, static_cast<u16>(*button)});
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

bool InputActions::isDown(const Binding& binding) const {
    return binding.mouse ? state_->mouseButtonDown(static_cast<MouseButton>(binding.code))
                         : state_->keyDown(static_cast<Key>(binding.code));
}

bool InputActions::wasPressed(const Binding& binding) const {
    return binding.mouse ? state_->mouseButtonPressed(static_cast<MouseButton>(binding.code))
                         : state_->keyPressed(static_cast<Key>(binding.code));
}

bool InputActions::wasReleased(const Binding& binding) const {
    return binding.mouse ? state_->mouseButtonReleased(static_cast<MouseButton>(binding.code))
                         : state_->keyReleased(static_cast<Key>(binding.code));
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
            names.emplace_back(binding.mouse ? mouseButtonName(static_cast<MouseButton>(binding.code))
                                             : keyName(static_cast<Key>(binding.code)));
        }
    }
    return names;
}

}  // namespace bdex::platform
