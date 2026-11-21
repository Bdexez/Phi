phi_add_library(physics
    SOURCES
        src/BoxDynamics.cpp
        src/Physics.cpp
    PUBLIC_DEPS
        phi::core
        phi::ecs
        phi::scene)
