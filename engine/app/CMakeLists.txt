bdex_add_library(app
    SOURCES
        src/Application.cpp
    PUBLIC_DEPS
        bdex::core
        bdex::ecs
        bdex::platform
        bdex::rhi
        bdex::assets
        bdex::scene
        bdex::render
        bdex::physics
        bdex::audio
        bdex::animation
        bdex::particles)
# The audio module loaded by default: OpenAL when it was found, else silent.
target_compile_definitions(bdex_app PRIVATE BDEX_DEFAULT_AUDIO_MODULE="${BDEX_AUDIO_MODULE}")
