#pragma once

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

/// Recomputes WorldTransform for every entity with a Transform, following
/// Parent links. Each matrix is computed once, in O(n) overall, and parents
/// may be stored in any order. A link to a dead entity or to one without a
/// Transform is treated as "no parent"; in a cycle, the link that closes it is
/// ignored.
void updateWorldTransforms(ecs::World& world);

}  // namespace bdex::scene
