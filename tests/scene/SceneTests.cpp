#include <bdex/render/Frustum.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <glm/gtc/epsilon.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace bdex;
using namespace bdex::scene;

namespace {

Vec3 worldPosition(ecs::World& world, ecs::Entity entity) {
    return Vec3(world.get<WorldTransform>(entity).matrix[3]);
}

void expectNear(const Vec3& actual, const Vec3& expected) {
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

}  // namespace

TEST(Scene, CreateEntityAddsStandardComponents) {
    Scene scene("test");
    const ecs::Entity entity = scene.createEntity("Player");
    EXPECT_EQ(scene.world().get<Name>(entity).value, "Player");
    EXPECT_TRUE((scene.world().has<Transform, WorldTransform>(entity)));
}

TEST(SceneManager, FirstSceneBecomesActive) {
    SceneManager scenes;
    EXPECT_EQ(scenes.active(), nullptr);
    Scene& first = scenes.create("first");
    Scene& second = scenes.create("second");
    EXPECT_EQ(scenes.active(), &first);

    scenes.setActive(&second);
    scenes.destroy(second);
    EXPECT_EQ(scenes.active(), nullptr);
    EXPECT_EQ(scenes.sceneCount(), 1u);
}

TEST(Transform, LocalMatrixAppliesScaleThenRotationThenTranslation) {
    Transform transform;
    transform.position = Vec3(10.0f, 0.0f, 0.0f);
    transform.rotation = glm::angleAxis(std::numbers::pi_v<f32> * 0.5f, Vec3(0.0f, 1.0f, 0.0f));
    transform.scale = Vec3(2.0f);

    // (1, 0, 0) -> scaled to (2, 0, 0) -> rotated 90 deg around Y to (0, 0, -2) -> translated.
    const Vec3 point = Vec3(transform.localMatrix() * Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    expectNear(point, Vec3(10.0f, 0.0f, -2.0f));

    expectNear(transform.forward(), Vec3(-1.0f, 0.0f, 0.0f));
    expectNear(transform.up(), Vec3(0.0f, 1.0f, 0.0f));
}

TEST(Hierarchy, ChildrenFollowTheirParents) {
    Scene scene("test");
    ecs::World& world = scene.world();

    // Created child-first on purpose: order must not matter.
    const ecs::Entity grandchild = scene.createEntity("grandchild");
    const ecs::Entity child = scene.createEntity("child");
    const ecs::Entity root = scene.createEntity("root");
    world.emplace<Parent>(child, root);
    world.emplace<Parent>(grandchild, child);

    world.get<Transform>(root).position = Vec3(0.0f, 5.0f, 0.0f);
    world.get<Transform>(root).rotation =
        glm::angleAxis(std::numbers::pi_v<f32> * 0.5f, Vec3(0.0f, 1.0f, 0.0f));
    world.get<Transform>(child).position = Vec3(1.0f, 0.0f, 0.0f);
    world.get<Transform>(grandchild).position = Vec3(1.0f, 0.0f, 0.0f);

    updateWorldTransforms(world);

    expectNear(worldPosition(world, root), Vec3(0.0f, 5.0f, 0.0f));
    // +X in the root's rotated frame is -Z in the world.
    expectNear(worldPosition(world, child), Vec3(0.0f, 5.0f, -1.0f));
    expectNear(worldPosition(world, grandchild), Vec3(0.0f, 5.0f, -2.0f));
}

TEST(Hierarchy, BrokenAndCyclicLinksDoNotHang) {
    Scene scene("test");
    ecs::World& world = scene.world();

    const ecs::Entity orphan = scene.createEntity("orphan");
    const ecs::Entity gone = scene.createEntity("gone");
    world.emplace<Parent>(orphan, gone);
    world.destroy(gone);
    world.get<Transform>(orphan).position = Vec3(3.0f, 0.0f, 0.0f);

    const ecs::Entity a = scene.createEntity("a");
    const ecs::Entity b = scene.createEntity("b");
    world.emplace<Parent>(a, b);
    world.emplace<Parent>(b, a);

    updateWorldTransforms(world);  // must terminate
    expectNear(worldPosition(world, orphan), Vec3(3.0f, 0.0f, 0.0f));
}

TEST(Hierarchy, CyclesKeepOneEntityAsRoot) {
    Scene scene("test");
    ecs::World& world = scene.world();
    const ecs::Entity a = scene.createEntity("a");
    const ecs::Entity b = scene.createEntity("b");
    world.emplace<Parent>(a, b);
    world.emplace<Parent>(b, a);
    world.get<Transform>(a).position = Vec3(1.0f, 0.0f, 0.0f);
    world.get<Transform>(b).position = Vec3(0.0f, 2.0f, 0.0f);

    updateWorldTransforms(world);

    // The link closing the cycle is ignored: one entity sits at its local
    // position, the other is offset by it.
    const Vec3 positionA = worldPosition(world, a);
    const Vec3 positionB = worldPosition(world, b);
    const bool aIsRoot = glm::all(glm::epsilonEqual(positionA, Vec3(1.0f, 0.0f, 0.0f), 1e-5f));
    const bool bIsRoot = glm::all(glm::epsilonEqual(positionB, Vec3(0.0f, 2.0f, 0.0f), 1e-5f));
    EXPECT_NE(aIsRoot, bIsRoot);
    expectNear(aIsRoot ? positionB : positionA, Vec3(1.0f, 2.0f, 0.0f));
}

TEST(Hierarchy, DeepChainsAreFullyResolved) {
    Scene scene("test");
    ecs::World& world = scene.world();

    // Deeper than any recursion guard would allow, created leaf-first.
    constexpr int kDepth = 500;
    std::vector<ecs::Entity> chain;
    for (int i = 0; i < kDepth; ++i) {
        chain.push_back(scene.createEntity("link"));
        world.get<Transform>(chain.back()).position = Vec3(1.0f, 0.0f, 0.0f);
    }
    for (int i = 0; i + 1 < kDepth; ++i) {
        world.emplace<Parent>(chain[i], chain[i + 1]);
    }

    updateWorldTransforms(world);
    expectNear(worldPosition(world, chain.front()), Vec3(static_cast<f32>(kDepth), 0.0f, 0.0f));
    expectNear(worldPosition(world, chain.back()), Vec3(1.0f, 0.0f, 0.0f));
}

TEST(Hierarchy, ParentsWithoutWorldTransformStillCount) {
    Scene scene("test");
    ecs::World& world = scene.world();

    const ecs::Entity pivot = world.create();  // Transform only, no WorldTransform
    world.emplace<Transform>(pivot).position = Vec3(0.0f, 0.0f, 4.0f);
    const ecs::Entity child = scene.createEntity("child");
    world.emplace<Parent>(child, pivot);

    updateWorldTransforms(world);
    expectNear(worldPosition(world, child), Vec3(0.0f, 0.0f, 4.0f));
}

TEST(Hierarchy, DestroyingARootTakesItsDescendants) {
    Scene scene("test");
    ecs::World& world = scene.world();
    const ecs::Entity root = scene.createEntity("root");
    const ecs::Entity child = scene.createEntity("child");
    const ecs::Entity grandchild = scene.createEntity("grandchild");
    const ecs::Entity sibling = scene.createEntity("sibling");
    const ecs::Entity bystander = scene.createEntity("bystander");
    world.emplace<Parent>(child, root);
    world.emplace<Parent>(sibling, root);
    world.emplace<Parent>(grandchild, child);

    EXPECT_EQ(destroyHierarchy(world, child), 2u);  // child + grandchild
    EXPECT_FALSE(world.alive(grandchild));
    EXPECT_TRUE(world.alive(root));
    EXPECT_TRUE(world.alive(sibling));

    EXPECT_EQ(destroyHierarchy(world, root), 2u);  // root + sibling
    EXPECT_FALSE(world.alive(sibling));
    EXPECT_TRUE(world.alive(bystander));
    EXPECT_EQ(destroyHierarchy(world, root), 0u);  // stale handle

    // Cycles below the root terminate.
    const ecs::Entity a = scene.createEntity("a");
    const ecs::Entity b = scene.createEntity("b");
    world.emplace<Parent>(a, b);
    world.emplace<Parent>(b, a);
    EXPECT_EQ(destroyHierarchy(world, a), 2u);
}

TEST(Camera, LooksDownNegativeZWithYUp) {
    Transform cameraTransform;
    cameraTransform.position = Vec3(0.0f, 0.0f, 5.0f);
    const render::CameraData camera =
        render::makeCameraData(cameraTransform.localMatrix(), render::Camera{}, 16.0f / 9.0f);

    expectNear(camera.position, Vec3(0.0f, 0.0f, 5.0f));

    const auto project = [&](const Vec3& world) {
        const Vec4 clip = camera.viewProjection * Vec4(world, 1.0f);
        return Vec3(clip) / clip.w;
    };

    // The origin is straight ahead: center of the screen, inside [0, 1] depth.
    const Vec3 center = project(Vec3(0.0f));
    EXPECT_NEAR(center.x, 0.0f, 1e-5f);
    EXPECT_NEAR(center.y, 0.0f, 1e-5f);
    EXPECT_GT(center.z, 0.0f);
    EXPECT_LT(center.z, 1.0f);

    // Clip space has Y pointing down: a point above the origin lands at y < 0,
    // a point to the right at x > 0.
    EXPECT_LT(project(Vec3(0.0f, 1.0f, 0.0f)).y, 0.0f);
    EXPECT_GT(project(Vec3(1.0f, 0.0f, 0.0f)).x, 0.0f);
}

TEST(Frustum, KeepsWhatTheCameraCanSee) {
    Transform cameraTransform;
    cameraTransform.position = Vec3(0.0f, 0.0f, 5.0f);  // looking down -Z
    render::Camera lens;
    lens.nearPlane = 0.1f;
    lens.farPlane = 100.0f;
    const render::CameraData camera =
        render::makeCameraData(cameraTransform.localMatrix(), lens, 16.0f / 9.0f);
    const render::Frustum frustum(camera.viewProjection);

    const auto visible = [&](const Vec3& center, f32 radius) {
        return frustum.intersects({center, radius});
    };
    EXPECT_TRUE(visible(Vec3(0.0f), 0.5f));                    // straight ahead
    EXPECT_FALSE(visible(Vec3(0.0f, 0.0f, 10.0f), 0.5f));      // behind the camera
    EXPECT_FALSE(visible(Vec3(0.0f, 0.0f, -200.0f), 1.0f));    // beyond the far plane
    EXPECT_FALSE(visible(Vec3(50.0f, 0.0f, 0.0f), 1.0f));      // far to the right
    EXPECT_FALSE(visible(Vec3(0.0f, -50.0f, 0.0f), 1.0f));     // far below
    EXPECT_TRUE(visible(Vec3(50.0f, 0.0f, 0.0f), 48.0f));      // large enough to reach in
    EXPECT_TRUE(visible(Vec3(0.0f, 0.0f, 5.5f), 1.0f));        // straddles the near plane
}

TEST(Frustum, BoundingSpheresFollowTransforms) {
    const render::BoundingSphere cube = render::computeBoundingSphere(assets::makeCube(2.0f));
    expectNear(cube.center, Vec3(0.0f));
    EXPECT_NEAR(cube.radius, std::sqrt(3.0f), 1e-5f);
    EXPECT_EQ(render::computeBoundingSphere({}).radius, 0.0f);

    Transform transform;
    transform.position = Vec3(1.0f, 2.0f, 3.0f);
    transform.rotation = glm::angleAxis(1.0f, glm::normalize(Vec3(1.0f, 1.0f, 0.0f)));
    transform.scale = Vec3(1.0f, 4.0f, 2.0f);
    const render::BoundingSphere moved = render::transformSphere(cube, transform.localMatrix());
    expectNear(moved.center, Vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(moved.radius, std::sqrt(3.0f) * 4.0f, 1e-4f);  // largest axis scale
}
