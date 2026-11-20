bdex_add_library(physics
    SOURCES
        src/BoxDynamics.cpp
        src/Physics.cpp
    PUBLIC_DEPS
        bdex::core
        bdex::ecs
        bdex::scene)
