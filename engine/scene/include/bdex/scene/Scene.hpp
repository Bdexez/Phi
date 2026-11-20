#pragma once

#include <bdex/core/JobSystem.hpp>
#include <bdex/ecs/World.hpp>
#include <bdex/scene/Components.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bdex::scene {

/// A named ECS world.
class Scene {
public:
    explicit Scene(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] ecs::World& world() { return world_; }

    /// Creates an entity with Name, Transform and WorldTransform components.
    ecs::Entity createEntity(std::string_view name = "Entity");

    /// The entity with this Name in the lowest slot (normally the first one
    /// created), or an invalid handle. A linear scan: look entities up once
    /// and keep the handle rather than every frame.
    [[nodiscard]] ecs::Entity find(std::string_view name);
    /// Every entity with this Name, by slot.
    [[nodiscard]] std::vector<ecs::Entity> findAll(std::string_view name);

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
    void setActive(Scene* scene) { active_ = scene; }

    [[nodiscard]] usize sceneCount() const { return scenes_.size(); }

private:
    std::vector<std::unique_ptr<Scene>> scenes_;
    Scene* active_ = nullptr;
};

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

}  // namespace bdex::scene
