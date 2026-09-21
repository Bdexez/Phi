#include <bdex/scene/Serialization.hpp>

#include <bdex/core/Config.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/scene/Components.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace bdex::scene {
namespace {

Result<> parseFloats(std::string_view key, std::string_view text, f32* out, int count) {
    for (int i = 0; i < count; ++i) {
        const usize begin = text.find_first_not_of(' ');
        if (begin == std::string_view::npos) {
            return fail("'{}' needs {} numbers", key, count);
        }
        text.remove_prefix(begin);
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out[i]);
        if (error != std::errc{}) {
            return fail("'{}': '{}' is not a number", key, text);
        }
        text.remove_prefix(static_cast<usize>(end - text.data()));
    }
    if (text.find_first_not_of(' ') != std::string_view::npos) {
        return fail("'{}' has more than {} numbers", key, count);
    }
    return {};
}

}  // namespace

// --- Fields ----------------------------------------------------------------------

std::string formatField(f32 value) {
    return std::format("{}", value);  // shortest form that reads back exactly
}

std::string formatField(const Vec3& value) {
    return std::format("{} {} {}", value.x, value.y, value.z);
}

std::string formatField(const Quat& value) {
    return std::format("{} {} {} {}", value.w, value.x, value.y, value.z);
}

std::string formatField(bool value) {
    return value ? "true" : "false";
}

Result<> parseField(const Fields& fields, std::string_view key, f32& out) {
    const auto it = fields.find(key);
    return it == fields.end() ? Result<>{} : parseFloats(key, it->second, &out, 1);
}

Result<> parseField(const Fields& fields, std::string_view key, Vec3& out) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return {};
    }
    // Through an array: indexing past out.x would leave the member.
    f32 values[3];
    if (auto parsed = parseFloats(key, it->second, values, 3); !parsed) {
        return parsed;
    }
    out = Vec3(values[0], values[1], values[2]);
    return {};
}

Result<> parseField(const Fields& fields, std::string_view key, Quat& out) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return {};
    }
    f32 values[4];
    if (auto parsed = parseFloats(key, it->second, values, 4); !parsed) {
        return parsed;
    }
    out = Quat(values[0], values[1], values[2], values[3]);
    // Hand-edited values may not be unit length; saved ones are, and are
    // kept bit for bit so a load-save round trip is exact.
    if (std::abs(glm::length(out) - 1.0f) > 1e-5f) {
        out = glm::normalize(out);
    }
    return {};
}

Result<> parseField(const Fields& fields, std::string_view key, bool& out) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return {};
    }
    if (it->second == "true" || it->second == "false") {
        out = it->second == "true";
        return {};
    }
    return fail("'{}': '{}' is not true or false", key, it->second);
}

Result<> parseField(const Fields& fields, std::string_view key, std::string& out) {
    if (const auto it = fields.find(key); it != fields.end()) {
        out = it->second;
    }
    return {};
}

// --- EntityRefs ------------------------------------------------------------------

std::string EntityRefs::save(ecs::Entity entity) const {
    if (world_ == nullptr || !world_->alive(entity)) {
        return {};
    }
    const auto it = fileIdOfSlot_.find(entity.index);
    return it == fileIdOfSlot_.end() ? std::string{} : std::to_string(it->second);
}

ecs::Entity EntityRefs::load(std::string_view id) const {
    usize index = 0;
    const auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), index);
    if (error != std::errc{} || end != id.data() + id.size()) {
        return {};
    }
    const auto it = entityOfId_.find(index);
    return it == entityOfId_.end() ? ecs::Entity{} : it->second;
}

// --- ComponentSerializers --------------------------------------------------------

ComponentSerializers ComponentSerializers::withSceneComponents() {
    ComponentSerializers serializers;
    serializers.add<Name>(
        "Name", [](const Name& name, Fields& out, const EntityRefs&) { out["value"] = name.value; },
        [](Name& name, const Fields& in, const EntityRefs&) { return parseField(in, "value", name.value); });
    serializers.add<Transform>(
        "Transform",
        [](const Transform& transform, Fields& out, const EntityRefs&) {
            out["position"] = formatField(transform.position);
            out["rotation"] = formatField(transform.rotation);
            out["scale"] = formatField(transform.scale);
        },
        [](Transform& transform, const Fields& in, const EntityRefs&) -> Result<> {
            if (auto parsed = parseField(in, "position", transform.position); !parsed) return parsed;
            if (auto parsed = parseField(in, "rotation", transform.rotation); !parsed) return parsed;
            return parseField(in, "scale", transform.scale);
        });
    serializers.add<Parent>(
        "Parent",
        [](const Parent& parent, Fields& out, const EntityRefs& refs) { out["entity"] = refs.save(parent.entity); },
        [](Parent& parent, const Fields& in, const EntityRefs& refs) -> Result<> {
            const auto it = in.find("entity");
            if (it == in.end()) {
                return fail("Parent needs an entity");
            }
            parent.entity = refs.load(it->second);
            if (!parent.entity.valid() && !it->second.empty()) {
                return fail("Parent refers to unknown entity '{}'", it->second);
            }
            return {};
        });
    // Derived every frame from Transform and Parent: nothing to store, but
    // entities need one to be drawn.
    serializers.add<WorldTransform>(
        "WorldTransform", [](const WorldTransform&, Fields&, const EntityRefs&) {},
        [](WorldTransform&, const Fields&, const EntityRefs&) -> Result<> { return {}; });
    return serializers;
}

