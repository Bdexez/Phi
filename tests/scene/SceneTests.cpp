#include <bdex/render/DebugDraw.hpp>
#include <bdex/render/Frustum.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/render/ResourceNames.hpp>
#include <bdex/render/Serialization.hpp>
#include <bdex/render/Shadows.hpp>
#include <bdex/scene/Scene.hpp>
#include <bdex/scene/Serialization.hpp>

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

TEST(Scene, FindsEntitiesByName) {
    Scene scene("test");
    const ecs::Entity player = scene.createEntity("Player");
    const ecs::Entity first = scene.createEntity("Crate");
    scene.createEntity("Lamp");
    const ecs::Entity second = scene.createEntity("Crate");

    EXPECT_EQ(scene.find("Player"), player);
    EXPECT_EQ(scene.find("Crate"), first);
    EXPECT_EQ(scene.findAll("Crate"), (std::vector<ecs::Entity>{first, second}));
    EXPECT_FALSE(scene.find("Ghost").valid());
    EXPECT_TRUE(scene.findAll("Ghost").empty());

    scene.world().destroy(first);
    EXPECT_EQ(scene.find("Crate"), second);
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

TEST(Shadows, TheLightVolumeCoversTheNearbyView) {
    Transform cameraTransform;
    cameraTransform.position = Vec3(3.0f, 2.0f, 8.0f);
    cameraTransform.rotation = glm::angleAxis(0.4f, Vec3(0.0f, 1.0f, 0.0f));
    const render::CameraData camera =
        render::makeCameraData(cameraTransform.localMatrix(), render::Camera{}, 16.0f / 9.0f);
    const Vec3 sun = glm::normalize(Vec3(-0.4f, -1.0f, -0.3f));
    const render::CameraData light = render::fitDirectionalShadow(camera, sun, 30.0f, 2048);

    // Every point the camera sees within the shadow distance lands inside
    // the shadow map, depth included.
    const Mat4 inverseViewProjection = glm::inverse(camera.viewProjection);
    for (const f32 x : {-1.0f, 0.0f, 1.0f}) {
        for (const f32 y : {-1.0f, 0.0f, 1.0f}) {
            const Vec4 far = inverseViewProjection * Vec4(x, y, 1.0f, 1.0f);
            const Vec3 direction = glm::normalize(Vec3(far) / far.w - camera.position);
            for (const f32 along : {0.5f, 10.0f, 29.0f}) {
                const Vec4 clip = light.viewProjection * Vec4(camera.position + direction * along, 1.0f);
                const Vec3 ndc = Vec3(clip) / clip.w;
                EXPECT_LE(std::abs(ndc.x), 1.0f);
                EXPECT_LE(std::abs(ndc.y), 1.0f);
                EXPECT_GE(ndc.z, 0.0f);
                EXPECT_LE(ndc.z, 1.0f);
            }
        }
    }
}

TEST(Shadows, TheLightVolumeMovesByWholeTexels) {
    const Vec3 sun = glm::normalize(Vec3(-0.4f, -1.0f, -0.3f));
    constexpr u32 kMapSize = 1024;
    const auto lightFor = [&](const Vec3& position, f32 yaw) {
        Transform transform;
        transform.position = position;
        transform.rotation = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
        return render::fitDirectionalShadow(
            render::makeCameraData(transform.localMatrix(), render::Camera{}, 1.5f), sun, 40.0f, kMapSize);
    };
    const render::CameraData a = lightFor(Vec3(0.0f), 0.0f);

    // Turning the camera keeps the volume's size.
    EXPECT_FLOAT_EQ(lightFor(Vec3(0.0f), 1.3f).projection[0][0], a.projection[0][0]);

    // Moving it shifts a fixed world point by a whole number of texels.
    const render::CameraData b = lightFor(Vec3(0.37f, 0.0f, -0.21f), 0.0f);
    const Vec4 point(5.0f, 1.0f, -3.0f, 1.0f);
    const Vec2 uvA = Vec2(a.viewProjection * point) * 0.5f * static_cast<f32>(kMapSize);
    const Vec2 uvB = Vec2(b.viewProjection * point) * 0.5f * static_cast<f32>(kMapSize);
    const Vec2 shift = uvB - uvA;
    EXPECT_NEAR(shift.x, std::round(shift.x), 1e-2f);
    EXPECT_NEAR(shift.y, std::round(shift.y), 1e-2f);
}

TEST(DebugDraw, BuildsLineListsForShapes) {
    render::DebugDraw debug;
    debug.line(Vec3(0.0f), Vec3(1.0f), Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(debug.lineCount(), 1u);
    EXPECT_EQ(debug.vertices()[1].position, Vec3(1.0f));
    EXPECT_EQ(debug.vertices()[0].color, Vec4(1.0f, 0.0f, 0.0f, 1.0f));

    debug.clear();
    debug.box(Vec3(-1.0f), Vec3(1.0f));
    EXPECT_EQ(debug.lineCount(), 12u);
    for (usize i = 0; i < debug.vertices().size(); i += 2) {
        // Every edge has length 2 and runs along one axis.
        const Vec3 edge = debug.vertices()[i + 1].position - debug.vertices()[i].position;
        EXPECT_FLOAT_EQ(glm::length(edge), 2.0f);
    }

    // Oriented: a cube turned 45 degrees about Y reaches sqrt(2) along X.
    debug.clear();
    Transform turned;
    turned.rotation = glm::angleAxis(glm::radians(45.0f), Vec3(0.0f, 1.0f, 0.0f));
    debug.box(turned.localMatrix(), Vec3(1.0f));
    EXPECT_EQ(debug.lineCount(), 12u);
    f32 reach = 0.0f;
    for (const render::DebugDraw::Vertex& vertex : debug.vertices()) {
        reach = std::max(reach, vertex.position.x);
    }
    EXPECT_NEAR(reach, std::sqrt(2.0f), 1e-5f);

    debug.clear();
    debug.sphere(Vec3(1.0f, 2.0f, 3.0f), 2.0f, Vec4(1.0f), 16);
    EXPECT_EQ(debug.lineCount(), 3u * 16u);
    for (const render::DebugDraw::Vertex& vertex : debug.vertices()) {
        EXPECT_NEAR(glm::length(vertex.position - Vec3(1.0f, 2.0f, 3.0f)), 2.0f, 1e-5f);
    }

    debug.clear();
    debug.axes(Mat4(1.0f), 2.0f);
    ASSERT_EQ(debug.lineCount(), 3u);
    EXPECT_EQ(debug.vertices()[3].position, Vec3(0.0f, 2.0f, 0.0f));  // Y axis, green
    EXPECT_EQ(debug.vertices()[3].color, Vec4(0.0f, 1.0f, 0.0f, 1.0f));

    // A frustum's corners project back onto the clip volume's corners.
    debug.clear();
    const render::CameraData camera = render::makeCameraData(Mat4(1.0f), render::Camera{}, 1.0f);
    debug.frustum(camera.viewProjection);
    EXPECT_EQ(debug.lineCount(), 12u);
    for (const render::DebugDraw::Vertex& vertex : debug.vertices()) {
        const Vec4 clip = camera.viewProjection * Vec4(vertex.position, 1.0f);
        EXPECT_NEAR(std::abs(clip.x / clip.w), 1.0f, 1e-3f);
        EXPECT_NEAR(std::abs(clip.y / clip.w), 1.0f, 1e-3f);
    }
}

TEST(Picking, RaysThroughPixelsHitWhatIsDrawnThere) {
    Transform eye;
    eye.position = Vec3(0.0f, 0.0f, 10.0f);
    const render::CameraData camera = render::makeCameraData(eye.localMatrix(), render::Camera{}, 2.0f);
    const Extent2D extent{200, 100};

    // The center pixel looks straight ahead.
    const render::Ray center = render::rayThroughPixel(camera.viewProjection, Vec2(100.0f, 50.0f), extent);
    expectNear(center.direction, Vec3(0.0f, 0.0f, -1.0f));
    EXPECT_NEAR(center.origin.z, 10.0f - render::Camera{}.nearPlane, 1e-3f);

    // A point projected on screen is hit by the ray through its pixel.
    const Vec3 target(2.0f, 1.0f, -3.0f);
    const Vec4 clip = camera.viewProjection * Vec4(target, 1.0f);
    const Vec2 pixel((clip.x / clip.w * 0.5f + 0.5f) * 200.0f, (clip.y / clip.w * 0.5f + 0.5f) * 100.0f);
    const render::Ray ray = render::rayThroughPixel(camera.viewProjection, pixel, extent);
    const auto hit = render::intersect(ray, {target, 0.5f});
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(glm::length(ray.origin + ray.direction * (*hit) - target), 0.5f, 1e-3f);

    // Misses and spheres behind the ray.
    EXPECT_FALSE(render::intersect(ray, {target + Vec3(3.0f, 0.0f, 0.0f), 0.5f}).has_value());
    EXPECT_FALSE(render::intersect(center, {Vec3(0.0f, 0.0f, 20.0f), 1.0f}).has_value());
    EXPECT_EQ(render::intersect(center, {center.origin, 1.0f}), 0.0f);  // inside
}

// --- Serialization -----------------------------------------------------------------

TEST(SceneFiles, RoundTripAHierarchy) {
    Scene source("source");
    ecs::World& world = source.world();
    world.destroy(source.createEntity("gap"));  // slots need not be dense
    const ecs::Entity root = source.createEntity("Root");
    const ecs::Entity child = source.createEntity("Child = with equals");
    world.emplace<Parent>(child, root);
    world.get<Transform>(root).position = Vec3(1.5f, -2.0f, 0.1f);
    world.get<Transform>(root).rotation = glm::angleAxis(0.7f, glm::normalize(Vec3(1.0f, 2.0f, 3.0f)));
    world.get<Transform>(child).scale = Vec3(0.25f, 3.0f, 1.0f);
    const ecs::Entity bare = world.create();  // nothing registered: not saved
    (void)bare;

    const ComponentSerializers serializers = ComponentSerializers::withSceneComponents();
    const std::string text = serializers.save(world);

    Scene target("target");
    target.createEntity("already here");  // loading adds to what exists
    const auto loaded = serializers.load(target.world(), text);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message << "\n" << text;
    ASSERT_EQ(loaded->size(), 2u);
    EXPECT_EQ(target.world().entityCount(), 3u);

    const ecs::Entity newRoot = target.find("Root");
    const ecs::Entity newChild = target.find("Child = with equals");
    ASSERT_TRUE(newRoot.valid());
    ASSERT_TRUE(newChild.valid());
    EXPECT_EQ(target.world().get<Parent>(newChild).entity, newRoot);
    EXPECT_TRUE(target.world().has<WorldTransform>(newChild));
    // Floats are written in their shortest exact form: values come back bit for bit.
    EXPECT_EQ(target.world().get<Transform>(newRoot).position, world.get<Transform>(root).position);
    EXPECT_EQ(target.world().get<Transform>(newChild).scale, world.get<Transform>(child).scale);
    const Quat rotation = target.world().get<Transform>(newRoot).rotation;
    EXPECT_NEAR(std::abs(glm::dot(rotation, world.get<Transform>(root).rotation)), 1.0f, 1e-6f);

    // Saving the loaded world gives the same file.
    Scene again("again");
    ASSERT_TRUE(serializers.load(again.world(), text).has_value());
    EXPECT_EQ(serializers.save(again.world()), text);
}

TEST(SceneFiles, FieldsHoldColorsAndWholeNumbers) {
    Fields fields;
    fields["color"] = formatField(Vec4(3.0f, 0.5f, 0.125f, 0.25f));
    fields["count"] = formatField(4294967295u);
    Vec4 color(0.0f);
    u32 count = 0;
    ASSERT_TRUE(parseField(fields, "color", color).has_value());
    ASSERT_TRUE(parseField(fields, "count", count).has_value());
    EXPECT_EQ(color, Vec4(3.0f, 0.5f, 0.125f, 0.25f));
    EXPECT_EQ(count, 4294967295u);

    // Whole numbers are whole, unsigned and in range; a bad one leaves the
    // value as it was.
    for (const char* bad : {"-1", "+1", "2.5", "4294967296", "", "12 apples"}) {
        Fields broken{{"count", bad}};
        u32 value = 9;
        EXPECT_FALSE(parseField(broken, "count", value).has_value()) << bad;
        EXPECT_EQ(value, 9u) << bad;
    }
    Fields shortColor{{"color", "1 2 3"}};
    EXPECT_FALSE(parseField(shortColor, "color", color).has_value());
}

TEST(SceneFiles, LeaveTransientEntitiesAndTheirChildrenOut) {
    Scene level("level");
    ecs::World& world = level.world();
    const ecs::Entity floor = level.createEntity("Floor");
    const ecs::Entity player = level.createEntity("Player");
    const ecs::Entity gun = level.createEntity("Gun");
    const ecs::Entity lamp = level.createEntity("Lamp");
    world.emplace<Transient>(player);
    ASSERT_TRUE(setParent(world, gun, player).has_value());
    ASSERT_TRUE(setParent(world, lamp, floor).has_value());

    const ComponentSerializers serializers = ComponentSerializers::withSceneComponents();
    const std::string text = serializers.save(world);
    EXPECT_NE(text.find("Floor"), std::string::npos);
    EXPECT_NE(text.find("Lamp"), std::string::npos);
    EXPECT_EQ(text.find("Player"), std::string::npos);
    EXPECT_EQ(text.find("Gun"), std::string::npos);

    Scene loaded("loaded");
    ASSERT_TRUE(serializers.load(loaded.world(), text).has_value());
    EXPECT_EQ(loaded.world().entityCount(), 2u);
    const ecs::Entity loadedLamp = loaded.find("Lamp");
    ASSERT_TRUE(loaded.world().alive(loadedLamp));
    EXPECT_EQ(loaded.world().get<Parent>(loadedLamp).entity, loaded.find("Floor"));
}

TEST(SceneFiles, RejectBadDataWithoutLeavingHalfAScene) {
    const ComponentSerializers serializers = ComponentSerializers::withSceneComponents();
    ecs::World world;

    const auto badNumber = serializers.load(world, "[entity.0]\nTransform.position = 1 two 3\n");
    ASSERT_FALSE(badNumber.has_value());
    EXPECT_NE(badNumber.error().message.find("position"), std::string::npos);
    EXPECT_EQ(world.entityCount(), 0u);

    EXPECT_FALSE(serializers.load(world, "[entity.0]\nParent.entity = 7\n").has_value());
    EXPECT_FALSE(serializers.load(world, "[stuff]\nkey = 1\n").has_value());
    EXPECT_EQ(world.entityCount(), 0u);

    // Unknown components are skipped, not fatal.
    const auto unknown = serializers.load(world, "[entity.0]\nName.value = a\nWeapon.ammo = 3\n");
    ASSERT_TRUE(unknown.has_value());
    EXPECT_EQ(world.get<Name>(unknown->front()).value, "a");
}

TEST(SceneFiles, EntityIdsAreNamesNotSizes) {
    // Hand-edited or hostile files may number entities however they like:
    // memory must follow the entity count, not the largest id (these once
    // asked for gigabytes, or overflowed).
    const ComponentSerializers serializers = ComponentSerializers::withSceneComponents();
    ecs::World world;
    const auto loaded = serializers.load(world,
                                         "[entity.4000000000]\nName.value = far\nParent.entity = 18446744073709551615\n"
                                         "[entity.18446744073709551615]\nName.value = farther\n"
                                         "[entity.7]\nName.value = near\nParent.entity = 4000000000\n");
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(loaded->size(), 3u);  // in id order
    const ecs::Entity near = loaded->at(0);
    const ecs::Entity far = loaded->at(1);
    const ecs::Entity farther = loaded->at(2);
    EXPECT_EQ(world.get<Name>(near).value, "near");
    EXPECT_EQ(world.get<Parent>(near).entity, far);
    EXPECT_EQ(world.get<Parent>(far).entity, farther);
    // References to ids the file does not define are still errors.
    EXPECT_FALSE(serializers.load(world, "[entity.3000000000]\nParent.entity = 3000000001\n").has_value());
}

TEST(SceneFiles, KeepModelNodeIndices) {
    const ComponentSerializers serializers = ComponentSerializers::withSceneComponents();
    Scene source("source");
    source.world().emplace<ModelNode>(source.createEntity("hook"), 4u);
    Scene loaded("loaded");
    const auto entities = serializers.load(loaded.world(), serializers.save(source.world()));
    ASSERT_TRUE(entities.has_value()) << entities.error().message;
    EXPECT_EQ(loaded.world().get<ModelNode>(entities->front()).index, 4u);
    EXPECT_FALSE(serializers.load(loaded.world(), "[entity.0]\nModelNode.index = -2\n").has_value());
    EXPECT_FALSE(serializers.load(loaded.world(), "[entity.0]\nModelNode.index = 1.5\n").has_value());
}

TEST(SceneFiles, ApplicationsRegisterTheirComponents) {
    struct Health {
        f32 points = 100.0f;
        bool invulnerable = false;
    };
    ComponentSerializers serializers = ComponentSerializers::withSceneComponents();
    serializers.add<Health>(
        "Health",
        [](const Health& health, Fields& out, const EntityRefs&) {
            out["points"] = formatField(health.points);
            out["invulnerable"] = formatField(health.invulnerable);
        },
        [](Health& health, const Fields& in, const EntityRefs&) -> Result<> {
            if (auto parsed = parseField(in, "points", health.points); !parsed) return parsed;
            return parseField(in, "invulnerable", health.invulnerable);
        });

    ecs::World world;
    world.emplace<Health>(world.create(), Health{42.5f, true});
    ecs::World copy;
    const auto loaded = serializers.load(copy, serializers.save(world));
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_FLOAT_EQ(copy.get<Health>(loaded->front()).points, 42.5f);
    EXPECT_TRUE(copy.get<Health>(loaded->front()).invulnerable);
}

TEST(SceneFiles, KeepCamerasAndLights) {
    scene::ComponentSerializers serializers = ComponentSerializers::withSceneComponents();
    render::addComponentSerializers(serializers);

    Scene source("source");
    const ecs::Entity camera = source.createEntity("Camera");
    source.world().emplace<render::Camera>(camera, render::Camera{0.9f, 0.5f, 250.0f, false});
    const ecs::Entity sun = source.createEntity("Sun");
    source.world().emplace<render::DirectionalLight>(sun).castShadows = false;
    const ecs::Entity lamp = source.createEntity("Lamp");
    source.world().emplace<render::PointLight>(lamp, render::PointLight{Vec3(1.0f, 0.5f, 0.0f), 3.0f, 12.0f});

    Scene target("target");
    ASSERT_TRUE(serializers.load(target.world(), serializers.save(source.world())).has_value());
    const render::Camera& lens = target.world().get<render::Camera>(target.find("Camera"));
    EXPECT_FLOAT_EQ(lens.verticalFov, 0.9f);
    EXPECT_FLOAT_EQ(lens.farPlane, 250.0f);
    EXPECT_FALSE(lens.primary);
    EXPECT_FALSE(target.world().get<render::DirectionalLight>(target.find("Sun")).castShadows);
    const render::PointLight& light = target.world().get<render::PointLight>(target.find("Lamp"));
    EXPECT_EQ(light.color, Vec3(1.0f, 0.5f, 0.0f));
    EXPECT_FLOAT_EQ(light.range, 12.0f);

    const ecs::Entity torch = source.createEntity("Torch");
    render::SpotLight spot;
    spot.innerAngle = 0.25f;
    spot.outerAngle = 0.4f;
    source.world().emplace<render::SpotLight>(torch, spot);
    Scene again("again");
    ASSERT_TRUE(serializers.load(again.world(), serializers.save(source.world())).has_value());
    EXPECT_FLOAT_EQ(again.world().get<render::SpotLight>(again.find("Torch")).outerAngle, 0.4f);
}

TEST(Shadows, CascadesCoverTheirSlices) {
    const std::vector<f32> splits = render::cascadeSplits(0.1f, 100.0f, 4);
    ASSERT_EQ(splits.size(), 4u);
    EXPECT_FLOAT_EQ(splits.back(), 100.0f);
    for (usize i = 1; i < splits.size(); ++i) {
        EXPECT_GT(splits[i], splits[i - 1]);
        // Each slice is longer than the one before: near cascades are sharper.
        EXPECT_GT(splits[i] - splits[i - 1], i > 1 ? splits[i - 1] - splits[i - 2] : splits[0]);
    }

    Transform eye;
    eye.position = Vec3(4.0f, 3.0f, 2.0f);
    eye.rotation = glm::angleAxis(-0.6f, Vec3(0.0f, 1.0f, 0.0f));
    const render::CameraData camera = render::makeCameraData(eye.localMatrix(), render::Camera{}, 16.0f / 9.0f);
    const Vec3 sun = glm::normalize(Vec3(0.3f, -1.0f, 0.2f));
    const Mat4 inverseViewProjection = glm::inverse(camera.viewProjection);
    f32 previousSize = 0.0f;
    for (usize cascade = 0; cascade < splits.size(); ++cascade) {
        const f32 nearDistance = cascade == 0 ? 0.0f : splits[cascade - 1];
        const render::CameraData light =
            render::fitDirectionalShadow(camera, sun, nearDistance, splits[cascade], 1024);
        // Larger slices get larger (coarser) volumes.
        const f32 size = 1.0f / light.projection[0][0];
        EXPECT_GT(size, previousSize);
        previousSize = size;
        // Points of the slice project inside the map.
        for (const f32 x : {-1.0f, 1.0f}) {
            for (const f32 y : {-1.0f, 1.0f}) {
                const Vec4 far = inverseViewProjection * Vec4(x, y, 1.0f, 1.0f);
                const Vec3 ray = glm::normalize(Vec3(far) / far.w - camera.position);
                const f32 alongAxis = glm::dot(ray, -glm::normalize(Vec3(glm::inverse(camera.view)[2])));
                for (const f32 depth : {std::max(nearDistance, 0.2f), splits[cascade]}) {
                    const Vec4 clip = light.viewProjection * Vec4(camera.position + ray * (depth / alongAxis), 1.0f);
                    EXPECT_LE(std::abs(clip.x / clip.w), 1.0f);
                    EXPECT_LE(std::abs(clip.y / clip.w), 1.0f);
                    EXPECT_GE(clip.z / clip.w, 0.0f);
                    EXPECT_LE(clip.z / clip.w, 1.0f);
                }
            }
        }
    }
}

TEST(Hierarchy, ParallelUpdateMatchesTheSerialOne) {
    Scene scene("forest");
    ecs::World& world = scene.world();
    u32 state = 7;
    const auto next = [&] { return state = state * 1664525u + 1013904223u; };
    std::vector<ecs::Entity> entities;
    for (int i = 0; i < 5000; ++i) {
        const ecs::Entity entity = i % 50 == 0 ? world.create() : scene.createEntity("node");
        if (i % 50 == 0) {
            world.emplace<Transform>(entity);  // pivot without a WorldTransform
        }
        Transform& transform = world.get<Transform>(entity);
        transform.position = Vec3(static_cast<f32>(next() % 100), static_cast<f32>(next() % 7), 0.5f);
        transform.rotation = glm::angleAxis(static_cast<f32>(next() % 628) / 100.0f, Vec3(0.0f, 1.0f, 0.0f));
        // About half are roots; the others hang under an earlier entity.
        if (i > 0 && next() % 2 == 0) {
            world.emplace<Parent>(entity, entities[next() % entities.size()]);
        }
        entities.push_back(entity);
    }
    world.emplace<Parent>(entities[1], entities[2]);  // and a cycle
    world.emplace<Parent>(entities[2], entities[1]);
    world.destroy(entities[3]);                      // and broken links

    updateWorldTransforms(world);
    std::vector<Mat4> serial;
    world.view<const WorldTransform>().each([&](const WorldTransform& t) { serial.push_back(t.matrix); });

    world.view<WorldTransform>().each([](WorldTransform& t) { t.matrix = Mat4(0.0f); });
    JobSystem jobs(4);
    updateWorldTransforms(world, jobs);
    std::vector<Mat4> parallel;
    world.view<const WorldTransform>().each([&](const WorldTransform& t) { parallel.push_back(t.matrix); });
    EXPECT_EQ(parallel, serial);
}

TEST(Hierarchy, ReparentingRejectsCyclesAndCanKeepTheWorldPose) {
    Scene scene("test");
    ecs::World& world = scene.world();
    const ecs::Entity root = scene.createEntity("root");
    const ecs::Entity arm = scene.createEntity("arm");
    const ecs::Entity hand = scene.createEntity("hand");
    ASSERT_TRUE(setParent(world, arm, root).has_value());
    ASSERT_TRUE(setParent(world, hand, arm).has_value());
    EXPECT_EQ(children(world, root), (std::vector<ecs::Entity>{arm}));
    EXPECT_EQ(children(world, arm), (std::vector<ecs::Entity>{hand}));

    // Cycles and dead parents are refused, leaving links as they were.
    EXPECT_FALSE(setParent(world, root, hand).has_value());
    EXPECT_FALSE(setParent(world, arm, arm).has_value());
    const ecs::Entity gone = scene.createEntity("gone");
    world.destroy(gone);
    EXPECT_FALSE(setParent(world, hand, gone).has_value());
    EXPECT_FALSE(world.has<Parent>(root));
    EXPECT_EQ(world.get<Parent>(hand).entity, arm);

    // Moving the hand to another, rotated and scaled parent without a jump.
    world.get<Transform>(root).position = Vec3(1.0f, 2.0f, 3.0f);
    world.get<Transform>(arm).position = Vec3(0.0f, 1.0f, 0.0f);
    world.get<Transform>(hand).position = Vec3(0.5f, 0.0f, 0.0f);
    const ecs::Entity holder = scene.createEntity("holder");
    world.get<Transform>(holder).position = Vec3(-4.0f, 0.0f, 1.0f);
    world.get<Transform>(holder).rotation = glm::angleAxis(0.8f, glm::normalize(Vec3(0.0f, 1.0f, 1.0f)));
    world.get<Transform>(holder).scale = Vec3(2.0f);
    updateWorldTransforms(world);
    const Vec3 before = worldPosition(world, hand);
    ASSERT_TRUE(setParent(world, hand, holder, true).has_value());
    updateWorldTransforms(world);
    expectNear(worldPosition(world, hand), before);
    EXPECT_TRUE(children(world, arm).empty());

    // Detaching keeps the pose too.
    ASSERT_TRUE(setParent(world, hand, {}, true).has_value());
    updateWorldTransforms(world);
    EXPECT_FALSE(world.has<Parent>(hand));
    expectNear(worldPosition(world, hand), before);
}

TEST(Hierarchy, BranchesListSiblingsInIndexOrder) {
    ecs::World world;
    const ecs::Entity root = world.create();
    const ecs::Entity a = world.create();
    const ecs::Entity b = world.create();
    const ecs::Entity c = world.create();
    const ecs::Entity grandchild = world.create();
    // Linked out of order, then one link removed and added back, which
    // reorders the Parent pool.
    world.emplace<scene::Parent>(c, root);
    world.emplace<scene::Parent>(grandchild, a);
    world.emplace<scene::Parent>(a, root);
    world.emplace<scene::Parent>(b, root);
    world.remove<scene::Parent>(c);
    world.emplace<scene::Parent>(c, root);
    EXPECT_EQ(branch(world, root), (std::vector<ecs::Entity>{root, a, b, c, grandchild}));
    EXPECT_EQ(children(world, root), (std::vector<ecs::Entity>{a, b, c}));
}

TEST(Hierarchy, CloningCopiesAWholeBranch) {
    Scene scene("test");
    ecs::World& world = scene.world();
    const ecs::Entity room = scene.createEntity("room");
    const ecs::Entity table = scene.createEntity("table");
    const ecs::Entity leg = scene.createEntity("leg");
    const ecs::Entity lamp = scene.createEntity("lamp");
    ASSERT_TRUE(setParent(world, table, room).has_value());
    ASSERT_TRUE(setParent(world, leg, table).has_value());
    ASSERT_TRUE(setParent(world, lamp, table).has_value());
    world.get<Transform>(table).position = Vec3(1.0f, 0.0f, 0.0f);
    world.get<Transform>(leg).position = Vec3(0.0f, -0.5f, 0.0f);

    const ecs::Entity copy = cloneHierarchy(world, table);
    ASSERT_TRUE(world.alive(copy));
    EXPECT_EQ(world.entityCount(), 7u);
    // The copy sits beside the original, its children under the copy.
    EXPECT_EQ(world.get<Parent>(copy).entity, room);
    const std::vector<ecs::Entity> copiedChildren = children(world, copy);
    ASSERT_EQ(copiedChildren.size(), 2u);
    for (const ecs::Entity child : copiedChildren) {
        EXPECT_NE(child, leg);
        EXPECT_NE(child, lamp);
    }
    EXPECT_EQ(children(world, table), (std::vector<ecs::Entity>{leg, lamp}));
    EXPECT_EQ(children(world, room).size(), 2u);

    // Moving the copy moves its own children only.
    world.get<Transform>(copy).position = Vec3(5.0f, 0.0f, 0.0f);
    updateWorldTransforms(world);
    expectNear(worldPosition(world, leg), Vec3(1.0f, -0.5f, 0.0f));
    bool movedLeg = false;
    for (const ecs::Entity child : copiedChildren) {
        movedLeg |= glm::all(glm::epsilonEqual(worldPosition(world, child), Vec3(5.0f, -0.5f, 0.0f), 1e-5f));
    }
    EXPECT_TRUE(movedLeg);

    // Destroying the copy leaves the original alone.
    EXPECT_EQ(destroyHierarchy(world, copy), 3u);
    EXPECT_EQ(children(world, table).size(), 2u);
    EXPECT_FALSE(world.alive(cloneHierarchy(world, copy)));
}

TEST(DebugDraw, WritesTextWithLines) {
    render::DebugDraw debug;
    const auto bounds = [&]() {
        Vec3 low(1e9f);
        Vec3 high(-1e9f);
        for (const render::DebugDraw::Vertex& vertex : debug.vertices()) {
            low = glm::min(low, vertex.position);
            high = glm::max(high, vertex.position);
        }
        return std::pair{low, high};
    };

    // An H, 3 high: three strokes in a 2 x 3 box (grid units of 0.5).
    debug.text(Vec3(0.0f), "H", 3.0f);
    EXPECT_EQ(debug.lineCount(), 3u);
    auto [low, high] = bounds();
    expectNear(low, Vec3(0.0f));
    expectNear(high, Vec3(2.0f, 3.0f, 0.0f));

    // Lowercase is drawn as capitals; spaces advance without drawing.
    std::vector<render::DebugDraw::Vertex> capital(debug.vertices().begin(), debug.vertices().end());
    debug.clear();
    debug.text(Vec3(0.0f), "h", 3.0f);
    ASSERT_EQ(debug.lineCount(), capital.size() / 2);
    for (usize i = 0; i < capital.size(); ++i) {
        expectNear(debug.vertices()[i].position, capital[i].position);
    }
    debug.clear();
    debug.text(Vec3(0.0f), " H", 3.0f);
    std::tie(low, high) = bounds();
    EXPECT_FLOAT_EQ(low.x, 3.0f);  // one advance: 6 grid units

    // New lines go down; unknown characters show as '?'.
    debug.clear();
    debug.text(Vec3(0.0f), "I\nI", 6.0f);
    std::tie(low, high) = bounds();
    EXPECT_FLOAT_EQ(low.y, -9.0f);
    EXPECT_FLOAT_EQ(high.y, 6.0f);
    debug.clear();
    debug.text(Vec3(0.0f), "~", 6.0f);
    const usize unknown = debug.lineCount();
    debug.clear();
    debug.text(Vec3(0.0f), "?", 6.0f);
    EXPECT_EQ(debug.lineCount(), unknown);

    // Facing another way: along -Z, standing along +X.
    debug.clear();
    debug.text(Vec3(1.0f, 2.0f, 3.0f), "L", 6.0f, Vec4(1.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(1.0f, 0.0f, 0.0f));
    std::tie(low, high) = bounds();
    expectNear(low, Vec3(1.0f, 2.0f, -1.0f));
    expectNear(high, Vec3(7.0f, 2.0f, 3.0f));

    // Overlay text is in pixels: top-left corner given, rows growing down.
    debug.clear();
    debug.screenText(Vec2(10.0f, 20.0f), "H", 12.0f);
    debug.screenLine(Vec2(0.0f), Vec2(5.0f, 0.0f));
    EXPECT_EQ(debug.lineCount(), 0u);  // nothing in the world
    ASSERT_EQ(debug.overlayVertices().size(), 8u);
    Vec3 textLow(1e9f);
    Vec3 textHigh(-1e9f);
    for (usize i = 0; i < 6; ++i) {
        textLow = glm::min(textLow, debug.overlayVertices()[i].position);
        textHigh = glm::max(textHigh, debug.overlayVertices()[i].position);
    }
    expectNear(textLow, Vec3(10.0f, 20.0f, 0.0f));
    expectNear(textHigh, Vec3(18.0f, 32.0f, 0.0f));
    debug.screenRect(Vec2(0.0f), Vec2(10.0f, 5.0f), Vec4(1.0f));
    EXPECT_EQ(debug.overlayTriangles().size(), 6u);  // two triangles
    debug.clear();
    EXPECT_TRUE(debug.overlayVertices().empty());
    EXPECT_TRUE(debug.overlayTriangles().empty());

    EXPECT_FLOAT_EQ(render::DebugDraw::textWidth("AB", 6.0f), 10.0f);
    EXPECT_FLOAT_EQ(render::DebugDraw::textWidth("ABC\nA", 3.0f), 8.0f);
    EXPECT_FLOAT_EQ(render::DebugDraw::textWidth("", 3.0f), 0.0f);
}

TEST(SceneFiles, MeshRenderersAreSavedByName) {
    render::ResourceNames names;
    const render::MeshHandle cube{3, 1};
    const render::MeshHandle sphere{4, 1};
    const render::MaterialHandle red{7, 2};
    names.name(cube, "cube");
    names.name(sphere, "sphere");
    names.name(red, "red");
    EXPECT_EQ(names.mesh("cube"), cube);
    EXPECT_EQ(names.nameOf(red), "red");
    EXPECT_FALSE(names.mesh("missing").valid());
    EXPECT_TRUE(names.nameOf(render::MeshHandle{9, 9}).empty());

    // One name per handle, one handle per name.
    names.name(cube, "box");
    EXPECT_FALSE(names.mesh("cube").valid());
    EXPECT_EQ(names.nameOf(cube), "box");
    names.name(sphere, "box");
    EXPECT_EQ(names.mesh("box"), sphere);
    EXPECT_TRUE(names.nameOf(cube).empty());
    names.name(cube, "cube");

    ComponentSerializers serializers = ComponentSerializers::withSceneComponents();
    render::addMeshRendererSerializer(serializers, names);
    Scene source("source");
    const ecs::Entity drawn = source.createEntity("drawn");
    source.world().emplace<render::MeshRenderer>(drawn, cube, red, false);
    const std::string text = serializers.save(source.world());
    EXPECT_NE(text.find("MeshRenderer.mesh = cube"), std::string::npos) << text;

    Scene loaded("loaded");
    const auto entities = serializers.load(loaded.world(), text);
    ASSERT_TRUE(entities.has_value()) << entities.error().message;
    const auto& renderer = loaded.world().get<render::MeshRenderer>(entities->front());
    EXPECT_EQ(renderer.mesh, cube);
    EXPECT_EQ(renderer.material, red);
    EXPECT_FALSE(renderer.visible);

    // Names the registry does not know are errors that say which.
    const auto unknown = serializers.load(loaded.world(), "[entity.0]\nMeshRenderer.mesh = teapot\nMeshRenderer.material = red\n");
    ASSERT_FALSE(unknown.has_value());
    EXPECT_NE(unknown.error().message.find("teapot"), std::string::npos);
}
