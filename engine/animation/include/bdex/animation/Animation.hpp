#pragma once

#include <bdex/assets/Gltf.hpp>
#include <bdex/ecs/World.hpp>

#include <memory>
#include <vector>

namespace bdex::animation {

/// Plays a model's animation on the entities instantiated from it. Put it
/// on the model's root entity: the clip's channels name nodes by index,
/// found among the entities below with that scene::ModelNode index, so a
/// copy of the model (World::clone, scene::cloneHierarchy) animates its
/// own nodes.
struct AnimationPlayer {
    std::shared_ptr<const assets::ModelAnimation> clip;
    f32 time = 0.0f;   ///< Seconds into the clip.
    f32 speed = 1.0f;  ///< 2 plays twice as fast; negative plays backward.
    bool loop = true;
    bool playing = true;  ///< Cleared when a clip that does not loop ends.

    // Runtime, kept by updateAnimations: node index -> entity.
    std::vector<ecs::Entity> targets;

    AnimationPlayer() = default;
    AnimationPlayer(std::shared_ptr<const assets::ModelAnimation> animation, bool looping = true)
        : clip(std::move(animation)), loop(looping) {}
    // A copy finds its own nodes again.
    AnimationPlayer(const AnimationPlayer& other)
        : clip(other.clip), time(other.time), speed(other.speed), loop(other.loop), playing(other.playing) {}
    AnimationPlayer& operator=(const AnimationPlayer& other) {
        if (this != &other) {
            clip = other.clip;
            time = other.time;
            speed = other.speed;
            loop = other.loop;
            playing = other.playing;
            targets.clear();
        }
        return *this;
    }
    AnimationPlayer(AnimationPlayer&&) noexcept = default;
    AnimationPlayer& operator=(AnimationPlayer&&) noexcept = default;
    ~AnimationPlayer() = default;
};

/// Advances every playing AnimationPlayer by `dt` seconds (wrapping when
/// looping, stopping at the ends otherwise) and writes its channels into
/// the Transforms of its nodes. Run it before world transforms.
void updateAnimations(ecs::World& world, f32 dt);

/// The pose of `clip` at `time` on the nodes below `root`, without a
/// player (for tools, or to set a starting pose).
void applyAnimation(ecs::World& world, ecs::Entity root, const assets::ModelAnimation& clip, f32 time);

}  // namespace bdex::animation
