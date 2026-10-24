#include "SettingsPanel.hpp"

#include <bdex/animation/Animation.hpp>
#include <bdex/render/DebugDraw.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/scene/Scene.hpp>

#include <cmath>

namespace sandbox {

using namespace bdex;

namespace {

constexpr f32 kWidth = 230.0f;
constexpr f32 kMargin = 12.0f;

}  // namespace

PanelRequests SettingsPanel::draw(Engine& engine, ecs::World& world, const platform::InputState* input,
                                  const PanelControls& controls) {
    auto* debug = engine.services().tryGet<render::DebugDraw>();
    if (!visible_ || debug == nullptr || input == nullptr) {
        return {};
    }
    const auto* renderer = engine.services().tryGet<render::IRenderer>();
    const f32 left = (renderer != nullptr ? static_cast<f32>(renderer->renderExtent().width) : 1280.0f) - kWidth - kMargin;
    ui_.begin(ui::fromInputState(*input), *debug);

    ui_.panel("SUN", Vec2(left, kMargin), kWidth);
    drawSun(world);
    ui_.endPanel();

    PanelRequests requests;
    ui_.panel("SCENE", Vec2(left, 190.0f), kWidth);
    ui_.checkbox("platform moves", controls.platformMoving);
    if (ui_.checkbox("lanterns sway", lanternsSway_)) {
        world.view<animation::AnimationPlayer>().each(
            [&](animation::AnimationPlayer& player) { player.playing = lanternsSway_; });
    }
    ui_.checkbox("sparks", controls.sparks);
    ui_.checkbox("gizmos", controls.gizmos);
    bool timeChanged = ui_.slider("time scale", timeScale_, 0.05f, 2.0f);
    timeChanged |= ui_.checkbox("pause", paused_);  // both drawn every frame
    if (timeChanged) {
        engine.setTimeScale(paused_ ? 0.0 : timeScale_);
    }
    requests.throwCrate = ui_.button("throw a crate");
    requests.throwBall = ui_.button("throw a ball");
    requests.clearThrows = ui_.button("clear throws");
    ui_.endPanel();

    ui_.end();
    return requests;
}

void SettingsPanel::drawSun(ecs::World& world) {
    render::DirectionalLight* sun = nullptr;
    world.view<render::DirectionalLight>().each([&](render::DirectionalLight& light) {
        if (sun == nullptr) {
            sun = &light;
        }
    });
    if (sun == nullptr) {
        ui_.label("no sun");
        return;
    }
    // The angles come from the light itself each frame, so they stay right
    // when a scene is loaded; dragging writes them back.
    const Vec3 toward = -glm::normalize(sun->direction);
    f32 elevation = glm::degrees(std::asin(std::clamp(toward.y, -1.0f, 1.0f)));
    f32 azimuth = glm::degrees(std::atan2(toward.x, toward.z));
    if (azimuth < 0.0f) {
        azimuth += 360.0f;
    }
    bool moved = ui_.slider("height", elevation, 5.0f, 85.0f);
    moved |= ui_.slider("bearing", azimuth, 0.0f, 360.0f);
    if (moved) {
        const f32 up = glm::radians(elevation);
        const f32 around = glm::radians(azimuth);
        sun->direction = -Vec3(std::cos(up) * std::sin(around), std::sin(up), std::cos(up) * std::cos(around));
    }
    ui_.slider("strength", sun->intensity, 0.0f, 3.0f);
    f32 ambient = sun->ambient.b;
    if (ui_.slider("ambient", ambient, 0.0f, 0.6f)) {
        sun->ambient = Vec3(0.64f, 0.76f, 1.0f) * ambient;  // a cool sky tint
    }
}

}  // namespace sandbox
