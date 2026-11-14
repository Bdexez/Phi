#include "FlyCamera.hpp"

#include <phi/physics/Physics.hpp>
#include <phi/scene/Components.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sandbox {

using namespace phi;

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

    if (body_.valid() && !world.alive(body_)) {
        body_ = {};  // the scene was replaced under us
    }
    if (body_.valid()) {
        walk(world, actions, deltaSeconds);
    } else {
        fly(world, actions, deltaSeconds);
    }
}

void FlyCamera::setWalking(ecs::World& world, bool walking) {
    if (walking == body_.valid() || !world.has<scene::Transform>(camera_)) {
        return;
    }
    if (walking) {
        body_ = world.create();
        world.emplace<scene::Name>(body_, "Player");
        world.emplace<scene::Transient>(body_);  // not saved: walking is a mode, not part of the level
        world.emplace<scene::Transform>(body_).position =
            world.get<scene::Transform>(camera_).position - Vec3(0.0f, kEyeHeight, 0.0f);
        world.emplace<physics::SphereCollider>(body_, kBodyRadius);  // no RigidBody: kinematic
        verticalSpeed_ = 0.0f;
        grounded_ = false;
    } else {
        world.destroy(body_);
        body_ = {};
    }
}

void FlyCamera::walk(ecs::World& world, const platform::InputActions& actions, f32 deltaSeconds) {
    // Walking stays level whatever the pitch.
    const scene::Transform& eye = world.get<scene::Transform>(camera_);
    const auto flat = [](Vec3 v) {
        v.y = 0.0f;
        const f32 length = glm::length(v);
        return length > 1e-4f ? v / length : Vec3(0.0f);
    };
    Vec3 wish = flat(eye.forward()) * (actions.value("move_forward") - actions.value("move_back")) +
                flat(eye.right()) * (actions.value("move_right") - actions.value("move_left"));
    if (const f32 length = glm::length(wish); length > 1.0f) {
        wish /= length;
    }
    const f32 speed = glm::mix(4.0f, 8.0f, actions.value("move_fast"));

    constexpr f32 kGravity = 18.0f;  // snappier than 9.81 for a game feel
    constexpr f32 kJumpSpeed = 6.5f;
    if (grounded_ && actions.pressed("move_up")) {
        verticalSpeed_ = kJumpSpeed;
    }
    verticalSpeed_ -= kGravity * deltaSeconds;
    // Standing on a moving platform, ride along with it.
    const Vec3 carried = grounded_ ? groundVelocity_ * deltaSeconds : Vec3(0.0f);
    const physics::MoveResult moved = physics::moveAndSlide(
        world, body_, wish * (speed * deltaSeconds) + carried + Vec3(0.0f, verticalSpeed_ * deltaSeconds, 0.0f));
    grounded_ = moved.grounded;
    groundVelocity_ = moved.grounded ? moved.groundVelocity : Vec3(0.0f);
    if (grounded_ && verticalSpeed_ < 0.0f) {
        verticalSpeed_ = -1.0f;  // keep pressing on the ground, to keep touching it
    } else if (moved.blocked && verticalSpeed_ > 0.0f && moved.moved.y < verticalSpeed_ * deltaSeconds * 0.5f) {
        verticalSpeed_ = 0.0f;  // bumped a ceiling
    }
    world.get<scene::Transform>(camera_).position =
        world.get<scene::Transform>(body_).position + Vec3(0.0f, kEyeHeight, 0.0f);
}

void FlyCamera::fly(ecs::World& world, const platform::InputActions& actions, f32 deltaSeconds) {
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
