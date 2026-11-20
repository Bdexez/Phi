#pragma once

#include <bdex/core/Engine.hpp>
#include <bdex/platform/Actions.hpp>
#include <bdex/platform/Window.hpp>

#include <string>

namespace sandbox {

/// What the sandbox shows on top of the scene: the frame rate in the window
/// title, a stats panel (F12) and the list of controls (F1).
class Hud {
public:
    /// Once per frame: counts the frame, refreshes the title and the stats
    /// twice a second (often enough, and readable), and draws the panels.
    void update(bdex::Engine& engine, const bdex::FrameTime& time, bdex::usize entityCount,
                const bdex::platform::InputActions* actions, bdex::platform::IWindow* window);

    void toggleStats() { showStats_ = !showStats_; }
    void toggleHelp() { showHelp_ = !showHelp_; }

private:
    /// Every action and what drives it now, from the [input] bindings: a
    /// rebound key shows as rebound.
    [[nodiscard]] static std::string helpText(const bdex::platform::InputActions* actions);

    bdex::u32 frames_ = 0;
    bdex::f64 seconds_ = 0.0;
    std::string stats_;
    bool showStats_ = true;
    bool showHelp_ = false;
};

}  // namespace sandbox
