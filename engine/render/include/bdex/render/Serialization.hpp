#pragma once

#include <bdex/scene/Serialization.hpp>

namespace bdex::render {

/// Registers Camera, DirectionalLight, PointLight and SpotLight with scene
/// files.
/// MeshRenderer is left out: its handles only mean something to the
/// renderer that created them; addMeshRendererSerializer (ResourceNames.hpp)
/// saves it by name.
void addComponentSerializers(scene::ComponentSerializers& serializers);

}  // namespace bdex::render
