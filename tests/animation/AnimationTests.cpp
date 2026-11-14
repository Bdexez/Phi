#include <phi/animation/Animation.hpp>
#include <phi/render/Model.hpp>
#include <phi/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <numbers>

using namespace phi;
using namespace phi::animation;

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

TEST(Animation, PlayersRoundTripThroughSceneFilesByClipName) {
    Door door;
    ClipNames clips;
    clips.name(door.swing, "door/swing");
    scene::ComponentSerializers serializers = scene::ComponentSerializers::withSceneComponents();
    addComponentSerializers(serializers, clips);

    scene::Scene level("level");
    const ecs::Entity root = render::instantiateModel(level, door.model, render::ModelResources{}, "Door");
    AnimationPlayer& player = level.world().emplace<AnimationPlayer>(root, door.swing, false);
    player.time = 0.5f;
    player.speed = 2.0f;
    level.world().emplace<AnimationPlayer>(level.createEntity("Idle"));  // no clip
    const std::string text = serializers.save(level.world());
    EXPECT_NE(text.find("AnimationPlayer.clip = door/swing"), std::string::npos);

    scene::Scene loaded("loaded");
    ASSERT_TRUE(serializers.load(loaded.world(), text).has_value());
    const ecs::Entity copy = loaded.find("Door");
    const AnimationPlayer& restored = loaded.world().get<AnimationPlayer>(copy);
    EXPECT_EQ(restored.clip, door.swing);
    EXPECT_EQ(restored.time, 0.5f);
    EXPECT_EQ(restored.speed, 2.0f);
    EXPECT_FALSE(restored.loop);
    EXPECT_TRUE(restored.playing);
    EXPECT_EQ(loaded.world().get<AnimationPlayer>(loaded.find("Idle")).clip, nullptr);
    // And it animates its own nodes, found again through their indices:
    // 0.5 s more at double speed is the middle of the clip.
    updateAnimations(loaded.world(), 0.25f);
    EXPECT_NEAR(loaded.world().get<scene::Transform>(doorOf(loaded.world(), copy)).position.y, 2.0f, 1e-5f);

    // A clip nobody named, or a name nobody gave: refused when loading.
    scene::Scene stray("stray");
    stray.world().emplace<AnimationPlayer>(stray.createEntity("Stray"), std::make_shared<assets::ModelAnimation>());
    scene::Scene broken("broken");
    EXPECT_FALSE(serializers.load(broken.world(), serializers.save(stray.world())).has_value());
    ClipNames none;
    scene::ComponentSerializers strangers = scene::ComponentSerializers::withSceneComponents();
    addComponentSerializers(strangers, none);
    const auto unknown = strangers.load(broken.world(), text);
    ASSERT_FALSE(unknown.has_value());
    EXPECT_NE(unknown.error().message.find("door/swing"), std::string::npos);
    EXPECT_EQ(broken.world().entityCount(), 0u);  // nothing half loaded

    // Renaming: a name means one clip, a clip has one name.
    const auto other = std::make_shared<assets::ModelAnimation>();
    clips.name(door.swing, "door/open");
    EXPECT_EQ(clips.find("door/swing"), nullptr);
    EXPECT_EQ(clips.nameOf(door.swing.get()), "door/open");
    clips.name(other, "door/open");
    EXPECT_EQ(clips.find("door/open"), other);
    EXPECT_EQ(clips.nameOf(door.swing.get()), "");
}
