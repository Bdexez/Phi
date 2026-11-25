#include <phi/scene/Scene.hpp>

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace phi::scene {

ecs::Entity Scene::createEntity(std::string_view name) {
    const ecs::Entity entity = world_.create();
    world_.emplace<Name>(entity, std::string(name));
    world_.emplace<Transform>(entity);
    world_.emplace<WorldTransform>(entity);
    return entity;
}

std::vector<ecs::Entity> Scene::findAll(std::string_view name) const {
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

ecs::Entity Scene::find(std::string_view name) const {
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
    if (world.alive(parent)) {
        world.view<const Parent>().each([&](ecs::Entity child, const Parent& link) {
            if (link.entity == parent) {
                found.push_back(child);
            }
        });
    }
    std::sort(found.begin(), found.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
    return found;
}

/// `root` and every entity below it through Parent links, breadth-first, so
/// parents come before their children.
std::vector<ecs::Entity> branch(const ecs::World& world, ecs::Entity root) {
    std::vector<ecs::Entity> found = {root};
    const auto* parents = world.storage<Parent>();
    if (parents == nullptr || parents->size() == 0) {
        return found;
    }
    // Children of each entity, gathered in one pass over the Parent links.
    // Siblings are sorted by slot index as each visited node is expanded.
    std::unordered_map<u32, std::vector<ecs::Entity>> children;
    const std::span<const u32> childSlots = parents->entities();
    const std::span<const Parent> links = parents->values();
    for (usize i = 0; i < childSlots.size(); ++i) {
        if (world.alive(links[i].entity)) {
            children[links[i].entity.index].push_back(world.entityAt(childSlots[i]));
        }
    }
    // The set guards against cycles.
    std::unordered_set<u32> seen = {root.index};
    for (usize i = 0; i < found.size(); ++i) {
        const auto it = children.find(found[i].index);
        if (it == children.end()) {
            continue;
        }
        auto& siblings = it->second;
        std::sort(siblings.begin(), siblings.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
        for (const ecs::Entity child : siblings) {
            if (seen.insert(child.index).second) {
                found.push_back(child);
            }
        }
    }
    return found;
}

ecs::Entity cloneHierarchy(ecs::World& world, ecs::Entity root) {
    if (!world.alive(root)) {
        return {};
    }
    const std::vector<ecs::Entity> originals = branch(world, root);
    std::unordered_map<u32, ecs::Entity> copies;  // original slot -> copy
    for (const ecs::Entity original : originals) {
        const ecs::Entity copy = world.clone(original);
        copies[original.index] = copy;
        // Below the root, attach to the copy of the parent; the root's copy
        // stays where the root is (a sibling under the same parent).
        if (original != root) {
            world.emplace<Parent>(copy, copies.at(world.get<Parent>(original).entity.index));
        }
    }
    return copies.at(root.index);
}

usize destroyHierarchy(ecs::World& world, ecs::Entity root) {
    if (!world.alive(root)) {
        return 0;
    }
    const std::vector<ecs::Entity> doomed = branch(world, root);

    for (const ecs::Entity entity : doomed) {
        world.destroy(entity);
    }
    return doomed.size();
}

namespace {

void updateWorldTransformsImpl(ecs::World& world, JobSystem* jobs) {
    auto* worldTransforms = world.storage<WorldTransform>();
    const auto* transforms = world.storage<Transform>();
    if (worldTransforms == nullptr || transforms == nullptr || worldTransforms->size() == 0) {
        return;
    }
    const auto* parents = world.storage<Parent>();

    // Fast path for flat scenes (no Parent links): write WorldTransform dense
    // storage directly without touching scratch buffers or a second pass.
    if (parents == nullptr || parents->size() == 0) {
        const std::span<const u32> entities = worldTransforms->entities();
        const std::span<WorldTransform> outputs = worldTransforms->values();
        const auto computeRange = [&](usize begin, usize end) {
            for (usize slot = begin; slot < end; ++slot) {
                if (const Transform* local = transforms->tryGet(entities[slot])) {
                    outputs[slot].matrix = local->localMatrix();
                }
            }
        };
        if (jobs != nullptr) {
            jobs->parallelFor(entities.size(), 1024, computeRange);
        } else {
            computeRange(0, entities.size());
        }
        return;
    }

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

    const usize slotCount = world.slotCount();
    scratch.matrices.resize(slotCount);
    scratch.states.assign(slotCount, State::Pending);

    const auto parentOf = [&](u32 index) -> std::optional<u32> {
        const Parent* parent = parents->tryGet(index);
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
        const std::span<const Transform> locals = transforms->values();
        jobs->parallelFor(owners.size(), 1024, [&](usize begin, usize end) {
            for (usize i = begin; i < end; ++i) {
                const u32 index = owners[i];
                if (parentOf(index).has_value()) {
                    continue;
                }
                scratch.matrices[index] = locals[i].localMatrix();
                WorldTransform* target = worldTransforms->tryGet(index);
                if (target != nullptr) {
                    target->matrix = scratch.matrices[index];
                }
                scratch.states[index] = target != nullptr ? State::Written : State::Done;
            }
        });
    }

    const std::span<const u32> entities = worldTransforms->entities();
    const std::span<WorldTransform> outputs = worldTransforms->values();
    for (usize slot = 0; slot < entities.size(); ++slot) {
        const u32 index = entities[slot];
        if (transforms->contains(index) && scratch.states[index] != State::Written) {
            outputs[slot].matrix = resolve(index);
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

}  // namespace phi::scene
