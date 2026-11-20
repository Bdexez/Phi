#pragma once

#include <bdex/ecs/World.hpp>
#include <bdex/platform/Actions.hpp>
#include <bdex/platform/Input.hpp>
#include <bdex/platform/Window.hpp>

namespace sandbox {

/// Camera driven by input actions: mouse look while "look" is held, or the
/// look_* stick actions; move_* actions move it, analog strength included,
/// move_fast blends toward a higher speed. It flies freely, or walks: a
/// body (a kinematic sphere) under the camera falls, walks on what it
/// stands on through physics::moveAndSlide, and jumps on move_up.
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

    /// Switches between flying and walking; walking puts a body, named
    /// "Player", under the camera.
    void setWalking(bdex::ecs::World& world, bool walking);
    [[nodiscard]] bool walking() const { return body_.valid(); }

private:
    void applyRotation(bdex::ecs::World& world) const;
    void walk(bdex::ecs::World& world, const bdex::platform::InputActions& actions, bdex::f32 deltaSeconds);
    void fly(bdex::ecs::World& world, const bdex::platform::InputActions& actions, bdex::f32 deltaSeconds);

    static constexpr bdex::f32 kBodyRadius = 0.4f;
    static constexpr bdex::f32 kEyeHeight = 1.3f;  ///< Above the body's center.

    bdex::ecs::Entity camera_;
    bdex::f32 yaw_ = 0.0f;
    bdex::f32 pitch_ = 0.0f;
    bool looking_ = false;
    bdex::ecs::Entity body_;
    bdex::f32 verticalSpeed_ = 0.0f;
    bool grounded_ = false;
    bdex::Vec3 groundVelocity_{0.0f};
};

}  // namespace sandbox
