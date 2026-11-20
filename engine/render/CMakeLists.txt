bdex_add_library(render
    SOURCES
        src/Camera.cpp
        src/DebugDraw.cpp
        src/Frustum.cpp
        src/Model.cpp
        src/ResourceNames.cpp
        src/Serialization.cpp
        src/Shadows.cpp
    PUBLIC_DEPS
        bdex::core
        bdex::ecs
        bdex::rhi
        bdex::assets
        bdex::scene)

# GLSL headers shared by every renderer and by application shaders
# (#include "bdex/frame_uniforms.glsl"); picked up by bdex_compile_shaders().
set(BDEX_SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    CACHE INTERNAL "Include directory for engine GLSL headers")
