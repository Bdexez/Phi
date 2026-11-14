#pragma once

#include <phi/scene/Serialization.hpp>

namespace phi::render {

/// Registers Camera, DirectionalLight, PointLight and SpotLight with scene
/// files.
/// MeshRenderer is left out: its handles only mean something to the
/// renderer that created them; addMeshRendererSerializer (ResourceNames.hpp)
/// saves it by name.
void addComponentSerializers(scene::ComponentSerializers& serializers);

}  // namespace phi::render
