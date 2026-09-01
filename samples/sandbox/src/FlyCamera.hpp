#pragma once

#include <bdex/ecs/World.hpp>
#include <bdex/platform/Actions.hpp>
#include <bdex/platform/Input.hpp>
#include <bdex/platform/Window.hpp>

namespace sandbox {

/// Free-flying camera driven by input actions: mouse look while "look" is
/// held, or the look_* stick actions; move_* actions move it, analog
/// strength included, move_fast blends toward a higher speed.
class FlyCamera {
public:
    /// Takes over `camera`'s Transform rotation from yaw and pitch.
    void attach(bdex::ecs::World& world, bdex::ecs::Entity camera, bdex::f32 yaw, bdex::f32 pitch);
    /// Reads yaw and pitch back from the camera's current facing (after the
    /// scene was loaded, say).
    void adoptFacing(bdex::ecs::World& world, bdex::ecs::Entity camera);
    void update(bdex::ecs::World& world, const bdex::platform::InputActions& actions,
                const bdex::platform::InputState& input, bdex::platform::IWindow* window, bdex::f32 deltaSeconds);

    [[nodiscard]] bdex::ecs::Entity entity() const { return camera_; }

private:
    void applyRotation(bdex::ecs::World& world) const;

    bdex::ecs::Entity camera_;
    bdex::f32 yaw_ = 0.0f;
    bdex::f32 pitch_ = 0.0f;
    bool looking_ = false;
};

}  // namespace sandbox
