#include <bdex/audio/Components.hpp>

#include <bdex/scene/Components.hpp>

namespace bdex::audio {

namespace {

/// Where an entity is: its world transform if computed, else its local one.
Vec3 positionOf(const ecs::World& world, ecs::Entity entity) {
    if (const auto* placed = world.tryGet<scene::WorldTransform>(entity)) {
        return Vec3(placed->matrix[3]);
    }
    if (const auto* local = world.tryGet<scene::Transform>(entity)) {
        return local->position;
    }
    return Vec3(0.0f);
}

}  // namespace

void addComponentSerializers(scene::ComponentSerializers& serializers) {
    serializers.add<AudioListener>(
        "AudioListener", [](const AudioListener&, scene::Fields&, const scene::EntityRefs&) {},
        [](AudioListener&, const scene::Fields&, const scene::EntityRefs&) -> Result<> { return {}; });
}

void AudioSystem::update(ecs::World& world) {
    // The ears.
    bool heard = false;
    world.view<const AudioListener>().each([&](ecs::Entity entity, const AudioListener&) {
        if (heard) {
            return;
        }
        heard = true;
        Quat facing(1.0f, 0.0f, 0.0f, 0.0f);
        if (const auto* placed = world.tryGet<scene::WorldTransform>(entity)) {
            const Mat3 axes(placed->matrix);
            facing = glm::quat_cast(Mat3(glm::normalize(axes[0]), glm::normalize(axes[1]), glm::normalize(axes[2])));
        } else if (const auto* local = world.tryGet<scene::Transform>(entity)) {
            facing = local->rotation;
        }
        mixer_->setListener(positionOf(world, entity), facing);
    });

    // Voices whose source went away.
    std::erase_if(owners_, [&](const auto& entry) {
        const auto& [voice, owner] = entry;
        const AudioSource* source = owner.world == &world ? world.tryGet<AudioSource>(owner.entity) : nullptr;
        if (source != nullptr && source->voice == voice) {
            return false;
        }
        mixer_->stop(voice);
        return true;
    });

    // Sources.
    world.view<AudioSource>().each([&](ecs::Entity entity, AudioSource& source) {
        PlayParams params = source.params;
        params.position = positionOf(world, entity);
        if (!source.started && source.autoplay) {
            source.started = true;
            source.voice = mixer_->play(source.clip, params);
            if (source.voice != 0) {
                owners_[source.voice] = {&world, entity};
            }
            return;
        }
        if (source.voice != 0) {
            if (mixer_->playing(source.voice)) {
                mixer_->update(source.voice, params);
            } else {
                owners_.erase(source.voice);
                source.voice = 0;  // finished
            }
        }
    });
}

}  // namespace bdex::audio
