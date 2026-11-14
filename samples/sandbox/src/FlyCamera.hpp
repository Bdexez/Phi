#pragma once

#include <phi/ecs/World.hpp>
#include <phi/platform/Actions.hpp>
#include <phi/platform/Input.hpp>
#include <phi/platform/Window.hpp>

namespace sandbox {

/// Camera driven by input actions: mouse look while "look" is held, or the
/// look_* stick actions; move_* actions move it, analog strength included,
/// move_fast blends toward a higher speed. It flies freely, or walks: a
/// body (a kinematic sphere) under the camera falls, walks on what it
/// stands on through physics::moveAndSlide, and jumps on move_up.
class FlyCamera {
public:
    /// Takes over `camera`'s Transform rotation from yaw and pitch.
    void attach(phi::ecs::World& world, phi::ecs::Entity camera, phi::f32 yaw, phi::f32 pitch);
    /// Reads yaw and pitch back from the camera's current facing (after the
    /// scene was loaded, say).
    void adoptFacing(phi::ecs::World& world, phi::ecs::Entity camera);
    void update(phi::ecs::World& world, const phi::platform::InputActions& actions,
                const phi::platform::InputState& input, phi::platform::IWindow* window, phi::f32 deltaSeconds);

    [[nodiscard]] phi::ecs::Entity entity() const { return camera_; }

    /// Switches between flying and walking; walking puts a body, named
    /// "Player", under the camera.
    void setWalking(phi::ecs::World& world, bool walking);
    [[nodiscard]] bool walking() const { return body_.valid(); }

private:
    void applyRotation(phi::ecs::World& world) const;
    void walk(phi::ecs::World& world, const phi::platform::InputActions& actions, phi::f32 deltaSeconds);
    void fly(phi::ecs::World& world, const phi::platform::InputActions& actions, phi::f32 deltaSeconds);

    static constexpr phi::f32 kBodyRadius = 0.4f;
    static constexpr phi::f32 kEyeHeight = 1.3f;  ///< Above the body's center.

    phi::ecs::Entity camera_;
    phi::f32 yaw_ = 0.0f;
    phi::f32 pitch_ = 0.0f;
    bool looking_ = false;
    phi::ecs::Entity body_;
    phi::f32 verticalSpeed_ = 0.0f;
    bool grounded_ = false;
    phi::Vec3 groundVelocity_{0.0f};
};

}  // namespace sandbox
