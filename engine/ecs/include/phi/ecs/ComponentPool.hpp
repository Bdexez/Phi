#pragma once

#include <phi/core/Assert.hpp>
#include <phi/core/Types.hpp>

#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace phi::ecs {

/// Whether World::clone copies a component type. A type whose copy
/// constructor is declared but cannot compile, such as one holding a
/// std::vector<std::unique_ptr<...>>, must opt out:
///
///   template <> struct phi::ecs::Cloneable<Inventory> : std::false_type {};
template <class T>
struct Cloneable : std::bool_constant<std::is_copy_constructible_v<T>> {};

/// Type-erased view of a component pool, used by World to destroy and clone
/// entities without knowing the component types.
class IComponentPool {
public:
    virtual ~IComponentPool() = default;

    virtual void remove(u32 entityIndex) = 0;
    [[nodiscard]] virtual bool contains(u32 entityIndex) const = 0;
    [[nodiscard]] virtual usize size() const = 0;
    /// Indices of the entities owning a component, in dense storage order.
    [[nodiscard]] virtual std::span<const u32> entities() const = 0;
    virtual void clear() = 0;
    /// Gives entity `to` a copy of entity `from`'s component. Returns false,
    /// doing nothing, if `from` has none or the type cannot be copied.
    virtual bool copy(u32 from, u32 to) = 0;
};

/// Sparse-set storage: components are packed in a contiguous array (fast
/// iteration), with O(1) lookup, insertion and removal by entity index.
template <class T>
class ComponentPool final : public IComponentPool {
public:
    template <class... Args>
    T& emplace(u32 entityIndex, Args&&... args) {
        if (contains(entityIndex)) {
            T& existing = dense_[sparse_[entityIndex]];
            existing = T{std::forward<Args>(args)...};
            return existing;
        }
        if (entityIndex >= sparse_.size()) {
            sparse_.resize(static_cast<usize>(entityIndex) + 1, kAbsent);
        }
        sparse_[entityIndex] = static_cast<u32>(dense_.size());
        denseEntities_.push_back(entityIndex);
        dense_.push_back(T{std::forward<Args>(args)...});
        return dense_.back();
    }

    void remove(u32 entityIndex) override {
        if (!contains(entityIndex)) {
            return;
        }
        // Swap-and-pop keeps the dense arrays contiguous.
        const u32 slot = sparse_[entityIndex];
        const u32 last = static_cast<u32>(dense_.size() - 1);
        if (slot != last) {
            dense_[slot] = std::move(dense_[last]);
            denseEntities_[slot] = denseEntities_[last];
            sparse_[denseEntities_[slot]] = slot;
        }
        dense_.pop_back();
        denseEntities_.pop_back();
        sparse_[entityIndex] = kAbsent;
    }

    [[nodiscard]] bool contains(u32 entityIndex) const override {
        return entityIndex < sparse_.size() && sparse_[entityIndex] != kAbsent;
    }

    [[nodiscard]] T& get(u32 entityIndex) {
        PHI_ASSERT(contains(entityIndex), "entity does not have this component");
        return dense_[sparse_[entityIndex]];
    }
    [[nodiscard]] const T& get(u32 entityIndex) const {
        PHI_ASSERT(contains(entityIndex), "entity does not have this component");
        return dense_[sparse_[entityIndex]];
    }

    [[nodiscard]] T* tryGet(u32 entityIndex) {
        return contains(entityIndex) ? &dense_[sparse_[entityIndex]] : nullptr;
    }
    [[nodiscard]] const T* tryGet(u32 entityIndex) const {
        return contains(entityIndex) ? &dense_[sparse_[entityIndex]] : nullptr;
    }

    [[nodiscard]] usize size() const override { return dense_.size(); }
    [[nodiscard]] std::span<const u32> entities() const override { return denseEntities_; }
    [[nodiscard]] std::span<T> values() { return dense_; }
    [[nodiscard]] std::span<const T> values() const { return dense_; }
    [[nodiscard]] T& valueAt(usize denseSlot) { return dense_[denseSlot]; }
    [[nodiscard]] const T& valueAt(usize denseSlot) const { return dense_[denseSlot]; }

    void clear() override {
        sparse_.clear();
        dense_.clear();
        denseEntities_.clear();
    }

    bool copy(u32 from, u32 to) override {
        if constexpr (Cloneable<T>::value) {
            if (!contains(from)) {
                return false;
            }
            T value = dense_[sparse_[from]];  // emplace may reallocate the source
            emplace(to, std::move(value));
            return true;
        } else {
            return false;
        }
    }

private:
    static constexpr u32 kAbsent = ~0u;

    std::vector<u32> sparse_;         // entity index -> dense slot
    std::vector<u32> denseEntities_;  // dense slot -> entity index
    std::vector<T> dense_;
};

}  // namespace phi::ecs
