#pragma once

#include <bdex/core/Assert.hpp>
#include <bdex/core/TypeId.hpp>
#include <bdex/ecs/ComponentPool.hpp>
#include <bdex/ecs/Entity.hpp>

#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bdex::ecs {

template <class... Components>
class View;

/// Container of entities and their components.
///
/// Component types need no registration: any movable type can be attached.
/// Pools are keyed by bdex::typeId, so a component type defined in a plugin
/// and the same type used by the executable share one pool. Queries
/// (has, tryGet, remove) never create a pool; only emplace and view do.
///
/// Systems are plain functions over views, scheduled through the engine
/// Scheduler:
///
///   world.view<Transform, Velocity>().each([&](Transform& t, Velocity& v) {
///       t.position += v.value * dt;
///   });
class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    [[nodiscard]] Entity create();
    /// Destroys the entity and all its components. Stale handles are ignored.
    void destroy(Entity entity);
    [[nodiscard]] bool alive(Entity entity) const;
    [[nodiscard]] usize entityCount() const { return aliveCount_; }

    /// Destroys every entity and component.
    void clear();

    /// Attaches (or replaces) a component and returns it.
    template <class T, class... Args>
    T& emplace(Entity entity, Args&&... args) {
        BDEX_ASSERT(alive(entity), "emplace on a dead entity");
        return pool<T>().emplace(entity.index, std::forward<Args>(args)...);
    }

    template <class T>
    void remove(Entity entity) {
        if (auto* components = findPool<T>(); components != nullptr && alive(entity)) {
            components->remove(entity.index);
        }
    }

    template <class T>
    [[nodiscard]] T& get(Entity entity) {
        return const_cast<T&>(std::as_const(*this).get<T>(entity));
    }
    template <class T>
    [[nodiscard]] const T& get(Entity entity) const {
        BDEX_ASSERT(alive(entity), "get on a dead entity");
        const auto* components = findPool<T>();
        BDEX_ASSERT(components != nullptr, "entity does not have this component");
        return components->get(entity.index);
    }

    template <class T>
    [[nodiscard]] T* tryGet(Entity entity) {
        return const_cast<T*>(std::as_const(*this).tryGet<T>(entity));
    }
    template <class T>
    [[nodiscard]] const T* tryGet(Entity entity) const {
        const auto* components = findPool<T>();
        return components != nullptr && alive(entity) ? components->tryGet(entity.index)
                                                      : nullptr;
    }

    template <class... Ts>
    [[nodiscard]] bool has(Entity entity) const {
        return alive(entity) && (hasComponent<Ts>(entity.index) && ...);
    }

    /// Iterates entities owning *all* the listed components (types must be
    /// distinct). A const-qualified type (`view<const Transform>()`) is
    /// handed to the callback as a const reference.
    template <class... Components>
    [[nodiscard]] View<Components...> view() {
        return View<Components...>(*this, pool<Components>()...);
    }

    /// Direct access to the storage of a component type, or null if no entity
    /// ever had it. Meant for engine systems that need tight loops (indices
    /// are entity slots; check alive() before trusting an index from outside
    /// the pool).
    template <class T>
    [[nodiscard]] ComponentPool<std::remove_cvref_t<T>>* storage() {
        return findPool<T>();
    }
    template <class T>
    [[nodiscard]] const ComponentPool<std::remove_cvref_t<T>>* storage() const {
        return findPool<T>();
    }

    /// Number of entity slots ever used: valid indices for entityAt() are
    /// below it (check alive() on the result).
    [[nodiscard]] usize slotCount() const { return generations_.size(); }

    /// Rebuilds the full handle of a live entity from its index.
    [[nodiscard]] Entity entityAt(u32 index) const {
        BDEX_ASSERT(index < generations_.size());
        return Entity{index, generations_[index]};
    }

