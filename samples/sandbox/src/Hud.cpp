#include "Hud.hpp"

#include <bdex/audio/Mixer.hpp>
#include <bdex/render/DebugDraw.hpp>
#include <bdex/rhi/Device.hpp>

#include <format>
#include <string_view>
#include <utility>

namespace sandbox {

using namespace bdex;

void Hud::update(Engine& engine, const FrameTime& time, usize entityCount, const platform::InputActions* actions,
                 platform::IWindow* window) {
    ++frames_;
    seconds_ += time.unscaledDelta;  // frames per real second, paused or not
    if (seconds_ >= 0.5) {
        const f64 fps = frames_ / seconds_;
        if (window != nullptr) {
            window->setTitle(std::format("bdex sandbox - {:.0f} fps ({:.2f} ms)", fps, 1000.0 / fps));
        }
        f64 gpu = 0.0;
        if (const auto* device = engine.services().tryGet<rhi::IDevice>()) {
            for (const rhi::GpuTiming& timing : device->lastGpuTimings()) {
                if (timing.depth == 0) {
                    gpu = timing.milliseconds;
                }
            }
        }
        const auto* mixer = engine.services().tryGet<audio::Mixer>();
        stats_ = std::format("{:.0f} FPS  {:.2f} MS\nGPU {:.2f} MS\n{} ENTITIES  {} VOICES", fps, 1000.0 / fps, gpu,
                             entityCount, mixer != nullptr ? mixer->voiceCount() : 0);
        frames_ = 0;
        seconds_ = 0.0;
    }

    auto* debug = engine.services().tryGet<render::DebugDraw>();
    if (debug == nullptr) {
        return;
    }
    // A shadow one pixel off keeps the text readable on any background.
    const auto write = [&](const Vec2& at, std::string_view text, const Vec4& color) {
        debug->screenText(at + Vec2(1.0f), text, 12.0f, Vec4(0.0f, 0.0f, 0.0f, 0.8f));
        debug->screenText(at, text, 12.0f, color);
    };
    if (showStats_ && !stats_.empty()) {
        write(Vec2(12.0f), stats_ + (showHelp_ ? "" : "\nF1 HELP"), Vec4(1.0f, 1.0f, 0.6f, 1.0f));
    }
    if (showHelp_) {
        write(Vec2(12.0f, 100.0f), helpText(actions), Vec4(0.75f, 0.95f, 1.0f, 1.0f));
    }
}

std::string Hud::helpText(const platform::InputActions* actions) {
    static constexpr std::pair<const char*, const char*> kActions[] = {
        {"move_forward", "forward"},    {"move_back", "back"},              {"move_left", "left"},
        {"move_right", "right"},        {"move_up", "up / jump"},           {"move_down", "down"},
        {"move_fast", "faster"},        {"look", "look around"},            {"select", "select"},
        {"throw_ball", "throw a ball"}, {"throw_crate", "throw a crate"},   {"walk", "walk / fly"},
        {"flashlight", "flashlight"},   {"duplicate", "duplicate"},         {"delete", "delete"},
        {"debug_view", "gizmos"},       {"save_scene", "save scene"},       {"load_scene", "load scene"},
        {"screenshot", "screenshot"},   {"profile", "log profile"},         {"reload_shaders", "reload shaders"},
        {"stats", "stats"},             {"toggle_fullscreen", "fullscreen"}, {"help", "this help"},       {"panel", "settings panel"},
        {"quit", "quit"},
    };
    std::string text;
    for (const auto& [action, description] : kActions) {
        std::string keys;
        if (actions != nullptr) {
            for (const std::string& binding : actions->bindings(action)) {
                keys += (keys.empty() ? "" : ", ") + binding;
            }
        }
        text += std::format("{:<16}{}\n", description, keys.empty() ? "-" : keys);
    }
    return text;
}

}  // namespace sandbox
