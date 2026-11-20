#include <bdex/ecs/World.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace bdex::ecs {

Entity World::create() {
    u32 index;
    if (!freeSlots_.empty()) {
        index = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        index = static_cast<u32>(generations_.size());
        generations_.push_back(1);
        aliveFlags_.push_back(false);
    }
    aliveFlags_[index] = true;
    ++aliveCount_;
    return Entity{index, generations_[index]};
}

Entity World::clone(Entity source) {
    if (!alive(source)) {
        return {};
    }
    const Entity copy = create();
    // Types collected first: hooks may add pools while they run.
    std::vector<TypeId> copied;
    for (auto& [type, pool] : pools_) {
        if (pool->copy(source.index, copy.index)) {
            copied.push_back(type);
        }
    }
    for (const TypeId type : copied) {
        if (!alive(copy)) {
            break;  // a hook destroyed it
        }
        notify(type, HookKind::Add, copy);
    }
    return copy;
}

void World::destroy(Entity entity) {
    if (!alive(entity)) {
        return;
    }
    if (!hooks_.empty()) {
        // A hook destroying the entity it is told about would recurse
        // forever: the entity is already on its way out, so that is a no-op.
        if (std::find(dying_.begin(), dying_.end(), entity.index) != dying_.end()) {
            return;
        }
        // Hooks first, while the components are still there. Types are
        // collected up front: a hook may add pools.
        std::vector<TypeId> owned;
        for (const auto& [type, pool] : pools_) {
            if (pool->contains(entity.index) && hasHook(type, HookKind::Remove)) {
                owned.push_back(type);
            }
        }
        dying_.push_back(entity.index);
        for (const TypeId type : owned) {
            notify(type, HookKind::Remove, entity);
        }
        std::erase(dying_, entity.index);
    }
    for (auto& [type, pool] : pools_) {
        pool->remove(entity.index);
    }
    aliveFlags_[entity.index] = false;
    ++generations_[entity.index];  // invalidates outstanding handles
    freeSlots_.push_back(entity.index);
    --aliveCount_;
}

bool World::alive(Entity entity) const {
    return entity.index < generations_.size() && aliveFlags_[entity.index] &&
           generations_[entity.index] == entity.generation;
}

void World::clear() {
    if (!hooks_.empty()) {
        std::vector<std::pair<TypeId, std::vector<u32>>> owners;
        for (const auto& [type, pool] : pools_) {
            if (hasHook(type, HookKind::Remove)) {
                const std::span<const u32> entities = pool->entities();
                owners.emplace_back(type, std::vector<u32>(entities.begin(), entities.end()));
            }
        }
        for (const auto& [type, entities] : owners) {
            for (const u32 index : entities) {
                const Entity entity = entityAt(index);
                const auto pool = pools_.find(type);
                if (alive(entity) && pool->second->contains(index)) {
                    notify(type, HookKind::Remove, entity);
                }
            }
        }
    }
    for (auto& [type, pool] : pools_) {
        pool->clear();
    }
    freeSlots_.clear();
    for (u32 index = static_cast<u32>(generations_.size()); index-- > 0;) {
        if (aliveFlags_[index]) {
            aliveFlags_[index] = false;
            ++generations_[index];
        }
        freeSlots_.push_back(index);
    }
    aliveCount_ = 0;
}

World::HookId World::addHook(TypeId type, HookKind kind, ComponentHook hook) {
    const HookId id = nextHookId_++;
    hooks_.push_back({id, type, kind, std::make_shared<const ComponentHook>(std::move(hook))});
    return id;
}

void World::removeHook(HookId id) {
    std::erase_if(hooks_, [id](const Hook& hook) { return hook.id == id; });
}

bool World::hasHook(TypeId type, HookKind kind) const {
    return std::any_of(hooks_.begin(), hooks_.end(),
                       [&](const Hook& hook) { return hook.type == type && hook.kind == kind; });
}

void World::notify(TypeId type, HookKind kind, Entity entity) {
    // Copied first: a hook may add or remove hooks.
    std::vector<std::shared_ptr<const ComponentHook>> matching;
    for (const Hook& hook : hooks_) {
        if (hook.type == type && hook.kind == kind) {
            matching.push_back(hook.function);
        }
    }
    for (const auto& hook : matching) {
        (*hook)(*this, entity);
    }
}

}  // namespace bdex::ecs
