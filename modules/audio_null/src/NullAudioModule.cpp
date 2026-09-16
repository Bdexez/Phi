// Audio without a device: the mixer runs in real time and its output is
// discarded, so sounds start, play for their length and finish exactly as
// they would if heard. Used headless and in tests.
#include <bdex/audio/Mixer.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/core/Module.hpp>

#include <algorithm>
#include <memory>

namespace bdex {
namespace {

class NullAudioModule final : public IModule {
public:
    [[nodiscard]] ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,
            .description = "Silent audio output",
            .provides = {"audio"},
            .dependencies = {},
            .after = {},
        };
    }

    Result<> onInit(Engine& engine) override {
        const auto rate = static_cast<u32>(std::clamp<i64>(engine.config().getInt("audio.sample_rate", 48000), 8000, 192000));
        mixer_ = std::make_shared<audio::Mixer>(rate);
        engine.services().provide<audio::Mixer>(mixer_);
        task_ = ScopedTask(engine.scheduler(),
                           engine.scheduler().add(Phase::PostUpdate, "audio_null.advance",
                                                  [mixer = mixer_.get()](const FrameTime& time) {
                                                      mixer->advance(time.delta);
                                                  }));
        return {};
    }

    void onShutdown(Engine& engine) override {
        task_.reset();
        engine.services().remove<audio::Mixer>();
        mixer_.reset();
    }

private:
    std::shared_ptr<audio::Mixer> mixer_;
    ScopedTask task_;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::NullAudioModule)
