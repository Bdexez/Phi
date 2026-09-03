#include <bdex/scene/Scene.hpp>

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bdex::scene {

ecs::Entity Scene::createEntity(std::string_view name) {
    const ecs::Entity entity = world_.create();
    world_.emplace<Name>(entity, std::string(name));
    world_.emplace<Transform>(entity);
    world_.emplace<WorldTransform>(entity);
    return entity;
}

std::vector<ecs::Entity> Scene::findAll(std::string_view name) {
    std::vector<ecs::Entity> found;
    world_.view<const Name>().each([&](ecs::Entity entity, const Name& candidate) {
        if (candidate.value == name) {
            found.push_back(entity);
        }
    });
    // Views run backwards over storage: order by slot instead.
    std::sort(found.begin(), found.end(),
              [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
    return found;
}

ecs::Entity Scene::find(std::string_view name) {
    ecs::Entity first;
    world_.view<const Name>().each([&](ecs::Entity entity, const Name& candidate) {
        if (candidate.value == name && entity.index < first.index) {
            first = entity;
        }
    });
    return first;
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

Result<> setParent(ecs::World& world, ecs::Entity child, ecs::Entity parent, bool keepWorldTransform) {
    if (!world.alive(child)) {
        return fail("setParent: the child is not alive");
    }
    if (parent.valid() && !world.alive(parent)) {
        return fail("setParent: the parent is not alive");
    }
    // Walking up from the new parent must not reach the child.
    for (ecs::Entity ancestor = parent; ancestor.valid();) {
        if (ancestor == child) {
            return fail("setParent: the parent is the child or one of its descendants");
        }
        const Parent* link = world.tryGet<Parent>(ancestor);
        ancestor = link != nullptr && world.alive(link->entity) ? link->entity : ecs::Entity{};
    }

    if (keepWorldTransform && world.has<Transform>(child)) {
        const auto worldOf = [&](ecs::Entity entity) {
            const WorldTransform* cached = world.tryGet<WorldTransform>(entity);
            return cached != nullptr ? cached->matrix : world.get<Transform>(entity).localMatrix();
        };
        const Mat4 parentWorld = parent.valid() && world.has<Transform>(parent) ? worldOf(parent) : Mat4(1.0f);
        const Mat4 local = glm::inverse(parentWorld) * worldOf(child);
        Transform& transform = world.get<Transform>(child);
        Vec3 skew;
        Vec4 perspective;
        glm::decompose(local, transform.scale, transform.rotation, transform.position, skew, perspective);
    }
    if (parent.valid()) {
        world.emplace<Parent>(child, parent);
    } else {
        world.remove<Parent>(child);
    }
    return {};
}

std::vector<ecs::Entity> children(const ecs::World& world, ecs::Entity parent) {
    std::vector<ecs::Entity> found;
    if (const auto* parents = world.storage<Parent>(); parents != nullptr && world.alive(parent)) {
        for (const u32 index : parents->entities()) {
            if (parents->get(index).entity == parent) {
                found.push_back(world.entityAt(index));
            }
        }
    }
    std::sort(found.begin(), found.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
    return found;
}

usize destroyHierarchy(ecs::World& world, ecs::Entity root) {
    if (!world.alive(root)) {
        return 0;
    }

    // Children of each entity, gathered in one pass over the Parent links.
    std::vector<ecs::Entity> doomed = {root};
    if (const auto* parents = world.storage<Parent>(); parents != nullptr) {
        std::unordered_map<u32, std::vector<ecs::Entity>> children;
        for (const u32 index : parents->entities()) {
            const ecs::Entity parent = parents->get(index).entity;
            if (world.alive(parent)) {
                children[parent.index].push_back(world.entityAt(index));
            }
        }
        // Breadth-first; the set guards against cycles.
        std::unordered_set<u32> seen = {root.index};
        for (usize i = 0; i < doomed.size(); ++i) {
            const auto found = children.find(doomed[i].index);
            if (found == children.end()) {
                continue;
            }
            for (const ecs::Entity child : found->second) {
                if (seen.insert(child.index).second) {
                    doomed.push_back(child);
                }
            }
        }
    }

    for (const ecs::Entity entity : doomed) {
        world.destroy(entity);
    }
    return doomed.size();
}

namespace {

void updateWorldTransformsImpl(ecs::World& world, JobSystem* jobs) {
    auto* worldTransforms = world.storage<WorldTransform>();
    const auto* transforms = world.storage<Transform>();
    if (worldTransforms == nullptr || transforms == nullptr) {
        return;
    }
    const auto* parents = world.storage<Parent>();

    // Every world matrix is computed once per call, parents before children,
    // whatever order entities are stored in: O(n) in the number of transforms.
    // Written: a root whose WorldTransform the parallel pass already set.
    enum class State : u8 { Pending, Visiting, Done, Written };
    struct Scratch {
        std::vector<Mat4> matrices;
        std::vector<State> states;
        std::vector<u32> chain;
    };
    thread_local Scratch threadScratch;  // reused across frames: no per-frame allocation
    // Named explicitly: inside a job, `threadScratch` would be the worker's.
    Scratch& scratch = threadScratch;

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
            if (scratch.states[slot] == State::Done || scratch.states[slot] == State::Written) {
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

    // Entities without a parent only need their local matrix: in parallel,
    // each job writing its own entities' slots and components.
    if (jobs != nullptr) {
        const std::span<const u32> owners = transforms->entities();
        jobs->parallelFor(owners.size(), 1024, [&](usize begin, usize end) {
            for (usize i = begin; i < end; ++i) {
                const u32 index = owners[i];
                if (parentOf(index).has_value()) {
                    continue;
                }
                scratch.matrices[index] = transforms->get(index).localMatrix();
                WorldTransform* target = worldTransforms->tryGet(index);
                if (target != nullptr) {
                    target->matrix = scratch.matrices[index];
                }
                scratch.states[index] = target != nullptr ? State::Written : State::Done;
            }
        });
    }

    const std::span<const u32> entities = worldTransforms->entities();
    for (usize slot = 0; slot < entities.size(); ++slot) {
        const u32 index = entities[slot];
        if (transforms->contains(index) && scratch.states[index] != State::Written) {
            worldTransforms->get(index).matrix = resolve(index);
        }
    }
}

}  // namespace

void updateWorldTransforms(ecs::World& world) {
    updateWorldTransformsImpl(world, nullptr);
}

void updateWorldTransforms(ecs::World& world, JobSystem& jobs) {
    updateWorldTransformsImpl(world, &jobs);
}

}  // namespace bdex::scene
