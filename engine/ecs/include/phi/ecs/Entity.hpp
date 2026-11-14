#pragma once

#include <phi/core/Types.hpp>

#include <functional>

namespace phi::ecs {

/// Lightweight identifier of an entity: a slot index plus a generation that
/// invalidates stale copies once the entity is destroyed.
struct Entity {
    static constexpr u32 kInvalidIndex = ~0u;

    u32 index = kInvalidIndex;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const { return index != kInvalidIndex; }
    friend constexpr bool operator==(const Entity&, const Entity&) = default;
};

}  // namespace phi::ecs

template <>
struct std::hash<phi::ecs::Entity> {
    std::size_t operator()(const phi::ecs::Entity& entity) const noexcept {
        return std::hash<phi::u64>{}((static_cast<phi::u64>(entity.generation) << 32) |
                                      entity.index);
    }
};