std::string ComponentSerializers::save(ecs::World& world) const {
    // First number the entities owning a registered component, in slot
    // order (stable across saves), so references can be written...
    EntityRefs refs;
    refs.world_ = &world;
    std::vector<ecs::Entity> saved;
    for (u32 slot = 0; slot < world.slotCount(); ++slot) {
        const ecs::Entity entity = world.entityAt(slot);
        const bool known = world.alive(entity) &&
                           std::any_of(entries_.begin(), entries_.end(),
                                       [&](const Entry& entry) { return entry.has(world, entity); });
        if (known) {
            refs.fileIdOfSlot_[slot] = static_cast<u32>(saved.size());
            saved.push_back(entity);
        }
    }

    // ...then write them.
    std::string text = "# bdex scene\n";
    for (usize id = 0; id < saved.size(); ++id) {
        text += std::format("\n[entity.{}]\n", id);
        for (const Entry& entry : entries_) {
            if (!entry.has(world, saved[id])) {
                continue;
            }
            Fields fields;
            entry.save(world, saved[id], fields, refs);
            if (fields.empty()) {
                text += std::format("{} =\n", entry.name);
            }
            for (const auto& [key, value] : fields) {
                text += std::format("{}.{} = {}\n", entry.name, key, value);
            }
        }
    }
    return text;
}

Result<std::vector<ecs::Entity>> ComponentSerializers::load(ecs::World& world, std::string_view text) const {
    auto config = Config::parse(text);
    if (!config) {
        return std::unexpected(config.error());
    }

    // entity id -> component name -> fields
    std::map<usize, std::map<std::string, Fields, std::less<>>> entities;
    for (const auto& [key, value] : config->entries()) {
        // "entity.<id>.<Component>[.<field>]"
        constexpr std::string_view kPrefix = "entity.";
        if (!key.starts_with(kPrefix)) {
            return fail("scene: unexpected key '{}'", key);
        }
        const std::string_view rest = std::string_view(key).substr(kPrefix.size());
        const usize dot = rest.find('.');
        usize id = 0;
        const auto [end, error] = std::from_chars(rest.data(), rest.data() + (dot == std::string_view::npos ? rest.size() : dot), id);
        if (error != std::errc{} || dot == std::string_view::npos || end != rest.data() + dot) {
            return fail("scene: malformed key '{}'", key);
        }
        const std::string_view componentAndField = rest.substr(dot + 1);
        const usize fieldDot = componentAndField.find('.');
        const std::string component(componentAndField.substr(0, fieldDot));
        Fields& fields = entities[id][component];  // created even without fields
        if (fieldDot != std::string_view::npos) {
            fields[std::string(componentAndField.substr(fieldDot + 1))] = value;
        }
    }

    // Two passes: every entity exists before components refer to them.
    EntityRefs refs;
    refs.world_ = &world;
    std::vector<ecs::Entity> created;
    for (const auto& [id, components] : entities) {
        refs.entityOfId_[id] = world.create();
        created.push_back(refs.entityOfId_[id]);
    }
    for (const auto& [id, components] : entities) {
        for (const auto& [name, fields] : components) {
            const auto entry = std::find_if(entries_.begin(), entries_.end(),
                                            [&](const Entry& candidate) { return candidate.name == name; });
            if (entry == entries_.end()) {
                log::warn("scene", "skipping unknown component '{}' of entity {}", name, id);
                continue;
            }
            if (auto loaded = entry->load(world, refs.entityOfId_[id], fields, refs); !loaded) {
                for (const ecs::Entity entity : created) {
                    world.destroy(entity);  // leave the world as it was
                }
                return fail("scene: entity {}: {}", id, loaded.error().message);
            }
        }
    }
    return created;
}

}  // namespace bdex::scene
