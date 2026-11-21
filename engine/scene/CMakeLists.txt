phi_add_library(scene
    SOURCES
        src/Scene.cpp
        src/Serialization.cpp
    PUBLIC_DEPS
        phi::core
        phi::ecs)
