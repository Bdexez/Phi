#pragma once

#include <bdex/core/Engine.hpp>
#include <bdex/core/EventBus.hpp>
#include <bdex/ecs/World.hpp>

#include <vector>

namespace sandbox {

/// A burst of glowing sparks wherever something hits hard enough, more for
/// harder hits, thrown off the surface struck; and glowing trails behind
/// fast bodies that carry one.
class Sparks {
public:
    void start(bdex::Engine& engine);
    /// Removes the bursts whose sparks have all died, and lights trails on
    /// bodies moving fast enough.
    void update(bdex::ecs::World& world);
    /// Gives a body a trail, lit while it moves faster than kTrailSpeed.
    static void addTrail(bdex::ecs::World& world, bdex::ecs::Entity body);
    /// Whether hits make sparks and trails glow (what is flying finishes).
    bool enabled = true;

    static constexpr bdex::f32 kTrailSpeed = 4.0f;  ///< m/s.

private:
    static constexpr bdex::usize kMaxBursts = 32;  ///< A settling pile does not fill the screen.

    bdex::Subscription collisions_;
    std::vector<bdex::ecs::Entity> bursts_;
    bdex::u32 seed_ = 1;
};

}  // namespace sandbox
