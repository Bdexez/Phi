#pragma once

#include <phi/core/Math.hpp>
#include <phi/core/Result.hpp>
#include <phi/ecs/World.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace phi::scene {

/// A component's fields as text, by name.
using Fields = std::map<std::string, std::string, std::less<>>;

/// Translates entity references between a world and a scene file, whose
/// entities are numbered from 0 in the order they are written.
class EntityRefs {
public:
    /// The file's name for an entity; empty for an entity not being saved.
    [[nodiscard]] std::string save(ecs::Entity entity) const;
    /// The entity a file id refers to, or an invalid handle.
    [[nodiscard]] ecs::Entity load(std::string_view id) const;

private:
    friend class ComponentSerializers;
    std::map<u32, u32> fileIdOfSlot_;      ///< When saving: entity slot -> file id.
    /// When loading: file id -> entity. A map, since ids are whatever the
    /// file says: sizing an array by the largest one would let a file claim
    /// any amount of memory.
    std::map<usize, ecs::Entity> entityOfId_;
    const ecs::World* world_ = nullptr;
};

/// How each component type is written to and read from scene files.
/// Applications register their own components next to the built-in ones:
///
///   serializers.add<Health>("Health",
///       [](const Health& h, Fields& out, const EntityRefs&) { out["points"] = format(h.points); },
///       [](Health& h, const Fields& in, const EntityRefs&) { return parse(in, "points", h.points); });
///
/// File format (INI, readable by phi::Config):
///
///   [entity.0]
///   Name.value = Player
///   Transform.position = 0 1 0
///   Parent.entity = 1
///   WorldTransform =            (a component without fields)
class ComponentSerializers {
public:
    template <class T>
    using Save = std::function<void(const T&, Fields&, const EntityRefs&)>;
    template <class T>
    using Load = std::function<Result<>(T&, const Fields&, const EntityRefs&)>;

    /// Registers component T under `name` (letters, digits and '_').
    template <class T>
    void add(std::string name, Save<T> save, Load<T> load) {
        Entry entry;
        entry.name = std::move(name);
        entry.has = [](const ecs::World& world, ecs::Entity entity) { return world.has<T>(entity); };
        entry.save = [save = std::move(save)](const ecs::World& world, ecs::Entity entity, Fields& fields,
                                              const EntityRefs& refs) {
            save(world.get<T>(entity), fields, refs);
        };
        entry.load = [load = std::move(load)](ecs::World& world, ecs::Entity entity, const Fields& fields,
                                              const EntityRefs& refs) -> Result<> {
            T component{};
            if (auto loaded = load(component, fields, refs); !loaded) {
                return loaded;
            }
            world.emplace<T>(entity, std::move(component));
            return {};
        };
        entries_.push_back(std::move(entry));
    }

    /// Name, Transform, Parent and ModelNode. (render::addComponentSerializers adds
    /// cameras and lights.)
    [[nodiscard]] static ComponentSerializers withSceneComponents();

    /// Writes every entity owning at least one registered component, apart
    /// from Transient ones; the world is only read.
    [[nodiscard]] std::string save(const ecs::World& world) const;
    /// Creates the file's entities in `world`, with their components; the
    /// world's existing entities are kept. Components no serializer knows
    /// are skipped. Returns the created entities, in file order.
    [[nodiscard]] Result<std::vector<ecs::Entity>> load(ecs::World& world, std::string_view text) const;

private:
    struct Entry {
        std::string name;
        std::function<bool(const ecs::World&, ecs::Entity)> has;
        std::function<void(const ecs::World&, ecs::Entity, Fields&, const EntityRefs&)> save;
        std::function<Result<>(ecs::World&, ecs::Entity, const Fields&, const EntityRefs&)> load;
    };
    std::vector<Entry> entries_;
};

// --- Field helpers for serializers ------------------------------------------------

[[nodiscard]] std::string formatField(f32 value);
[[nodiscard]] std::string formatField(const Vec3& value);
[[nodiscard]] std::string formatField(const Vec4& value);  ///< "x y z w", colors included
[[nodiscard]] std::string formatField(const Quat& value);  ///< "w x y z"
[[nodiscard]] std::string formatField(bool value);
[[nodiscard]] std::string formatField(u32 value);

/// Reads `fields[key]` into `out`, leaving it untouched when the key is
/// absent; fails on a malformed value.
Result<> parseField(const Fields& fields, std::string_view key, f32& out);
Result<> parseField(const Fields& fields, std::string_view key, Vec3& out);
Result<> parseField(const Fields& fields, std::string_view key, Vec4& out);
Result<> parseField(const Fields& fields, std::string_view key, Quat& out);
Result<> parseField(const Fields& fields, std::string_view key, bool& out);
/// A whole number from 0 to 4294967295; signs, fractions and overflow fail.
Result<> parseField(const Fields& fields, std::string_view key, u32& out);
Result<> parseField(const Fields& fields, std::string_view key, std::string& out);

}  // namespace phi::scene
