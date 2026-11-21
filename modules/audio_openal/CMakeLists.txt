phi_add_module(audio_openal
    SOURCES
        src/OpenAlAudioModule.cpp
    DEPS
        phi::core
        phi::audio
        OpenAL::OpenAL)
