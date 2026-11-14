cmake_minimum_required(VERSION 3.25)

project(PhiEngine
    VERSION 0.1.0
    DESCRIPTION "Modular Vulkan game engine"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PHI_SHARED_MODULES "Build engine libraries as shared objects and modules as runtime-loaded plugins" ON)
option(PHI_BUILD_TESTS "Build the unit and integration tests" ON)
option(PHI_BUILD_SAMPLES "Build the sample applications" ON)
option(PHI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Every module can be switched off individually: nothing in the engine links
# against a module, so removing one never breaks the build of the others.
option(PHI_MODULE_WINDOW_GLFW "Build the GLFW window module" ON)
option(PHI_MODULE_WINDOW_NULL "Build the headless window module" ON)
option(PHI_MODULE_RHI_VULKAN "Build the Vulkan RHI module" ON)
option(PHI_MODULE_RHI_NULL "Build the no-op RHI module" ON)
option(PHI_MODULE_RENDERER_FORWARD "Build the forward renderer module" ON)
option(PHI_MODULE_AUDIO_NULL "Build the silent audio module" ON)
option(PHI_MODULE_AUDIO_OPENAL "Build the OpenAL audio module (skipped if OpenAL is not found)" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

# Executables, shared libraries and plugins all land in the same directory so
# the module loader can find plugins next to the executable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(PHI_ASSET_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/assets)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(PhiEngine)

find_package(glm REQUIRED)

# Audio output is optional: without OpenAL the engine builds and runs with
# silent audio. PHI_AUDIO_MODULE is the module applications load by default.
set(PHI_AUDIO_MODULE "")
if(PHI_MODULE_AUDIO_NULL)
    set(PHI_AUDIO_MODULE audio_null)
endif()
if(PHI_MODULE_AUDIO_OPENAL)
    find_package(OpenAL QUIET)
    if(OpenAL_FOUND)
        set(PHI_AUDIO_MODULE audio_openal)
    else()
        message(STATUS "phi: OpenAL not found, the audio_openal module is not built (sound will be silent)")
    endif()
endif()

# Before the samples, which register smoke runs of their own.
if(PHI_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(engine)
add_subdirectory(modules)

if(PHI_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(PHI_BUILD_TESTS)
    add_subdirectory(tests)
endif()
