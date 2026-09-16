bdex_add_module(audio_openal
    SOURCES
        src/OpenAlAudioModule.cpp
    DEPS
        bdex::core
        bdex::audio
        OpenAL::OpenAL)
