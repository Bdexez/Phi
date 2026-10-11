#pragma once

#include <bdex/assets/Audio.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/core/EventBus.hpp>

#include <memory>

namespace sandbox {

/// The sandbox's sounds, made here rather than loaded, like its textures: a
/// whoosh for throws, and a knock for each collision, played where it
/// happens and as loud as it is hard.
class Sounds {
public:
    /// Synthesizes the sounds and starts playing impacts; nothing without a
    /// mixer (no audio module).
    void start(bdex::Engine& engine);
    void playThrow(bdex::Engine& engine) const;

private:
    std::shared_ptr<const bdex::assets::AudioData> throw_;
    std::shared_ptr<const bdex::assets::AudioData> impact_;
    bdex::Subscription collisions_;
};

}  // namespace sandbox
