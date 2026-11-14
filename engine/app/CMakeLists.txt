phi_add_library(app
    SOURCES
        src/Application.cpp
    PUBLIC_DEPS
        phi::core
        phi::ecs
        phi::platform
        phi::rhi
        phi::assets
        phi::scene
        phi::render
        phi::physics
        phi::audio
        phi::animation
        phi::particles)
# The audio module loaded by default: OpenAL when it was found, else silent.
target_compile_definitions(phi_app PRIVATE PHI_DEFAULT_AUDIO_MODULE="${PHI_AUDIO_MODULE}")
