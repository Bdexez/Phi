#pragma once

#include <phi/assets/Audio.hpp>
#include <phi/core/Engine.hpp>
#include <phi/core/EventBus.hpp>

#include <memory>

namespace sandbox {

/// The sandbox's sounds, made here rather than loaded, like its textures: a
/// whoosh for throws, and a knock for each collision, played where it
/// happens and as loud as it is hard.
class Sounds {
public:
    /// Synthesizes the sounds and starts playing impacts; nothing without a
    /// mixer (no audio module).
    void start(phi::Engine& engine);
    void playThrow(phi::Engine& engine) const;

private:
    std::shared_ptr<const phi::assets::AudioData> throw_;
    std::shared_ptr<const phi::assets::AudioData> impact_;
    phi::Subscription collisions_;
};

}  // namespace sandbox
