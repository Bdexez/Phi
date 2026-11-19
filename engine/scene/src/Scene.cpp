#include <bdex/scene/Scene.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

namespace bdex::scene {

ecs::Entity Scene::createEntity(std::string_view name) {
    const ecs::Entity entity = world_.create();
    world_.emplace<Name>(entity, std::string(name));
    world_.emplace<Transform>(entity);
    world_.emplace<WorldTransform>(entity);
    return entity;
}

Scene& SceneManager::create(std::string name) {
    scenes_.push_back(std::make_unique<Scene>(std::move(name)));
    if (active_ == nullptr) {
        active_ = scenes_.back().get();
    }
    return *scenes_.back();
}

void SceneManager::destroy(Scene& scene) {
    if (active_ == &scene) {
        active_ = nullptr;
    }
    std::erase_if(scenes_, [&scene](const auto& owned) { return owned.get() == &scene; });
}

void updateWorldTransforms(ecs::World& world) {
    auto* worldTransforms = world.storage<WorldTransform>();
    const auto* transforms = world.storage<Transform>();
    if (worldTransforms == nullptr || transforms == nullptr) {
        return;
    }
    const auto* parents = world.storage<Parent>();

    // Every world matrix is computed once per call, parents before children,
    // whatever order entities are stored in: O(n) in the number of transforms.
    enum class State : u8 { Pending, Visiting, Done };
    struct Scratch {
        std::vector<Mat4> matrices;
        std::vector<State> states;
        std::vector<u32> chain;
    };
    thread_local Scratch scratch;  // reused across frames: no per-frame allocation

    u32 slotCount = 0;
    for (const u32 index : transforms->entities()) {
        slotCount = std::max(slotCount, index + 1);
    }
    scratch.matrices.resize(slotCount);
    scratch.states.assign(slotCount, State::Pending);

    const auto parentOf = [&](u32 index) -> std::optional<u32> {
        const Parent* parent = parents != nullptr ? parents->tryGet(index) : nullptr;
        if (parent == nullptr || !world.alive(parent->entity) ||
            !transforms->contains(parent->entity.index)) {
            return std::nullopt;  // root, or a broken link
        }
        return parent->entity.index;
    };

    const auto resolve = [&](u32 index) -> const Mat4& {
        // Walk up until a root or an already resolved ancestor...
        scratch.chain.clear();
        Mat4 parentMatrix(1.0f);
        std::optional<u32> current = index;
        while (current.has_value()) {
            const u32 slot = *current;
            if (scratch.states[slot] == State::Done) {
                parentMatrix = scratch.matrices[slot];
                break;
            }
            if (scratch.states[slot] == State::Visiting) {
                // Cycle: the link that closes it is ignored, so the topmost
                // entity of the chain acts as a root.
                break;
            }
            scratch.states[slot] = State::Visiting;
            scratch.chain.push_back(slot);
            current = parentOf(slot);
        }
        // ...then resolve the chain top-down.
        for (auto it = scratch.chain.rbegin(); it != scratch.chain.rend(); ++it) {
            scratch.matrices[*it] = parentMatrix * transforms->get(*it).localMatrix();
            scratch.states[*it] = State::Done;
            parentMatrix = scratch.matrices[*it];
        }
        return scratch.matrices[index];
    };

    const std::span<const u32> entities = worldTransforms->entities();
    for (usize slot = 0; slot < entities.size(); ++slot) {
        const u32 index = entities[slot];
        if (transforms->contains(index)) {
            worldTransforms->get(index).matrix = resolve(index);
        }
    }
}

}  // namespace bdex::scene
