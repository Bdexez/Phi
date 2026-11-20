#include <bdex/ecs/World.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using namespace bdex;
using namespace bdex::ecs;

namespace {
struct Position {
    float x = 0, y = 0;
};
struct Velocity {
    float x = 0, y = 0;
};
struct Tag {};
struct Label {
    std::string text;
};
}  // namespace

TEST(World, CreateDestroyAndGenerations) {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    EXPECT_NE(a, b);
    EXPECT_TRUE(world.alive(a));
    EXPECT_EQ(world.entityCount(), 2u);

    world.destroy(a);
    EXPECT_FALSE(world.alive(a));
    EXPECT_EQ(world.entityCount(), 1u);

    const Entity c = world.create();  // reuses a's slot
    EXPECT_EQ(c.index, a.index);
    EXPECT_NE(c.generation, a.generation);
    EXPECT_FALSE(world.alive(a));
    EXPECT_TRUE(world.alive(c));

    world.destroy(a);  // stale handle: no effect
    EXPECT_TRUE(world.alive(c));
    EXPECT_FALSE(world.alive(Entity{}));
}

TEST(World, ComponentsLifecycle) {
    World world;
    const Entity e = world.create();

    world.emplace<Position>(e, 1.0f, 2.0f);
    EXPECT_TRUE(world.has<Position>(e));
    EXPECT_FALSE((world.has<Position, Velocity>(e)));
    EXPECT_FLOAT_EQ(world.get<Position>(e).y, 2.0f);

    world.emplace<Position>(e, 5.0f, 6.0f);  // replace
    EXPECT_FLOAT_EQ(world.get<Position>(e).x, 5.0f);

    EXPECT_EQ(world.tryGet<Velocity>(e), nullptr);
    world.remove<Position>(e);
    EXPECT_FALSE(world.has<Position>(e));
}

TEST(World, DestroyRemovesComponentsAndKeepsOthersIntact) {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();
    world.emplace<Label>(a, "a");
    world.emplace<Label>(b, "b");
    world.emplace<Label>(c, "c");

    world.destroy(a);  // swap-and-pop moves c's component into a's slot

    EXPECT_EQ(world.get<Label>(b).text, "b");
    EXPECT_EQ(world.get<Label>(c).text, "c");
    EXPECT_EQ(world.view<Label>().count(), 2u);

    const Entity reused = world.create();
    EXPECT_FALSE(world.has<Label>(reused));  // no component leaks to the new entity
}

TEST(World, ViewIteratesEntitiesWithAllComponents) {
    World world;
    const Entity moving = world.create();
    world.emplace<Position>(moving, 0.0f, 0.0f);
    world.emplace<Velocity>(moving, 1.0f, 2.0f);

    const Entity still = world.create();
    world.emplace<Position>(still, 10.0f, 10.0f);

    int visited = 0;
    world.view<Position, Velocity>().each([&](Entity entity, Position& p, Velocity& v) {
        EXPECT_EQ(entity, moving);
        p.x += v.x;
        p.y += v.y;
        ++visited;
    });

    EXPECT_EQ(visited, 1);
    EXPECT_FLOAT_EQ(world.get<Position>(moving).y, 2.0f);
    EXPECT_FLOAT_EQ(world.get<Position>(still).x, 10.0f);

    // The callback may omit the entity parameter.
    int count = 0;
    world.view<Position>().each([&](Position&) { ++count; });
    EXPECT_EQ(count, 2);
}

TEST(World, EntitiesCanBeDestroyedWhileIterating) {
    World world;
    std::set<u32> expected;
    for (int i = 0; i < 100; ++i) {
        const Entity e = world.create();
        world.emplace<Tag>(e);
        world.emplace<Position>(e, static_cast<float>(i), 0.0f);
        if (i % 2 == 1) {
            expected.insert(e.index);
        }
    }

    int visited = 0;
    world.view<Tag, Position>().each([&](Entity entity, Tag&, Position& p) {
        ++visited;
        if (static_cast<int>(p.x) % 2 == 0) {
            world.destroy(entity);
        }
    });
    EXPECT_EQ(visited, 100);  // each entity seen exactly once

    std::set<u32> remaining;
    world.view<Tag>().each([&](Entity entity, Tag&) { remaining.insert(entity.index); });
    EXPECT_EQ(remaining, expected);
}

TEST(World, ClearInvalidatesEverything) {
    World world;
    const Entity e = world.create();
    world.emplace<Position>(e);
    world.clear();

    EXPECT_FALSE(world.alive(e));
    EXPECT_EQ(world.entityCount(), 0u);
    EXPECT_EQ(world.view<Position>().count(), 0u);

    const Entity fresh = world.create();
    EXPECT_TRUE(world.alive(fresh));
    EXPECT_FALSE(world.has<Position>(fresh));
}

TEST(World, ConstViewsAndQueries) {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    world.emplace<Position>(a, 1.0f, 2.0f);
    world.emplace<Velocity>(a, 3.0f, 4.0f);
    world.emplace<Position>(b, 5.0f, 6.0f);

    // Read-only components arrive as const references.
    float sum = 0.0f;
    world.view<const Position, Velocity>().each([&](const Position& p, Velocity& v) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(p)>>);
        v.x += p.x;
        sum += p.y;
    });
    EXPECT_FLOAT_EQ(sum, 2.0f);
    EXPECT_FLOAT_EQ(world.get<Velocity>(a).x, 4.0f);
    EXPECT_EQ(world.view<const Position>().count(), 2u);

    // Queries work through a const world.
    const World& readOnly = world;
    EXPECT_TRUE((readOnly.has<Position, Velocity>(a)));
    EXPECT_FALSE(readOnly.has<Velocity>(b));
    EXPECT_FALSE(readOnly.has<Label>(a));  // never stored anywhere
    EXPECT_FLOAT_EQ(readOnly.get<Position>(b).x, 5.0f);
    ASSERT_NE(readOnly.tryGet<Position>(a), nullptr);
    EXPECT_EQ(readOnly.tryGet<Label>(a), nullptr);
    EXPECT_EQ(readOnly.tryGet<Position>(Entity{}), nullptr);
}

