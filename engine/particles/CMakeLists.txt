bdex_add_library(particles
    SOURCES
        src/Particles.cpp
    PUBLIC_DEPS
        bdex::core
        bdex::ecs
        bdex::scene)