private:
    template <class T>
    [[nodiscard]] const ComponentPool<std::remove_cvref_t<T>>* findPool() const {
        using Stored = std::remove_cvref_t<T>;
        const auto it = pools_.find(typeId<Stored>());
        return it == pools_.end() ? nullptr
                                  : static_cast<const ComponentPool<Stored>*>(it->second.get());
    }
    template <class T>
    [[nodiscard]] ComponentPool<std::remove_cvref_t<T>>* findPool() {
        return const_cast<ComponentPool<std::remove_cvref_t<T>>*>(
            std::as_const(*this).findPool<T>());
    }

    template <class T>
    [[nodiscard]] bool hasComponent(u32 index) const {
        const auto* components = findPool<T>();
        return components != nullptr && components->contains(index);
    }

    template <class T>
    ComponentPool<std::remove_cvref_t<T>>& pool() {
        using Stored = std::remove_cvref_t<T>;
        const TypeId id = typeId<Stored>();
        auto it = pools_.find(id);
        if (it == pools_.end()) {
            it = pools_.emplace(id, std::make_unique<ComponentPool<Stored>>()).first;
        }
        return static_cast<ComponentPool<Stored>&>(*it->second);
    }

    std::vector<u32> generations_;  // per slot; odd/even is irrelevant, only equality matters
    std::vector<bool> aliveFlags_;
    std::vector<u32> freeSlots_;
    usize aliveCount_ = 0;
    std::unordered_map<TypeId, std::unique_ptr<IComponentPool>> pools_;
};

/// Result of World::view<...>(). Cheap to create; holds direct references to
/// the pools so iteration performs no map lookup.
template <class... Components>
class View {
    template <class T>
    using PoolOf = ComponentPool<std::remove_const_t<T>>;

public:
    View(World& world, PoolOf<Components>&... pools) : world_(&world), pools_(&pools...) {}

    /// The same view, skipping entities that own any of `Excluded`:
    ///
    ///   world.view<Transform>().exclude<Parent>().each(...);  // roots only
    template <class... Excluded>
    [[nodiscard]] View exclude() const {
        View filtered = *this;
        (filtered.addExcluded(world_->template storage<Excluded>()), ...);
        return filtered;
    }

    /// Calls `fn` for each matching entity, as either
    /// `fn(Entity, Components&...)` or `fn(Components&...)`.
    ///
    /// Iteration walks the smallest pool backwards, so the callback may
    /// destroy the current entity or remove its components. Adding entities
    /// or components during iteration is allowed; whether they are visited
    /// in the same pass is unspecified.
    template <class Fn>
    void each(Fn&& fn) {
        IComponentPool* driver = smallestPool();
        for (usize i = driver->size(); i-- > 0;) {
            // Re-read the span every iteration: the callback may have grown
            // or shrunk the pool.
            const std::span<const u32> entities = driver->entities();
            if (i >= entities.size()) {
                continue;
            }
            const u32 index = entities[i];
            if (!matches(index)) {
                continue;
            }
            if constexpr (std::is_invocable_v<Fn, Entity, Components&...>) {
                fn(world_->entityAt(index),
                   std::get<PoolOf<Components>*>(pools_)->get(index)...);
            } else {
                fn(std::get<PoolOf<Components>*>(pools_)->get(index)...);
            }
        }
    }

    /// Number of matching entities (walks the smallest pool).
    [[nodiscard]] usize count() const {
        usize result = 0;
        IComponentPool* driver = smallestPool();
        for (const u32 index : driver->entities()) {
            if (matches(index)) {
                ++result;
            }
        }
        return result;
    }

private:
    void addExcluded(const IComponentPool* pool) {
        if (pool != nullptr) {  // a type no entity ever had excludes nothing
            excluded_.push_back(pool);
        }
    }

    [[nodiscard]] bool matches(u32 index) const {
        if (!(std::get<PoolOf<Components>*>(pools_)->contains(index) && ...)) {
            return false;
        }
        for (const IComponentPool* pool : excluded_) {
            if (pool->contains(index)) {
                return false;
            }
        }
        return true;
    }

    IComponentPool* smallestPool() const {
        IComponentPool* smallest = nullptr;
        const auto consider = [&smallest](IComponentPool* candidate) {
            if (smallest == nullptr || candidate->size() < smallest->size()) {
                smallest = candidate;
            }
        };
        (consider(std::get<PoolOf<Components>*>(pools_)), ...);
        return smallest;
    }

    World* world_;
    std::tuple<PoolOf<Components>*...> pools_;
    std::vector<const IComponentPool*> excluded_;
};

}  // namespace bdex::ecs
