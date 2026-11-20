find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(WARNING "bdex: GoogleTest not found, tests are disabled")
    return()
endif()
include(GoogleTest)

# A minimal module used to exercise the module system end to end (as a real
# plugin in shared builds, statically registered otherwise).
bdex_add_module(test_plugin
    SOURCES plugins/TestPlugin.cpp
    DEPS bdex::core)
target_include_directories(bdex_module_test_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(bdex_tests
    animation/AnimationTests.cpp
    app/ApplicationTests.cpp
    core/ConfigTests.cpp
    core/JsonTests.cpp
    core/EngineTests.cpp
    core/EventBusTests.cpp
    core/FileSystemTests.cpp
    core/FileWatcherTests.cpp
    core/HandleTests.cpp
    core/JobSystemTests.cpp
    core/LogTests.cpp
    core/ModuleManagerTests.cpp
    core/SchedulerTests.cpp
    core/ServiceRegistryTests.cpp
    core/TraceTests.cpp
    core/TypeIdTests.cpp
    ecs/WorldTests.cpp
    particles/ParticleTests.cpp
    physics/PhysicsTests.cpp
    platform/InputTests.cpp
    assets/AssetTests.cpp
    assets/AudioTests.cpp
    assets/GltfTests.cpp
    assets/PngTests.cpp
    assets/ZlibTests.cpp
    audio/AudioComponentsTests.cpp
    audio/MixerTests.cpp
    rhi/RhiTests.cpp
    scene/SceneTests.cpp
    ui/UiTests.cpp
    robustness/MutationTests.cpp
    integration/HeadlessEngineTests.cpp)
target_include_directories(bdex_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bdex_tests PRIVATE
    bdex::core
    bdex::ecs
    bdex::platform
    bdex::rhi
    bdex::assets
    bdex::audio
    bdex::animation
    bdex::particles
    bdex::ui
    bdex::scene
    bdex::render
    bdex::app
    bdex::physics
    GTest::gtest
    GTest::gtest_main)
bdex_configure_target(bdex_tests)
bdex_target_modules(bdex_tests test_plugin window_null rhi_null rhi_vulkan renderer_forward audio_null audio_openal)

# A test that deadlocks (the job system's, say) fails after two minutes
# rather than ctest's default 25: the slowest takes about 6 s under TSan.
gtest_discover_tests(bdex_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    DISCOVERY_TIMEOUT 30
    PROPERTIES TIMEOUT 120)
