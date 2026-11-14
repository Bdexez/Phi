#pragma once

#include <phi/core/Engine.hpp>
#include <phi/platform/Actions.hpp>
#include <phi/platform/Window.hpp>

#include <string>

namespace sandbox {

/// What the sandbox shows on top of the scene: the frame rate in the window
/// title, a stats panel (F12) and the list of controls (F1).
class Hud {
public:
    /// Once per frame: counts the frame, refreshes the title and the stats
    /// twice a second (often enough, and readable), and draws the panels.
    void update(phi::Engine& engine, const phi::FrameTime& time, phi::usize entityCount,
                const phi::platform::InputActions* actions, phi::platform::IWindow* window);

    void toggleStats() { showStats_ = !showStats_; }
    void toggleHelp() { showHelp_ = !showHelp_; }

private:
    /// Every action and what drives it now, from the [input] bindings: a
    /// rebound key shows as rebound.
    [[nodiscard]] static std::string helpText(const phi::platform::InputActions* actions);

    phi::u32 frames_ = 0;
    phi::f64 seconds_ = 0.0;
    std::string stats_;
    bool showStats_ = true;
    bool showHelp_ = false;
};

}  // namespace sandbox
