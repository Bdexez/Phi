bdex_add_module(renderer_forward
    SOURCES
        src/DebugFeature.cpp
        src/ForwardRenderer.cpp
        src/ForwardRendererModule.cpp
        src/MeshFeature.cpp
        src/Overlay.cpp
        src/ParticleFeature.cpp
        src/PostProcess.cpp
    DEPS
        bdex::core
        bdex::ecs
        bdex::platform
        bdex::rhi
        bdex::assets
        bdex::scene
        bdex::render
        bdex::particles)

# Shaders are loaded at runtime from "engine:/shaders/...".
bdex_compile_shaders(bdex_renderer_forward_shaders
    OUTPUT_DIR ${BDEX_ASSET_OUTPUT_DIR}/engine/shaders
    SOURCES
        shaders/mesh.vert
        shaders/mesh.frag
        shaders/shadow.vert
        shaders/shadow.frag
        shaders/fullscreen.vert
        shaders/tonemap.frag
        shaders/debug.vert
        shaders/debug.frag
        shaders/overlay.vert
        shaders/particle.vert
        shaders/particle.frag
        shaders/bloom_down.frag
        shaders/bloom_up.frag
        shaders/fxaa.frag)
add_dependencies(bdex_module_renderer_forward bdex_renderer_forward_shaders)
