#include <bdex/animation/Animation.hpp>
#include <bdex/render/Model.hpp>
#include <bdex/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <numbers>

using namespace bdex;
using namespace bdex::animation;

namespace {

/// A hinge (node 0) with a door (node 1) that swings a quarter turn about Y
/// and back over two seconds, and slides up then down.
struct Door {
    assets::ModelData model;
    std::shared_ptr<assets::ModelAnimation> swing = std::make_shared<assets::ModelAnimation>();

    Door() {
        assets::ModelNode hinge;
        hinge.name = "hinge";
        hinge.children = {1};
        assets::ModelNode door;
        door.name = "door";
        door.translation = Vec3(1.0f, 0.0f, 0.0f);
        model.nodes = {hinge, door};
        model.roots = {0};
        const f32 s = std::sin(std::numbers::pi_v<f32> / 4.0f);
        assets::AnimationChannel turn;
        turn.node = 1;
        turn.property = assets::AnimatedProperty::Rotation;
        turn.times = {0.0f, 1.0f, 2.0f};
        turn.values = {Vec4(0, 0, 0, 1), Vec4(0, s, 0, s), Vec4(0, 0, 0, 1)};
        assets::AnimationChannel lift;
        lift.node = 1;
        lift.times = {0.0f, 1.0f, 2.0f};
        lift.values = {Vec4(1, 0, 0, 0), Vec4(1, 2, 0, 0), Vec4(1, 0, 0, 0)};
        swing->name = "swing";
        swing->duration = 2.0f;
        swing->channels = {turn, lift};
    }
};

ecs::Entity doorOf(const ecs::World& world, ecs::Entity root) {
    for (const ecs::Entity entity : scene::branch(world, root)) {
        if (const auto* node = world.tryGet<scene::ModelNode>(entity); node != nullptr && node->index == 1) {
            return entity;
        }
    }
    return {};
}

}  // namespace

TEST(Animation, PlayersPoseTheirModelsNodes) {
    Door door;
    scene::Scene level("level");
    ecs::World& world = level.world();
    const ecs::Entity root = render::instantiateModel(level, door.model, render::ModelResources{}, "Door");
    world.emplace<AnimationPlayer>(root, door.swing);
    const ecs::Entity panel = doorOf(world, root);
    ASSERT_TRUE(world.alive(panel));

    updateAnimations(world, 1.0f);  // the middle: a quarter turn, lifted
    EXPECT_NEAR(glm::angle(world.get<scene::Transform>(panel).rotation), std::numbers::pi_v<f32> / 2.0f, 1e-4f);
    EXPECT_NEAR(world.get<scene::Transform>(panel).position.y, 2.0f, 1e-5f);
    updateAnimations(world, 0.5f);  // halfway back
    EXPECT_NEAR(world.get<scene::Transform>(panel).position.y, 1.0f, 1e-5f);
    updateAnimations(world, 1.0f);  // looped: 2.5 s is 0.5 s
    EXPECT_NEAR(world.get<AnimationPlayer>(root).time, 0.5f, 1e-5f);
    EXPECT_NEAR(world.get<scene::Transform>(panel).position.y, 1.0f, 1e-5f);

    // Without looping, it stops at the end, in the last pose.
    AnimationPlayer& player = world.get<AnimationPlayer>(root);
    player.loop = false;
    updateAnimations(world, 5.0f);
    EXPECT_FALSE(world.get<AnimationPlayer>(root).playing);
    EXPECT_FLOAT_EQ(world.get<AnimationPlayer>(root).time, 2.0f);
    EXPECT_NEAR(world.get<scene::Transform>(panel).position.y, 0.0f, 1e-5f);
}

TEST(Animation, ACopiedModelAnimatesItsOwnNodes) {
    Door door;
    scene::Scene level("level");
    ecs::World& world = level.world();
    const ecs::Entity original = render::instantiateModel(level, door.model, render::ModelResources{}, "Door");
    world.emplace<AnimationPlayer>(original, door.swing);
    updateAnimations(world, 0.0f);  // finds its nodes

    const ecs::Entity copy = scene::cloneHierarchy(world, original);
    world.get<AnimationPlayer>(copy).time = 1.0f;
    world.get<AnimationPlayer>(copy).playing = false;  // held at the top
    world.get<AnimationPlayer>(original).playing = false;  // held at the start
    updateAnimations(world, 0.1f);
    EXPECT_NEAR(world.get<scene::Transform>(doorOf(world, copy)).position.y, 2.0f, 1e-5f);
    EXPECT_NEAR(world.get<scene::Transform>(doorOf(world, original)).position.y, 0.0f, 1e-5f);

    // A pose without a player.
    applyAnimation(world, original, *door.swing, 0.5f);
    EXPECT_NEAR(world.get<scene::Transform>(doorOf(world, original)).position.y, 1.0f, 1e-5f);
}
