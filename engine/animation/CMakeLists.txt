bdex_add_library(animation
    SOURCES
        src/Animation.cpp
    PUBLIC_DEPS
        bdex::core
        bdex::ecs
        bdex::assets
        bdex::scene)
