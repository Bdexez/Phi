// Benchmark: renders a grid of cubes offscreen and reports where frame time
// goes, to compare engine changes on equal terms.
//
//   ./benchmark                                  10,000 cubes, Vulkan
//   ./benchmark --set bench.entities=50000
//   ./benchmark --modules window_null,rhi_null,renderer_forward   CPU side only
//
// Configuration:
//   bench.entities   cubes (default 10000)
//   bench.materials  distinct materials among them (default 4)
//   bench.warmup     frames run before measuring (default 30)
//   bench.frames     frames measured after the warm-up (default 200)
//
// Rendering features are those of the renderer configuration, so e.g.
// --set renderer.shadows=false measures without shadows.

#include <bdex/app/Application.hpp>
#include <bdex/core/Log.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace {

using namespace bdex;

class Benchmark final : public app::Application {
protected:
    void onConfigure(Config& config) override {
        config.set("engine.modules", "window_null,rhi_vulkan,renderer_forward");
        config.set("renderer.vsync", "false");
        config.set("engine.forced_delta", "0.016");  // same simulated time every run
    }

    Result<> onStart() override {
        const Config& config = engine().config();
        entities_ = static_cast<u32>(std::max<i64>(config.getInt("bench.entities", 10000), 1));
        warmup_ = static_cast<u64>(std::max<i64>(config.getInt("bench.warmup", 30), 0));
        measured_ = static_cast<u64>(std::max<i64>(config.getInt("bench.frames", 200), 1));

        ecs::World& world = scene().world();
        // Square grid in the XY plane, seen from far enough to fit.
        const u32 side = static_cast<u32>(std::ceil(std::sqrt(static_cast<f32>(entities_))));
        const ecs::Entity camera = scene().createEntity("Camera");
        world.emplace<render::Camera>(camera).farPlane = static_cast<f32>(side) * 4.0f;
        world.get<scene::Transform>(camera).position = Vec3(0.0f, 0.0f, static_cast<f32>(side) * 1.2f);
        const ecs::Entity sun = scene().createEntity("Sun");
        world.emplace<render::DirectionalLight>(sun);

        if (!engine().services().has<render::IRenderer>()) {
            return {};  // simulation only
        }
        const render::MeshHandle cube = renderer().createMesh(assets::makeCube(0.8f));
        std::vector<render::MaterialHandle> materials;
        const u32 materialCount = static_cast<u32>(std::max<i64>(engine().config().getInt("bench.materials", 4), 1));
        for (u32 i = 0; i < materialCount; ++i) {
            render::MaterialDesc desc;
            desc.baseColor = Vec4(static_cast<f32>(i + 1) / static_cast<f32>(materialCount), 0.5f, 0.7f, 1.0f);
            materials.push_back(renderer().createMaterial(desc));
        }
        for (u32 i = 0; i < entities_; ++i) {
            const ecs::Entity entity = scene().createEntity("cube");
            world.get<scene::Transform>(entity).position =
                Vec3(static_cast<f32>(i % side) - static_cast<f32>(side) * 0.5f,
                     static_cast<f32>(i / side) - static_cast<f32>(side) * 0.5f, 0.0f);
            world.emplace<render::MeshRenderer>(entity, cube, materials[i % materials.size()]);
        }
        return {};
    }

    void onUpdate(const FrameTime& time) override {
        if (time.frame == warmup_) {
            engine().scheduler().resetStats();  // measure from here
        }
        if (time.frame + 1 == warmup_ + measured_) {
            engine().requestExit();  // after this frame
        }
    }

    void onStop() override {
        log::info("bench", "{} entities, {} frames measured after {} of warm-up", entities_, measured_, warmup_);
        f64 total = 0.0;
        for (const TaskStats& task : engine().scheduler().taskStats()) {
            log::info("bench", "  {:<32} {:>8.3f} ms", task.name, task.averageMs);
            total += task.averageMs * static_cast<f64>(task.runs) / static_cast<f64>(measured_);
        }
        log::info("bench", "  {:<32} {:>8.3f} ms per frame (CPU)", "total", total);
        if (const auto* device = engine().services().tryGet<rhi::IDevice>()) {
            const rhi::FrameStats stats = device->lastFrameStats();
            log::info("bench", "  last frame: {} draws, {} instances, {} triangles", stats.drawCalls,
                      stats.instances, stats.triangles);
        }
    }

private:
    u32 entities_ = 0;
    u64 warmup_ = 0;
    u64 measured_ = 0;
};

}  // namespace

BDEX_MAIN(Benchmark)
