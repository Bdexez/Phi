#pragma once

#include <bdex/core/Engine.hpp>
#include <bdex/ecs/World.hpp>
#include <bdex/platform/Input.hpp>
#include <bdex/ui/Ui.hpp>

namespace sandbox {

/// Settings the sandbox owns and lends to the panel.
struct PanelControls {
    bool& platformMoving;
    bool& sparks;
    bool& gizmos;
};

/// What the panel's buttons asked for this frame.
struct PanelRequests {
    bool throwCrate = false;
    bool throwBall = false;
    bool clearThrows = false;
};

/// Live settings on the right of the screen: the sun, the scene, time.
class SettingsPanel {
public:
    PanelRequests draw(bdex::Engine& engine, bdex::ecs::World& world, const bdex::platform::InputState* input,
                       const PanelControls& controls);
    void toggle() { visible_ = !visible_; }
    /// Whether clicks belong to the panel rather than the scene.
    [[nodiscard]] bool wantsPointer() const { return visible_ && ui_.wantsPointer(); }

private:
    void drawSun(bdex::ecs::World& world);

    bdex::ui::Context ui_;
    bool visible_ = true;
    bool lanternsSway_ = true;
    bdex::f32 timeScale_ = 1.0f;
    bool paused_ = false;
};

}  // namespace sandbox
