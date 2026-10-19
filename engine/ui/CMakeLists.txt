bdex_add_library(ui
    SOURCES
        src/Ui.cpp
    PUBLIC_DEPS
        bdex::core
        bdex::platform
        bdex::render)
