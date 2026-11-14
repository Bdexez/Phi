#include <phi/render/Serialization.hpp>

#include <phi/render/Components.hpp>

namespace phi::render {

using scene::EntityRefs;
using scene::Fields;
using scene::formatField;
using scene::parseField;

namespace {

// Chains field parsers, stopping at the first failure.
template <class... Parsers>
Result<> parseAll(Parsers&&... parsers) {
    Result<> result;
    ((result ? (result = parsers(), 0) : 0), ...);
    return result;
}

}  // namespace

void addComponentSerializers(scene::ComponentSerializers& serializers) {
    serializers.add<Camera>(
        "Camera",
        [](const Camera& camera, Fields& out, const EntityRefs&) {
            out["vertical_fov"] = formatField(camera.verticalFov);
            out["near"] = formatField(camera.nearPlane);
            out["far"] = formatField(camera.farPlane);
            out["primary"] = formatField(camera.primary);
        },
        [](Camera& camera, const Fields& in, const EntityRefs&) {
            return parseAll([&] { return parseField(in, "vertical_fov", camera.verticalFov); },
                            [&] { return parseField(in, "near", camera.nearPlane); },
                            [&] { return parseField(in, "far", camera.farPlane); },
                            [&] { return parseField(in, "primary", camera.primary); });
        });
    serializers.add<DirectionalLight>(
        "DirectionalLight",
        [](const DirectionalLight& light, Fields& out, const EntityRefs&) {
            out["direction"] = formatField(light.direction);
            out["color"] = formatField(light.color);
            out["intensity"] = formatField(light.intensity);
            out["ambient"] = formatField(light.ambient);
            out["cast_shadows"] = formatField(light.castShadows);
        },
        [](DirectionalLight& light, const Fields& in, const EntityRefs&) {
            return parseAll([&] { return parseField(in, "direction", light.direction); },
                            [&] { return parseField(in, "color", light.color); },
                            [&] { return parseField(in, "intensity", light.intensity); },
                            [&] { return parseField(in, "ambient", light.ambient); },
                            [&] { return parseField(in, "cast_shadows", light.castShadows); });
        });
    serializers.add<SpotLight>(
        "SpotLight",
        [](const SpotLight& light, Fields& out, const EntityRefs&) {
            out["color"] = formatField(light.color);
            out["intensity"] = formatField(light.intensity);
            out["range"] = formatField(light.range);
            out["inner_angle"] = formatField(light.innerAngle);
            out["outer_angle"] = formatField(light.outerAngle);
            out["cast_shadows"] = formatField(light.castShadows);
        },
        [](SpotLight& light, const Fields& in, const EntityRefs&) {
            return parseAll([&] { return parseField(in, "color", light.color); },
                            [&] { return parseField(in, "intensity", light.intensity); },
                            [&] { return parseField(in, "range", light.range); },
                            [&] { return parseField(in, "inner_angle", light.innerAngle); },
                            [&] { return parseField(in, "outer_angle", light.outerAngle); },
                            [&] { return parseField(in, "cast_shadows", light.castShadows); });
        });
    serializers.add<PointLight>(
        "PointLight",
        [](const PointLight& light, Fields& out, const EntityRefs&) {
            out["color"] = formatField(light.color);
            out["intensity"] = formatField(light.intensity);
            out["range"] = formatField(light.range);
        },
        [](PointLight& light, const Fields& in, const EntityRefs&) {
            return parseAll([&] { return parseField(in, "color", light.color); },
                            [&] { return parseField(in, "intensity", light.intensity); },
                            [&] { return parseField(in, "range", light.range); });
        });
}

}  // namespace phi::render
