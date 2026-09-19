#pragma once

#include <bdex/audio/Mixer.hpp>
#include <bdex/ecs/World.hpp>
#include <bdex/scene/Serialization.hpp>

#include <memory>
#include <unordered_map>

namespace bdex::audio {

/// A sound attached to an entity. AudioSystem plays it (once, if
/// `autoplay`) and keeps a spatial one where its entity is; volume and
/// pitch changes apply while it plays.
struct AudioSource {
    std::shared_ptr<const assets::AudioData> clip;
    PlayParams params{.spatial = true};
    bool autoplay = true;

    // Runtime state, kept by AudioSystem.
    VoiceId voice = 0;
    bool started = false;

    AudioSource() = default;
    AudioSource(std::shared_ptr<const assets::AudioData> sound, const PlayParams& play = {.spatial = true},
                bool startAtOnce = true)
        : clip(std::move(sound)), params(play), autoplay(startAtOnce) {}
    // A copy (World::clone, say) plays its own sound rather than sharing
    // the original's voice. Moves keep it.
    AudioSource(const AudioSource& other) : clip(other.clip), params(other.params), autoplay(other.autoplay) {}
    AudioSource& operator=(const AudioSource& other) {
        clip = other.clip;
        params = other.params;
        autoplay = other.autoplay;
        voice = 0;
        started = false;
        return *this;
    }
    AudioSource(AudioSource&&) noexcept = default;
    AudioSource& operator=(AudioSource&&) noexcept = default;
    ~AudioSource() = default;
};

/// Plays a source again from the start on the next update.
inline void restart(AudioSource& source) {
    source.voice = 0;
    source.started = false;
    source.autoplay = true;
}

/// Stops a source on the next update (until restart()).
inline void stop(AudioSource& source) {
    source.voice = 0;
    source.started = true;
}

/// Marks the entity the mixer hears from (its WorldTransform), usually the
/// camera. With several, the first found is used.
struct AudioListener {};

/// Registers AudioListener with scene files. (AudioSource is not saved: a
/// clip has no file name to write.)
void addComponentSerializers(scene::ComponentSerializers& serializers);

/// Connects entities to the mixer, once per frame after world transforms.
/// Holds the mixer: release it before the engine shuts down (see
/// ServiceRegistry::tryGetShared).
class AudioSystem {
public:
    explicit AudioSystem(std::shared_ptr<Mixer> mixer) : mixer_(std::move(mixer)) {}

    /// Places the listener, starts sources due to play, moves and adjusts
    /// the voices of the others, and stops voices whose source is gone
    /// (destroyed entity, removed component, stop()) or that belong to
    /// another world than this one: a scene's sounds stop when another
    /// becomes the one updated.
    void update(ecs::World& world);

    /// Voices this system is keeping track of.
    [[nodiscard]] usize trackedVoices() const { return owners_.size(); }

private:
    struct Owner {
        const ecs::World* world;
        ecs::Entity entity;
    };

    std::shared_ptr<Mixer> mixer_;
    std::unordered_map<VoiceId, Owner> owners_;
};

}  // namespace bdex::audio
