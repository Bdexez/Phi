#pragma once

#include <phi/core/JobSystem.hpp>
#include <phi/core/Result.hpp>
#include <phi/ecs/World.hpp>
#include <phi/scene/Components.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace phi::scene {

/// A named ECS world.
class Scene {
public:
    explicit Scene(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] ecs::World& world() { return world_; }
    [[nodiscard]] const ecs::World& world() const { return world_; }

    /// Creates an entity with Name, Transform and WorldTransform components.
    ecs::Entity createEntity(std::string_view name = "Entity");

    /// The entity with this Name in the lowest slot (normally the first one
    /// created), or an invalid handle. A linear scan: look entities up once
    /// and keep the handle rather than every frame.
    [[nodiscard]] ecs::Entity find(std::string_view name) const;
    /// Every entity with this Name, by slot.
    [[nodiscard]] std::vector<ecs::Entity> findAll(std::string_view name) const;

private:
    std::string name_;
    ecs::World world_;
};

/// Service owning the scenes and designating the active one. Systems (the
/// renderer, gameplay...) look the active scene up here instead of being
/// handed a world, which keeps them independent from whoever creates scenes.
class SceneManager {
public:
    Scene& create(std::string name);
    void destroy(Scene& scene);

    [[nodiscard]] Scene* active() { return active_; }
    [[nodiscard]] const Scene* active() const { return active_; }
    void setActive(Scene* scene) { active_ = scene; }

    [[nodiscard]] usize sceneCount() const { return scenes_.size(); }

private:
    std::vector<std::unique_ptr<Scene>> scenes_;
    Scene* active_ = nullptr;
};

/// Attaches `child` under `parent`, or detaches it when `parent` is an
/// invalid handle. With `keepWorldTransform`, the child's Transform is
/// recomputed so it stays where it is in the world (from WorldTransforms
/// as of the last update); otherwise its local Transform is kept. Fails,
/// changing nothing, if `parent` is dead or below `child` (a cycle).
Result<> setParent(ecs::World& world, ecs::Entity child, ecs::Entity parent, bool keepWorldTransform = false);

/// `root` and every entity below it through Parent links, breadth-first:
/// parents before their children, siblings in entity index order.
[[nodiscard]] std::vector<ecs::Entity> branch(const ecs::World& world, ecs::Entity root);

/// Entities whose Parent is `parent`, by slot.
[[nodiscard]] std::vector<ecs::Entity> children(const ecs::World& world, ecs::Entity parent);

/// Copies `root` and every entity below it (see ecs::World::clone), with
/// the copies' Parent links pointing at each other; the root's copy keeps
/// the root's own parent, like duplicating in an editor. Returns the copy
/// of `root`, or an invalid handle if it is dead.
[[nodiscard]] ecs::Entity cloneHierarchy(ecs::World& world, ecs::Entity root);

/// Destroys `root` and every entity attached below it through Parent links.
/// (World::destroy alone leaves children behind, as roots placed at their
/// local transform.) Returns the number of entities destroyed.
usize destroyHierarchy(ecs::World& world, ecs::Entity root);

/// Recomputes WorldTransform for every entity with a Transform, following
/// Parent links. Each matrix is computed once, in O(n) overall, and parents
/// may be stored in any order. A link to a dead entity or to one without a
/// Transform is treated as "no parent"; in a cycle, the link that closes it is
/// ignored.
void updateWorldTransforms(ecs::World& world);
/// The same, computing entities without a parent (usually most of them) on
/// the job system's threads; hierarchies are then resolved on the caller's.
void updateWorldTransforms(ecs::World& world, JobSystem& jobs);

}  // namespace phi::scene
