#include <phi/audio/Components.hpp>

#include <phi/scene/Components.hpp>

namespace phi::audio {

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

void addAudioSourceSerializer(scene::ComponentSerializers& serializers, const SoundNames& sounds) {
    // Saving cannot fail: a sound without a name is written as an empty
    // name with `unnamed` set, which loading refuses.
    serializers.add<AudioSource>(
        "AudioSource",
        [&sounds](const AudioSource& source, scene::Fields& out, const scene::EntityRefs&) {
            using scene::formatField;
            const std::string_view name = sounds.nameOf(source.clip.get());
            out["sound"] = std::string(name);
            if (source.clip != nullptr && name.empty()) {
                out["unnamed"] = formatField(true);
            }
            const PlayParams& params = source.params;
            out["volume"] = formatField(params.volume);
            out["pitch"] = formatField(params.pitch);
            out["pan"] = formatField(params.pan);
            out["loop"] = formatField(params.loop);
            out["spatial"] = formatField(params.spatial);
            out["min_distance"] = formatField(params.minDistance);
            out["max_distance"] = formatField(params.maxDistance);
            out["bus"] = std::string(busName(params.bus));
            out["autoplay"] = formatField(source.autoplay);
        },
        [&sounds](AudioSource& source, const scene::Fields& in, const scene::EntityRefs&) -> Result<> {
            using scene::parseField;
            std::string name;
            std::string bus = std::string(busName(source.params.bus));
            bool unnamed = false;
            PlayParams& params = source.params;
            for (auto parsed : {parseField(in, "sound", name), parseField(in, "unnamed", unnamed),
                                parseField(in, "volume", params.volume), parseField(in, "pitch", params.pitch),
                                parseField(in, "pan", params.pan), parseField(in, "loop", params.loop),
                                parseField(in, "spatial", params.spatial),
                                parseField(in, "min_distance", params.minDistance),
                                parseField(in, "max_distance", params.maxDistance), parseField(in, "bus", bus),
                                parseField(in, "autoplay", source.autoplay)}) {
                if (!parsed) {
                    return parsed;
                }
            }
            if (const auto known = busNamed(bus)) {
                params.bus = *known;
            } else {
                return fail("unknown bus '{}' (effects, music, voice or ambience)", bus);
            }
            if (unnamed) {
                return fail("an audio source was saved with a sound that had no name (name it with "
                            "audio::SoundNames)");
            }
            if (!name.empty()) {
                source.clip = sounds.find(name);
                if (source.clip == nullptr) {
                    return fail("unknown sound '{}' (name it with audio::SoundNames)", name);
                }
            }
            return {};
        });
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

}  // namespace phi::audio
