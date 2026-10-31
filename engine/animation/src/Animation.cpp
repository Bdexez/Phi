#include <bdex/animation/Animation.hpp>

#include <bdex/scene/Components.hpp>
#include <bdex/scene/Scene.hpp>

#include <algorithm>
#include <cmath>

namespace bdex::animation {

namespace {

/// node index -> entity, for the nodes below `root`.
std::vector<ecs::Entity> findTargets(const ecs::World& world, ecs::Entity root) {
    std::vector<ecs::Entity> targets;
    for (const ecs::Entity entity : scene::branch(world, root)) {
        if (const auto* node = world.tryGet<scene::ModelNode>(entity)) {
            if (node->index >= targets.size()) {
                targets.resize(static_cast<usize>(node->index) + 1);
            }
            if (!targets[node->index].valid()) {
                targets[node->index] = entity;  // the nearest copy of a node wins
            }
        }
    }
    return targets;
}

void pose(ecs::World& world, const std::vector<ecs::Entity>& targets, const assets::ModelAnimation& clip, f32 time) {
    for (const assets::AnimationChannel& channel : clip.channels) {
        if (channel.node >= targets.size()) {
            continue;
        }
        auto* transform = world.tryGet<scene::Transform>(targets[channel.node]);
        if (transform == nullptr) {
            continue;
        }
        const Vec4 value = assets::sampleChannel(channel, time);
        switch (channel.property) {
            case assets::AnimatedProperty::Translation: transform->position = Vec3(value); break;
            case assets::AnimatedProperty::Rotation: transform->rotation = Quat(value.w, value.x, value.y, value.z); break;
            case assets::AnimatedProperty::Scale: transform->scale = Vec3(value); break;
        }
    }
}

}  // namespace

void applyAnimation(ecs::World& world, ecs::Entity root, const assets::ModelAnimation& clip, f32 time) {
    pose(world, findTargets(world, root), clip, time);
}

void updateAnimations(ecs::World& world, f32 dt) {
    // Players first, then their poses: finding targets walks the hierarchy,
    // which is no business of a view's iteration.
    std::vector<std::pair<ecs::Entity, AnimationPlayer*>> players;
    world.view<AnimationPlayer>().each([&](ecs::Entity entity, AnimationPlayer& player) {
        if (player.clip != nullptr) {
            players.emplace_back(entity, &player);
        }
    });
    for (const auto& [entity, player] : players) {
        const f32 duration = player->clip->duration;
        if (player->playing && std::isfinite(player->speed) && std::isfinite(dt)) {
            player->time += dt * player->speed;
            if (player->loop && duration > 0.0f) {
                player->time = std::fmod(player->time, duration);
                if (player->time < 0.0f) {
                    player->time += duration;
                }
            } else if (player->time >= duration || player->time <= 0.0f) {
                player->time = std::clamp(player->time, 0.0f, duration);
                player->playing = player->speed > 0.0f ? player->time < duration : player->time > 0.0f;
            }
        }
        // Targets are found once, and again when one disappears.
        const bool stale = player->targets.empty() ||
                           std::any_of(player->targets.begin(), player->targets.end(),
                                       [&](ecs::Entity target) { return target.valid() && !world.alive(target); });
        if (stale) {
            player->targets = findTargets(world, entity);
        }
        pose(world, player->targets, *player->clip, player->time);
    }
}

void ClipNames::name(std::shared_ptr<const assets::ModelAnimation> clip, std::string_view name) {
    if (clip == nullptr) {
        return;
    }
    // Free the name and the clip of what they meant before.
    if (const auto named = byName_.find(std::string(name)); named != byName_.end()) {
        byClip_.erase(named->second.get());
        byName_.erase(named);
    }
    if (const auto previous = byClip_.find(clip.get()); previous != byClip_.end()) {
        byName_.erase(previous->second);
        byClip_.erase(previous);
    }
    byClip_.emplace(clip.get(), std::string(name));
    byName_.emplace(std::string(name), std::move(clip));
}

std::shared_ptr<const assets::ModelAnimation> ClipNames::clip(std::string_view name) const {
    const auto it = byName_.find(std::string(name));
    return it == byName_.end() ? nullptr : it->second;
}

std::string_view ClipNames::nameOf(const assets::ModelAnimation* clip) const {
    const auto it = byClip_.find(clip);
    return it == byClip_.end() ? std::string_view{} : std::string_view(it->second);
}

void addComponentSerializers(scene::ComponentSerializers& serializers, const ClipNames& clips) {
    // Saving cannot fail: a clip without a name is written as an empty name
    // with `unnamed` set, which loading refuses.
    serializers.add<AnimationPlayer>(
        "AnimationPlayer",
        [&clips](const AnimationPlayer& player, scene::Fields& out, const scene::EntityRefs&) {
            const std::string_view name = clips.nameOf(player.clip.get());
            out["clip"] = std::string(name);
            if (player.clip != nullptr && name.empty()) {
                out["unnamed"] = scene::formatField(true);
            }
            out["time"] = scene::formatField(player.time);
            out["speed"] = scene::formatField(player.speed);
            out["loop"] = scene::formatField(player.loop);
            out["playing"] = scene::formatField(player.playing);
        },
        [&clips](AnimationPlayer& player, const scene::Fields& in, const scene::EntityRefs&) -> Result<> {
            std::string name;
            bool unnamed = false;
            for (auto parsed : {scene::parseField(in, "clip", name), scene::parseField(in, "unnamed", unnamed),
                                scene::parseField(in, "time", player.time),
                                scene::parseField(in, "speed", player.speed),
                                scene::parseField(in, "loop", player.loop),
                                scene::parseField(in, "playing", player.playing)}) {
                if (!parsed) {
                    return parsed;
                }
            }
            if (unnamed) {
                return fail("an animation player was saved with a clip that had no name (name it with "
                            "animation::ClipNames)");
            }
            if (!name.empty()) {
                player.clip = clips.clip(name);
                if (player.clip == nullptr) {
                    return fail("unknown animation clip '{}' (name it with animation::ClipNames)", name);
                }
            }
            return {};
        });
}

}  // namespace bdex::animation
