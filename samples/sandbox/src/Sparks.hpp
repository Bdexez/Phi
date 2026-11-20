#pragma once

#include <bdex/core/Engine.hpp>
#include <bdex/core/EventBus.hpp>
#include <bdex/ecs/World.hpp>

#include <vector>

namespace sandbox {

/// A burst of glowing sparks wherever something hits hard enough, more for
/// harder hits, thrown off the surface struck.
class Sparks {
public:
    void start(bdex::Engine& engine);
    /// Removes the bursts whose sparks have all died.
    void update(bdex::ecs::World& world);
    /// Whether hits make sparks (bursts already flying finish).
    bool enabled = true;

private:
    static constexpr bdex::usize kMaxBursts = 32;  ///< A settling pile does not fill the screen.

    bdex::Subscription collisions_;
    std::vector<bdex::ecs::Entity> bursts_;
    bdex::u32 seed_ = 1;
};

}  // namespace sandbox
