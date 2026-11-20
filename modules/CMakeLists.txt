# Modules are implementations of engine interfaces. They only link against
# engine libraries - never against each other - and are selected at runtime
# through the `engine.modules` configuration key.

if(BDEX_MODULE_WINDOW_NULL)
    add_subdirectory(window_null)
endif()

if(BDEX_MODULE_WINDOW_GLFW)
    add_subdirectory(window_glfw)
endif()

if(BDEX_MODULE_RHI_NULL)
    add_subdirectory(rhi_null)
endif()

if(BDEX_MODULE_RHI_VULKAN)
    add_subdirectory(rhi_vulkan)
endif()

if(BDEX_MODULE_RENDERER_FORWARD)
    add_subdirectory(renderer_forward)
endif()

if(BDEX_MODULE_AUDIO_NULL)
    add_subdirectory(audio_null)
endif()

if(BDEX_MODULE_AUDIO_OPENAL AND OpenAL_FOUND)
    add_subdirectory(audio_openal)
endif()
