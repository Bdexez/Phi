#pragma once

#include <phi/core/Engine.hpp>
#include <phi/ecs/World.hpp>
#include <phi/platform/Input.hpp>
#include <phi/ui/Ui.hpp>

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
    PanelRequests draw(phi::Engine& engine, phi::ecs::World& world, const phi::platform::InputState* input,
                       const PanelControls& controls);
    void toggle() { visible_ = !visible_; }
    /// Whether clicks belong to the panel rather than the scene.
    [[nodiscard]] bool wantsPointer() const { return visible_ && ui_.wantsPointer(); }

private:
    void drawSun(phi::ecs::World& world);

    phi::ui::Context ui_;
    bool visible_ = true;
    bool lanternsSway_ = true;
    phi::f32 timeScale_ = 1.0f;
    bool paused_ = false;
};

}  // namespace sandbox
