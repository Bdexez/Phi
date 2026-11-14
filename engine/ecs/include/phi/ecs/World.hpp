#pragma once

#include <phi/core/Assert.hpp>
#include <phi/core/JobSystem.hpp>
#include <phi/core/TypeId.hpp>
#include <phi/ecs/ComponentPool.hpp>
#include <phi/ecs/Entity.hpp>

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace phi::ecs {

template <class... Components>
class View;

/// Container of entities and their components.
///
/// Component types need no registration: any movable type can be attached.
/// Pools are keyed by phi::typeId, so a component type defined in a plugin
/// and the same type used by the executable share one pool. Queries
/// (has, tryGet, remove) never create a pool; only emplace and view do.
/// A const World gives views of const components only, creating nothing:
/// functions that only read the world can take it as `const World&`.
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

    /// Creates an entity with a copy of each of `source`'s components, firing
    /// onAdd hooks as emplace would. Components that cannot be copied are
    /// left out; entity handles inside components are copied as they are.
    /// Returns an invalid handle if `source` is dead.
    [[nodiscard]] Entity clone(Entity source);

    /// Attaches (or replaces) a component and returns it.
    template <class T, class... Args>
    T& emplace(Entity entity, Args&&... args) {
        PHI_ASSERT(alive(entity), "emplace on a dead entity");
        auto& components = pool<T>();
        const bool added = !components.contains(entity.index);
        T& component = components.emplace(entity.index, std::forward<Args>(args)...);
        if (!added || hooks_.empty()) {
            return component;
        }
        notify(typeId<std::remove_cvref_t<T>>(), HookKind::Add, entity);
        // A hook may have added components, moving this one.
        return components.get(entity.index);
    }

    template <class T>
    void remove(Entity entity) {
        auto* components = findPool<T>();
        if (components == nullptr || !alive(entity) || !components->contains(entity.index)) {
            return;
        }
        notify(typeId<std::remove_cvref_t<T>>(), HookKind::Remove, entity);
        if (alive(entity)) {  // a hook may have destroyed it
            components->remove(entity.index);
        }
    }

    // --- Observers ---------------------------------------------------------------

    using ComponentHook = std::function<void(World&, Entity)>;
    using HookId = u64;

    /// Calls `hook` after a T is attached to an entity that had none (not
    /// when emplace replaces one).
    template <class T>
    HookId onAdd(ComponentHook hook) {
        return addHook(typeId<std::remove_cvref_t<T>>(), HookKind::Add, std::move(hook));
    }
    /// Calls `hook` before a T leaves an entity (remove<T>, destroy, clear),
    /// while it can still be read. Hooks may change the world; destroying
    /// the entity being destroyed is a no-op.
    template <class T>
    HookId onRemove(ComponentHook hook) {
        return addHook(typeId<std::remove_cvref_t<T>>(), HookKind::Remove, std::move(hook));
    }
    void removeHook(HookId id);

    template <class T>
    [[nodiscard]] T& get(Entity entity) {
        return const_cast<T&>(std::as_const(*this).get<T>(entity));
    }
    template <class T>
    [[nodiscard]] const T& get(Entity entity) const {
        PHI_ASSERT(alive(entity), "get on a dead entity");
        const auto* components = findPool<T>();
        PHI_ASSERT(components != nullptr, "entity does not have this component");
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
        return View<Components...>(*this, &pool<Components>()...);
    }
    /// Read-only views: every type must be const (`view<const Name>()`).
    /// A type no entity ever had makes the view empty.
    template <class... Components>
    [[nodiscard]] View<Components...> view() const {
        static_assert((std::is_const_v<Components> && ...),
                      "a const World only gives views of const components: view<const T>()");
        // The view hands out const references only, so dropping const from
        // the pools never lets anything be written.
        return View<Components...>(
            *this, const_cast<ComponentPool<std::remove_cvref_t<Components>>*>(findPool<Components>())...);
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
        PHI_ASSERT(index < generations_.size());
        return Entity{index, generations_[index]};
    }

private:
    enum class HookKind : u8 { Add, Remove };
    struct Hook {
        HookId id = 0;
        TypeId type = 0;
        HookKind kind = HookKind::Add;
        std::shared_ptr<const ComponentHook> function;
    };

    HookId addHook(TypeId type, HookKind kind, ComponentHook hook);
    void notify(TypeId type, HookKind kind, Entity entity);
    [[nodiscard]] bool hasHook(TypeId type, HookKind kind) const;

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
    std::vector<Hook> hooks_;
    HookId nextHookId_ = 1;
    std::vector<u32> dying_;  ///< Entities whose destroy() is running hooks.
};

/// Result of World::view<...>(). Cheap to create; holds direct references to
/// the pools so iteration performs no map lookup.
template <class... Components>
class View {
    template <class T>
    using PoolOf = ComponentPool<std::remove_const_t<T>>;

public:
    /// A null pool (a type no entity ever had) makes the view empty.
    View(const World& world, PoolOf<Components>*... pools) : world_(&world), pools_(pools...) {}

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
        if (driver == nullptr) {
            return;
        }
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

    /// Like each(), split across the job system's threads (the caller takes
    /// part) in chunks of `grain` entities. For data-parallel systems: the
    /// callback may read and write the components it is given, but must not
    /// create or destroy entities, add or remove components, or touch other
    /// entities' components. Visiting order is unspecified.
    template <class Fn>
    void parallelEach(JobSystem& jobs, Fn&& fn, usize grain = 256) {
        IComponentPool* driver = smallestPool();
        if (driver == nullptr) {
            return;
        }
        const std::span<const u32> entities = driver->entities();
        jobs.parallelFor(entities.size(), grain, [&](usize begin, usize end) {
            for (usize i = begin; i < end; ++i) {
                const u32 index = entities[i];
                if (!matches(index)) {
                    continue;
                }
                if constexpr (std::is_invocable_v<Fn, Entity, Components&...>) {
                    fn(world_->entityAt(index), std::get<PoolOf<Components>*>(pools_)->get(index)...);
                } else {
                    fn(std::get<PoolOf<Components>*>(pools_)->get(index)...);
                }
            }
        });
    }

    /// Number of matching entities (walks the smallest pool).
    [[nodiscard]] usize count() const {
        usize result = 0;
        IComponentPool* driver = smallestPool();
        if (driver == nullptr) {
            return 0;
        }
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

    /// Null when a pool is missing: then nothing matches.
    IComponentPool* smallestPool() const {
        IComponentPool* smallest = nullptr;
        bool missing = false;
        const auto consider = [&](IComponentPool* candidate) {
            if (candidate == nullptr) {
                missing = true;
            } else if (smallest == nullptr || candidate->size() < smallest->size()) {
                smallest = candidate;
            }
        };
        (consider(std::get<PoolOf<Components>*>(pools_)), ...);
        return missing ? nullptr : smallest;
    }

    const World* world_;
    std::tuple<PoolOf<Components>*...> pools_;
    std::vector<const IComponentPool*> excluded_;
};

}  // namespace phi::ecs
