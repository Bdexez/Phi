#pragma once

#include <phi/core/Engine.hpp>
#include <phi/core/EventBus.hpp>
#include <phi/ecs/World.hpp>

#include <vector>

namespace sandbox {

/// A burst of glowing sparks wherever something hits hard enough, more for
/// harder hits, thrown off the surface struck; and glowing trails behind
/// fast bodies that carry one.
class Sparks {
public:
    void start(phi::Engine& engine);
    /// Removes the bursts whose sparks have all died, and lights trails on
    /// bodies moving fast enough.
    void update(phi::ecs::World& world);
    /// Gives a body a trail, lit while it moves faster than kTrailSpeed.
    static void addTrail(phi::ecs::World& world, phi::ecs::Entity body);
    /// Whether hits make sparks and trails glow (what is flying finishes).
    bool enabled = true;

    static constexpr phi::f32 kTrailSpeed = 4.0f;  ///< m/s.

private:
    static constexpr phi::usize kMaxBursts = 32;  ///< A settling pile does not fill the screen.

    phi::Subscription collisions_;
    std::vector<phi::ecs::Entity> bursts_;
    phi::u32 seed_ = 1;
};

}  // namespace sandbox
