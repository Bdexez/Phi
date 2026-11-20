// Audio output through OpenAL (OpenAL Soft on Linux): the engine mixes in
// software (audio::Mixer) and this module only streams the mix to the
// device, from its own thread, through a small queue of buffers on one
// OpenAL source. Without a usable device it falls back to silent playback,
// so a machine without sound still runs the game.
#include <bdex/audio/Mixer.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/core/Module.hpp>

#include <AL/al.h>
#include <AL/alc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

namespace bdex {
namespace {

class OpenAlAudioModule final : public IModule {
public:
    [[nodiscard]] ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,
            .description = "Audio output through OpenAL",
            .provides = {"audio"},
            .dependencies = {},
            .after = {},
        };
    }

    Result<> onInit(Engine& engine) override {
        const Config& config = engine.config();
        const auto rate = static_cast<u32>(std::clamp<i64>(config.getInt("audio.sample_rate", 48000), 8000, 192000));
        bufferFrames_ = static_cast<usize>(std::clamp<i64>(config.getInt("audio.buffer_frames", 512), 64, 16384));
        mixer_ = std::make_shared<audio::Mixer>(rate);
        engine.services().provide<audio::Mixer>(mixer_);

        if (const auto opened = open(config.getString("audio.device"), rate); !opened) {
            log::warn("openal", "{}; sounds will play silently", opened.error().message);
            close();
            silentTask_ = ScopedTask(engine.scheduler(),
                                     engine.scheduler().add(Phase::PostUpdate, "audio_openal.silent",
                                                            [mixer = mixer_.get()](const FrameTime& time) {
                                                                mixer->advance(time.delta);
                                                            }));
            return {};
        }
        const char* name = alcGetString(device_, ALC_ALL_DEVICES_SPECIFIER);
        log::info("openal", "playing on '{}' at {} Hz, {} frames per buffer", name != nullptr ? name : "?", rate,
                  bufferFrames_);
        thread_ = std::jthread([this, rate](std::stop_token stop) { stream(stop, rate); });
        return {};
    }

    void onShutdown(Engine& engine) override {
        silentTask_.reset();
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        close();
        engine.services().remove<audio::Mixer>();
        mixer_.reset();
    }

private:
    static constexpr usize kBuffers = 4;

    Result<> open(const std::string& deviceName, u32 rate) {
        device_ = alcOpenDevice(deviceName.empty() ? nullptr : deviceName.c_str());
        if (device_ == nullptr) {
            return fail("cannot open audio device '{}'", deviceName.empty() ? "default" : deviceName);
        }
        const std::array<ALCint, 3> attributes = {ALC_FREQUENCY, static_cast<ALCint>(rate), 0};
        context_ = alcCreateContext(device_, attributes.data());
        if (context_ == nullptr || alcMakeContextCurrent(context_) != ALC_TRUE) {
            return fail("cannot create an OpenAL context");
        }
        alGenSources(1, &source_);
        alGenBuffers(static_cast<ALsizei>(buffers_.size()), buffers_.data());
        if (const ALenum error = alGetError(); error != AL_NO_ERROR) {
            return fail("cannot create OpenAL buffers (error {:#x})", error);
        }
        // The mix is already positioned: the source plays it as is.
        alSourcei(source_, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(source_, AL_POSITION, 0.0f, 0.0f, 0.0f);
        return {};
    }

    void close() {
        if (source_ != 0) {
            alSourceStop(source_);
            alSourcei(source_, AL_BUFFER, 0);
            alDeleteSources(1, &source_);
            source_ = 0;
        }
        if (buffers_[0] != 0) {
            alDeleteBuffers(static_cast<ALsizei>(buffers_.size()), buffers_.data());
            buffers_.fill(0);
        }
        if (context_ != nullptr) {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context_);
            context_ = nullptr;
        }
        if (device_ != nullptr) {
            alcCloseDevice(device_);
            device_ = nullptr;
        }
    }

    /// Mixes the next buffer and hands it to OpenAL, as 16-bit stereo.
    void fill(ALuint buffer, u32 rate) {
        mixed_.resize(bufferFrames_ * 2);
        pcm_.resize(mixed_.size());
        mixer_->mix(mixed_);
        for (usize i = 0; i < mixed_.size(); ++i) {
            pcm_[i] = static_cast<i16>(std::lround(mixed_[i] * 32767.0f));
        }
        alBufferData(buffer, AL_FORMAT_STEREO16, pcm_.data(), static_cast<ALsizei>(pcm_.size() * sizeof(i16)),
                     static_cast<ALsizei>(rate));
        alSourceQueueBuffers(source_, 1, &buffer);
    }

    /// The streaming thread: refills each buffer as soon as OpenAL is done
    /// with it, restarting the source if it ran dry.
    void stream(const std::stop_token& stop, u32 rate) {
        for (const ALuint buffer : buffers_) {
            fill(buffer, rate);
        }
        alSourcePlay(source_);
        const auto period = std::chrono::microseconds(static_cast<i64>(bufferFrames_ * 1'000'000 / rate / 4));
        while (!stop.stop_requested()) {
            ALint processed = 0;
            alGetSourcei(source_, AL_BUFFERS_PROCESSED, &processed);
            for (; processed > 0; --processed) {
                ALuint buffer = 0;
                alSourceUnqueueBuffers(source_, 1, &buffer);
                fill(buffer, rate);
            }
            ALint state = AL_PLAYING;
            alGetSourcei(source_, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) {
                ++underruns_;
                alSourcePlay(source_);
            }
            std::this_thread::sleep_for(period);
        }
        if (underruns_ > 0) {
            log::warn("openal", "the device ran dry {} times (raise audio.buffer_frames?)", underruns_);
        }
    }

    std::shared_ptr<audio::Mixer> mixer_;
    ScopedTask silentTask_;
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    ALuint source_ = 0;
    std::array<ALuint, kBuffers> buffers_{};
    usize bufferFrames_ = 512;
    std::vector<f32> mixed_;
    std::vector<i16> pcm_;
    u32 underruns_ = 0;
    std::jthread thread_;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::OpenAlAudioModule)