TEST(World, RemovingAnUnknownComponentIsANoOp) {
    World world;
    const Entity e = world.create();
    world.remove<Label>(e);
    EXPECT_FALSE(world.has<Label>(e));
    EXPECT_TRUE(world.alive(e));
}

TEST(World, ViewsCanExcludeComponents) {
    World world;
    const Entity moving = world.create();
    world.emplace<Position>(moving);
    world.emplace<Velocity>(moving);
    const Entity still = world.create();
    world.emplace<Position>(still);
    const Entity tagged = world.create();
    world.emplace<Position>(tagged);
    world.emplace<Tag>(tagged);

    std::set<u32> seen;
    world.view<Position>().exclude<Velocity, Tag>().each(
        [&](Entity entity, Position&) { seen.insert(entity.index); });
    EXPECT_EQ(seen, (std::set<u32>{still.index}));
    EXPECT_EQ(world.view<Position>().exclude<Velocity>().count(), 2u);

    // Excluding a type no entity ever had filters nothing.
    EXPECT_EQ(world.view<Position>().exclude<Label>().count(), 3u);
    // The original view is unaffected.
    const auto all = world.view<Position>();
    const auto filtered = all.exclude<Tag>();
    EXPECT_EQ(filtered.count(), 2u);
}

TEST(World, ParallelEachVisitsEveryMatchOnce) {
    World world;
    std::vector<Entity> movers;
    for (int i = 0; i < 10000; ++i) {
        const Entity entity = world.create();
        world.emplace<Position>(entity, static_cast<float>(i), 0.0f);
        if (i % 3 != 0) {
            world.emplace<Velocity>(entity, 1.0f, 2.0f);
            movers.push_back(entity);
        }
    }

    JobSystem jobs(4);
    world.view<Position, const Velocity>().parallelEach(
        jobs, [](Position& position, const Velocity& velocity) {
            position.x += velocity.x;
            position.y += velocity.y;
        },
        64);

    usize moved = 0;
    world.view<const Position>().each([&](Entity entity, const Position& position) {
        const bool mover = world.has<Velocity>(entity);
        moved += mover ? 1 : 0;
        EXPECT_FLOAT_EQ(position.y, mover ? 2.0f : 0.0f);  // exactly once
    });
    EXPECT_EQ(moved, movers.size());

    std::atomic<usize> visited{0};
    world.view<Position>().exclude<Velocity>().parallelEach(jobs, [&](Entity, Position&) { ++visited; });
    EXPECT_EQ(visited.load(), 10000u - movers.size());
}

TEST(World, HooksObserveComponentsComingAndGoing) {
    World world;
    std::vector<std::string> events;
    const auto added = world.onAdd<Position>([&](World& w, Entity e) {
        events.push_back("add " + std::to_string(static_cast<int>(w.get<Position>(e).x)));
    });
    world.onRemove<Position>([&](World& w, Entity e) {
        events.push_back("remove " + std::to_string(static_cast<int>(w.get<Position>(e).x)));  // still readable
    });

    const Entity a = world.create();
    const Entity b = world.create();
    world.emplace<Position>(a, 1.0f, 0.0f);
    world.emplace<Position>(a, 2.0f, 0.0f);  // replaced: not a new addition
    world.emplace<Position>(b, 3.0f, 0.0f);
    world.emplace<Velocity>(b);              // other types are not observed
    world.remove<Position>(a);
    world.remove<Position>(a);               // nothing left to remove
    world.destroy(b);
    EXPECT_EQ(events, (std::vector<std::string>{"add 1", "add 3", "remove 2", "remove 3"}));

    events.clear();
    world.removeHook(added);
    const Entity c = world.create();
    world.emplace<Position>(c, 4.0f, 0.0f);
    world.clear();
    EXPECT_EQ(events, (std::vector<std::string>{"remove 4"}));
}

TEST(World, HooksMayChangeTheWorld) {
    World world;
    // Every Position gets a Velocity; removing the Position takes it away.
    world.onAdd<Position>([](World& w, Entity e) { w.emplace<Velocity>(e, 5.0f, 0.0f); });
    world.onRemove<Position>([](World& w, Entity e) { w.remove<Velocity>(e); });

    const Entity e = world.create();
    Position& position = world.emplace<Position>(e, 1.0f, 2.0f);
    EXPECT_FLOAT_EQ(position.y, 2.0f);  // the returned reference survives the hook
    ASSERT_TRUE(world.has<Velocity>(e));
    EXPECT_FLOAT_EQ(world.get<Velocity>(e).x, 5.0f);

    world.remove<Position>(e);
    EXPECT_FALSE(world.has<Velocity>(e));

    // A hook destroying the entity during destroy() is fine.
    world.onRemove<Tag>([](World& w, Entity entity) { w.destroy(entity); });
    const Entity tagged = world.create();
    world.emplace<Tag>(tagged);
    world.emplace<Position>(tagged);
    world.destroy(tagged);
    EXPECT_FALSE(world.alive(tagged));
    EXPECT_EQ(world.entityCount(), 1u);
}
