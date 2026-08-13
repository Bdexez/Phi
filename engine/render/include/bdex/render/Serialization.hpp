#pragma once

#include <bdex/scene/Serialization.hpp>

namespace bdex::render {

/// Registers Camera, DirectionalLight and PointLight with scene files.
/// MeshRenderer is left out: its handles only mean something to the
/// renderer that created them, so applications save their own reference
/// (an asset path, say) and rebuild it on load.
void addComponentSerializers(scene::ComponentSerializers& serializers);

}  // namespace bdex::render
