add_executable(sandbox
    src/FlyCamera.cpp
    src/Hud.cpp
    src/Lamps.cpp
    src/Level.cpp
    src/Palette.cpp
    src/Sandbox.cpp
    src/SettingsPanel.cpp
    src/SkyFeature.cpp
    src/Sounds.cpp
    src/Sparks.cpp)
target_link_libraries(sandbox PRIVATE phi::app phi::ui)
phi_configure_target(sandbox)

# The executable does not link against any module: these are the ones it can
# load at runtime (plugins), or that get registered statically when
# PHI_SHARED_MODULES is OFF. Which ones actually run is decided by
# `engine.modules` in sandbox.ini / on the command line.
phi_target_modules(sandbox
    window_glfw
    window_null
    rhi_vulkan
    rhi_null
    renderer_forward
    audio_null
    audio_openal)

# Application shaders, loaded from "app:/shaders/...".
phi_compile_shaders(sandbox_shaders
    OUTPUT_DIR ${PHI_ASSET_OUTPUT_DIR}/sandbox/shaders
    SOURCES
        shaders/sky.vert
        shaders/sky.frag)
add_dependencies(sandbox sandbox_shaders)

# Models, loaded from "app:/models/...".
configure_file(models/lamp.gltf ${PHI_ASSET_OUTPUT_DIR}/sandbox/models/lamp.gltf COPYONLY)

# Default configuration, picked up next to the executable.
# (with the audio module this build has).
configure_file(sandbox.ini ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/sandbox.ini @ONLY)

if(PHI_BUILD_TESTS)
    # Smoke run: five seconds of the demo (throws, impacts, sparks, trails,
    # sounds) headless, with the null device so it runs anywhere, sanitizers
    # included. It passes on the demo's closing line, which counts at least
    # ten throws and the save and load done halfway, and which a crash
    # never reaches. A pass expression makes
    # ctest ignore the exit code, and leaks are reported after that line, so
    # sanitizer reports fail it explicitly.
    add_test(NAME sandbox.demo
        COMMAND sandbox --headless --frames 300
                --set engine.forced_delta=0.0167
                --set sandbox.demo=true
                --set engine.modules=window_null,rhi_null,renderer_forward,audio_null
                --set log.level=info
        WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    set_tests_properties(sandbox.demo PROPERTIES
        PASS_REGULAR_EXPRESSION "demo: [1-9][0-9]+ throws, 1 reloads"
        FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")
endif()
