#include "FlyCamera.hpp"

#include <bdex/scene/Components.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sandbox {

using namespace bdex;

void FlyCamera::attach(ecs::World& world, ecs::Entity camera, f32 yaw, f32 pitch) {
    camera_ = camera;
    yaw_ = yaw;
    pitch_ = pitch;
    applyRotation(world);
}

void FlyCamera::adoptFacing(ecs::World& world, ecs::Entity camera) {
    camera_ = camera;
    if (!world.has<scene::Transform>(camera_)) {
        return;
    }
    const Vec3 forward = world.get<scene::Transform>(camera_).forward();
    yaw_ = std::atan2(-forward.x, -forward.z);
    pitch_ = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
}

void FlyCamera::applyRotation(ecs::World& world) const {
    world.get<scene::Transform>(camera_).rotation =
        glm::angleAxis(yaw_, Vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(pitch_, Vec3(1.0f, 0.0f, 0.0f));
}

void FlyCamera::update(ecs::World& world, const platform::InputActions& actions, const platform::InputState& input,
                       platform::IWindow* window, f32 deltaSeconds) {
    if (!world.has<scene::Transform>(camera_)) {
        return;
    }

    // The cursor is captured while looking with the mouse.
    const bool looking = actions.down("look");
    if (looking != looking_) {
        looking_ = looking;
        if (window != nullptr) {
            window->setCursorCaptured(looking);
        }
    }
    // Mouse motion while looking, plus the right stick (radians per second
    // at full tilt).
    Vec2 turn = looking_ ? input.mouseDelta() * 0.0025f : Vec2(0.0f);
    turn += Vec2(actions.value("look_right") - actions.value("look_left"),
                 actions.value("look_down") - actions.value("look_up")) *
            (2.5f * deltaSeconds);
    if (turn != Vec2(0.0f)) {
        const f32 limit = std::numbers::pi_v<f32> * 0.49f;
        yaw_ -= turn.x;
        pitch_ = std::clamp(pitch_ - turn.y, -limit, limit);
        applyRotation(world);
    }

    scene::Transform& transform = world.get<scene::Transform>(camera_);
    Vec3 direction(0.0f);
    // Analog: a stick pushed halfway moves at half speed.
    direction += transform.forward() * (actions.value("move_forward") - actions.value("move_back"));
    direction += transform.right() * (actions.value("move_right") - actions.value("move_left"));
    direction.y += actions.value("move_up") - actions.value("move_down");
    if (const f32 length = glm::length(direction); length > 0.0f) {
        const f32 speed = glm::mix(5.0f, 14.0f, actions.value("move_fast"));
        transform.position += direction / std::max(length, 1.0f) * speed * deltaSeconds;
    }
}

}  // namespace sandbox
