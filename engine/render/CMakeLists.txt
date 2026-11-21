phi_add_library(render
    SOURCES
        src/Camera.cpp
        src/DebugDraw.cpp
        src/Frustum.cpp
        src/Model.cpp
        src/ResourceNames.cpp
        src/Serialization.cpp
        src/Shadows.cpp
    PUBLIC_DEPS
        phi::core
        phi::ecs
        phi::rhi
        phi::assets
        phi::scene)

# GLSL headers shared by every renderer and by application shaders
# (#include "phi/frame_uniforms.glsl"); picked up by phi_compile_shaders().
set(PHI_SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    CACHE INTERNAL "Include directory for engine GLSL headers")
