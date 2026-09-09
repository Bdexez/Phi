# Architecture

This document explains how the engine is put together and how to extend it. The [README](../README.md) covers building and running.

## The idea in one paragraph

The engine is a small **kernel** plus **modules**. The kernel knows nothing about windows, GPUs, or scenes: it only offers four mechanisms (modules, services, events, scheduled tasks) and a loop that runs the phases of a frame. Everything visible - the window, the Vulkan device, the renderer - is a module that implements an interface declared in an engine library. Modules never link against each other; they meet through interfaces registered in the kernel. Replacing a backend is therefore a configuration change, not a code change.

```text
                 ┌──────────────────────────────────────────────┐
  application    │ sandbox (bdex::app::Application)             │
                 └──────────────────────┬───────────────────────┘
                                        │ uses interfaces only
                 ┌──────────────────────▼───────────────────────┐
  engine         │ app                                          │
  libraries      │ render    scene    assets                    │
  (interfaces    │ rhi       platform ecs                       │
   + shared      │ core  ← kernel: modules, services, events,   │
   logic)        │         scheduler, config, jobs, filesystem  │
                 └──────────────────────▲───────────────────────┘
                                        │ implement interfaces
                 ┌──────────────────────┴───────────────────────┐
  modules        │ window_glfw   rhi_vulkan   renderer_forward  │
  (plugins)      │ window_null   rhi_null                       │
                 └──────────────────────────────────────────────┘
```

## Engine libraries

Each library is one CMake target (`bdex::<name>`) with its public headers under `include/bdex/<name>/`. A library only depends on the ones listed for it; there are no cycles.

| Library | Contents | Depends on |
| --- | --- | --- |
| `core` | `Engine`, `ModuleManager`, `ServiceRegistry`, `EventBus`, `Scheduler`, `Config`, `JobSystem`, `VirtualFileSystem`, handles, logging, math aliases | GLM |
| `ecs` | `World`, sparse-set component pools, views | `core` |
| `platform` | `IWindowSystem`, `IWindow`, input events, `InputState` | `core` |
| `rhi` | `IDevice`, `ICommandList`, resource descriptions | `core`, `platform` |
| `assets` | `MeshData`, `ImageData`, loaders, `AssetManager`, procedural primitives | `core` |
| `scene` | `Scene`, `SceneManager`, `Transform`, hierarchy | `core`, `ecs` |
| `render` | `IRenderer`, `IRenderFeature`, render components, shared GLSL headers | `core`, `ecs`, `rhi`, `assets`, `scene` |
| `app` | `Application`: configuration, services, module loading, main loop | all of the above |

## The kernel

`bdex::Engine` (in `core`) owns the four mechanisms modules are built from.

### Modules

A module implements `IModule`: a description (`info()`) and two lifecycle hooks (`onInit`, `onShutdown`). `ModuleManager::load(name)` finds a module by name, either in the static registry or as a plugin `bdex_module_<name>` next to the executable; application code is identical in both cases.

Initialization order is computed from what modules declare, not from the order in which they are listed. Dependencies are expressed on **capabilities** rather than module names:

| Field | Meaning |
| --- | --- |
| `provides` | Capabilities the module offers, e.g. `"rhi"`. A module always provides its own name. |
| `dependencies` | Capabilities that must be provided by another loaded module. Missing one is a startup error. |
| `after` | Capabilities to be initialized after *if present*. Not an error when absent. |

`renderer_forward` depends on `"rhi"`, which `rhi_vulkan` and `rhi_null` both provide. `rhi_vulkan` declares `after = {"window"}`: with a window module it initializes second (it needs the window system's instance extensions), without one it simply renders offscreen. Cycles and missing capabilities are reported with the names involved. Shutdown runs in reverse order.

### Services

`ServiceRegistry` maps an **interface type** to an implementation:

```cpp
engine.services().provide<rhi::IDevice>(device);         // in rhi_vulkan
rhi::IDevice& device = engine.services().get<rhi::IDevice>();  // anywhere else
```

Consumers see the interface only. `get` aborts with the missing interface's name; `tryGet` returns null for optional services.

### Events

`EventBus` delivers plain structs to subscribers. `subscribe` returns an RAII `Subscription`; `publish` dispatches immediately, `enqueue` defers until the middle of the frame. The window module publishes `KeyEvent`, `WindowResized`...; `InputState` and the application subscribe without knowing which module emitted them.

### Scheduled tasks

The main loop does nothing but run phases. Behaviour is added as tasks:

```cpp
engine.scheduler().add(Phase::Update, "game.ai", [](const FrameTime& time) { ... });
```

| Phase | Typical use | Registered by |
| --- | --- | --- |
| `BeginFrame` | | |
| `Input` | Reset input transitions, poll OS events | window module |
| `PreUpdate` | | |
| `FixedUpdate` | Runs 0..n times with a constant delta (`engine.fixed_timestep`) | application |
| `Update` | Gameplay | application |
| `PostUpdate` | Derived data; world transforms run here with order 1000 | `app` |
| *(queued events are dispatched)* | | |
| `PreRender` | | |
| `Render` | Draw the active scene | renderer module |
| `PostRender` | | |
| `EndFrame` | | |

Within a phase, tasks run by ascending `order`, then by registration order.

`FixedUpdate` has its own clock: each step receives `delta = engine.fixed_timestep` and an `elapsed` that advances by exactly one step, so a simulation driven from it is deterministic. At most `engine.max_fixed_steps` steps (8 by default) run per frame; beyond that the backlog is dropped. The other phases receive `FrameTime::fixedAlpha`, the fraction of a step accumulated but not simulated yet, to interpolate between the last two fixed-step states when rendering.

### Identity across plugin boundaries

Services, events, and ECS components are keyed by `bdex::typeId<T>()`, a hash of the type's **name** computed at compile time. The same type therefore has the same id in the executable and in every plugin, without relying on RTTI or on static variables being shared between shared libraries. A plugin also exports its ABI version (`kModuleAbiVersion`); a mismatch is rejected at load time.

## Writing a module

A module is a directory under `modules/` with one `CMakeLists.txt` and its sources. The example below adds a module providing a new capability.

**1. Declare the interface** in an engine library (or reuse an existing one), so that consumers can include it without depending on the module:

```cpp
// engine/audio/include/bdex/audio/Audio.hpp
#include <string_view>

namespace bdex::audio {
class IAudio {
public:
    virtual ~IAudio() = default;
    virtual void play(std::string_view sound) = 0;
};
}
```

**2. Implement the module:**

```cpp
// modules/audio_null/src/AudioNullModule.cpp
#include <bdex/audio/Audio.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/core/Module.hpp>

#include <memory>

namespace bdex {
namespace {

class NullAudio final : public audio::IAudio {
public:
    void play(std::string_view sound) override { log::debug("audio", "play {}", sound); }
};

class AudioNullModule final : public IModule {
public:
    ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,   // injected by bdex_add_module()
            .description = "Silent audio backend",
            .provides = {"audio"},
            .dependencies = {},
            .after = {},
        };
    }

    Result<> onInit(Engine& engine) override {
        if (!engine.config().getBool("audio.enabled", true)) {
            return fail("audio is disabled");   // startup stops with this message
        }
        audio_ = std::make_shared<NullAudio>();
        engine.services().provide<audio::IAudio>(audio_);
        return {};
    }

    void onShutdown(Engine& engine) override {
        engine.services().remove<audio::IAudio>();
        audio_.reset();
    }

private:
    std::shared_ptr<NullAudio> audio_;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::AudioNullModule)
```

**3. Build it:**

```cmake
# modules/audio_null/CMakeLists.txt
bdex_add_module(audio_null
    SOURCES src/AudioNullModule.cpp
    DEPS bdex::core bdex::audio)
```

Add `add_subdirectory(audio_null)` to `modules/CMakeLists.txt`.

**4. Make it available to an application and load it:**

```cmake
bdex_target_modules(sandbox window_glfw rhi_vulkan renderer_forward audio_null)
```

```ini
[engine]
modules = window_glfw, rhi_vulkan, renderer_forward, audio_null
```

`bdex_target_modules` adds a build dependency in plugin builds, and links and registers the module in static builds. The executable itself needs no change.

### Rules for modules

- Link against engine libraries only, never against another module.
- Talk to the rest of the engine through `Engine`: services, events, tasks, configuration.
- Release in `onShutdown` everything registered in `onInit` (services, tasks, subscriptions). `ScopedTask` and `Subscription` make this automatic.
- Read settings from your own configuration section and document them at the top of the module's source file.
- Recoverable failures return `Result` with a message; `BDEX_ASSERT` is for programming errors.

[`tests/plugins/TestPlugin.cpp`](../tests/plugins/TestPlugin.cpp) is a complete minimal module, and [`modules/rhi_null`](../modules/rhi_null/src/NullRhiModule.cpp) is the smallest real backend.

## Entities and components

`ecs::World` stores components in sparse sets keyed by `typeId`, so any movable type is a component and plugins share pools with the executable. Systems are functions over views:

```cpp
world.view<Transform, const Velocity>().each([&](Transform& t, const Velocity& v) { t.position += v.value * dt; });
world.view<Transform>().exclude<Parent>().each(...);              // roots only
world.view<Transform, const Spin>().parallelEach(jobs, spinFn);   // on the job system
```

- A `const` component type is handed to the callback as a const reference, which documents what a system only reads.
- `parallelEach` splits the view across the `JobSystem`; its callback may only touch the components it is given.
- `onAdd<T>` / `onRemove<T>` observe components coming and going (including through `destroy` and `clear`), for indexes or resources that follow components.
- `clone(entity)` creates an entity with a copy of every copyable component, firing `onAdd` hooks; move-only components are left out, and a type whose copy constructor cannot compile (holding a `std::vector<std::unique_ptr<...>>`, say) opts out by specializing `ecs::Cloneable<T>` as `std::false_type`.
- `scene::updateWorldTransforms(world, jobs)` derives `WorldTransform` from `Transform` and `Parent` once per frame in O(n), computing roots in parallel. `scene::setParent` re-parents an entity (refusing cycles, optionally keeping its world pose), `scene::children` lists a node's children, `scene::cloneHierarchy` duplicates a whole branch (the copy beside the original, its children linked to each other), and `scene::destroyHierarchy` removes one.

## Physics

`physics::step(world, dt)` advances every `RigidBody` (velocity, mass, restitution, friction): gravity with semi-implicit Euler, then collisions of dynamic spheres (`SphereCollider`) with static boxes oriented by their `Transform` (`BoxCollider` without a `RigidBody`) and with each other, found by sort and sweep and resolved with impulses. Spheres moving more than half their radius in a step are swept against boxes first, so fast ones cannot pass through thin walls between steps. Bodies use their entity's `Transform` as a world pose, so they have no `Parent`. Friction at a contact also spins spheres (`angularVelocity`, solid-sphere inertia), so a sliding ball ends up rolling; `linearDamping` and `angularDamping` act as drag, the latter slowing rolling balls to rest; bodies that stay slower than `Settings::sleepSpeed` (config `physics.sleep_speed`, 0 disables) for half a second fall asleep, skipping integration and box tests and acting as static to others until hit faster than resting contact, given a velocity, or `physics::wake`d; `physics::interpolate` blends rotations as well as positions. `Application` runs it in `FixedUpdate`, after `onFixedUpdate`, and draws bodies between their last two steps with `physics::interpolate(world, fixedAlpha)`, so motion is smooth at any frame rate; `physics.gravity` sets the gravity. A step is deterministic for a given world. `physics::raycast` returns the first collider a ray touches, with the hit point and surface normal, for shooting, line of sight, or ground checks. Each resolved collision harder than `physics.collision_impulse` (0.5 N·s by default, which leaves resting contacts out) is published as a `physics::Collision` event, for impact sounds or damage. A `physics::Trigger` turns a static collider into a zone that bodies pass through; entering and leaving it are published as `physics::TriggerEvent`.

## Input

Window modules publish keyboard, mouse, and gamepad events; `platform::InputState` turns them into polled state with per-frame transitions, and releases everything when the window loses focus. `platform::InputActions` maps named actions to keys, mouse buttons, gamepad buttons, and stick directions loaded from the `[input]` configuration section, with `value()` for analog strength past a dead zone. `Application` provides it as a service.

## Tools

- `render::DebugDraw` (service of the forward renderer): lines, boxes, spheres, axes, and frusta drawn for one frame.
- `render::rayThroughPixel` and `render::intersect`, with `IRenderer::meshBounds`, for picking.
- `Scheduler::taskStats()`: CPU time of every task; the `benchmark` sample prints it for a reproducible scene.
- `IRenderer::reloadShaders()`: rebuilds pipelines from the shader files on disk.
- `log.file`: a copy of the log on disk.

## Assets

`AssetManager` loads files through the virtual file system and caches them by type and virtual path. Loaders are registered per type and extension (case-insensitive), so a module or an application can add a format without touching the engine:

```cpp
auto mesh = assets.load<assets::MeshData>("app:/meshes/ship.obj");      // blocks
auto sky = assets.loadAsync<assets::ImageData>("app:/textures/sky.png");  // returns at once
// ... later, e.g. in a following frame:
if (sky.ready()) { auto image = sky.get(); }
```

`loadAsync` reads and decodes on the `JobSystem` service. It is thread-safe: a request for an asset already being loaded joins that load, a failed load is retried on the next request, and the manager waits for loads still in flight when it is destroyed. Built-in loaders handle OBJ, PNG, TGA, and SPIR-V.

## Scene files

`scene::ComponentSerializers` saves a world's entities as text and loads them back. The format is the engine's INI dialect, one section per entity:

```ini
[entity.0]
Name.value = Player
Transform.position = 0 1 0
Transform.rotation = 1 0 0 0
Parent.entity = 3
WorldTransform =
```

`withSceneComponents()` knows `Name`, `Transform`, `Parent`, and `WorldTransform`; `render::addComponentSerializers` adds cameras and lights. Applications register their own components with `add<T>(name, save, load)`, using `formatField` / `parseField` for values. Entity references are renumbered on save and resolved on load; loading adds to the world and removes what it created if any value is malformed. `MeshRenderer` is not serialized: its handles belong to the renderer, so applications store their own asset reference and rebuild it.

## Rendering

### RHI

`rhi::IDevice` is a small, explicit description of a GPU: buffers, textures, samplers, shaders, bind groups (descriptor sets), graphics pipelines, and swapchains, all referred to by typed generational handles. A renderer is written against it once and runs on any backend module.

Frame protocol:

```cpp
rhi::ICommandList& commands = device.beginFrame();
rhi::TextureHandle target = device.acquireSwapchainTexture(swapchain);
if (target) {                       // invalid while minimized or resizing
    commands.beginRendering({colorAttachments, &depthAttachment});
    ...
    commands.endRendering();
}
device.endFrame();                  // submits and presents
```

What the backend takes care of, so that renderers do not have to:

- **Resource states.** Image layouts are tracked per texture; attachments, sampled textures, and presented images are transitioned automatically.
- **Lifetimes.** `destroy*` is always safe: the object is released once no in-flight frame can reference it.
- **Resizing.** The swapchain follows its window and is rebuilt when out of date.
- **GPU timings.** Each `beginDebugGroup` / `endDebugGroup` pair is also timed on the GPU with timestamp queries, read back without stalling once the frame's slot comes around again; `lastGpuTimings()` lists them, nested under a `frame` entry, for the last frame the GPU finished (empty where the queue has no timestamps). The forward renderer's groups span whole passes (`shadows`, `scene` with its features inside, `bloom`, `tonemap`, `fxaa`), so they add up to the frame.

The Vulkan backend uses Vulkan 1.3 dynamic rendering (no render pass or framebuffer objects), synchronization2, and Vulkan Memory Allocator. Clip space follows Vulkan conventions (Y down, depth in `[0, 1]`); `render::makeCameraData` flips the projection so the world stays Y-up with counter-clockwise front faces.

The window backend, not the RHI, creates the presentation surface (`IWindow::createVulkanSurface`), because it is the only component that knows the native windowing API. `rhi_vulkan` therefore has no dependency on GLFW, X11, or Wayland.

### Renderer and features

`render::IRenderer` owns meshes, textures, and materials (returned as handles that ECS components store) and draws the active scene during `Phase::Render`. `renderer_forward` renders one color + depth pass; **what** is drawn in that pass is decided by render features, sorted by `order()`:

| Order | Feature | Defined in |
| --- | --- | --- |
| -100 | `sandbox.sky` | the sandbox application |
| 0 | `forward.meshes` | `renderer_forward` |

A feature implements `IRenderFeature` (`onAttach`, `render`, `onDetach`) and is added with `IRenderer::addFeature`. It draws into the color format given in `RenderSetupContext`: a 16-bit float HDR target by default, tone mapped afterwards, so shaders output linear light that may exceed 1. Every feature receives the per-frame constants and the sun's shadow map as bind group 0; shaders get the matching declarations with `#include "bdex/frame_uniforms.glsl"`, and `#include "bdex/shadows.glsl"` provides `bdexSunShadow()` to receive shadows. To cast them, a feature overrides `renderShadows`, called inside the depth-only shadow pass before `render`, once per cascade with the cascade's index in the context.

Without a presentable window the renderer draws into an offscreen target; screenshots read that target back.

## Build system

`cmake/BdexEngine.cmake` provides the building blocks:

| Function | Purpose |
| --- | --- |
| `bdex_add_library(name ...)` | Engine library `bdex::<name>`. |
| `bdex_add_module(name ...)` | Module `bdex_module_<name>`: plugin or static library depending on `BDEX_SHARED_MODULES`. |
| `bdex_target_modules(target modules...)` | Makes modules available to an executable (build dependency, or link + static registration). |
| `bdex_compile_shaders(target ...)` | GLSL to SPIR-V with `glslc`, with `#include` dependency tracking. |

Runtime layout, relative to the executable: plugins sit next to it, `assets/engine/` is mounted as `engine:/`, and `assets/<app.name>/` as `app:/`.

## Current limits

Known gaps, listed so they are not mistaken for design decisions:

- One window; `IWindowSystem` exposes a primary window only.
- The forward renderer has a shadow pass, a color pass, and a post pass with bloom, exposure, tone mapping, and FXAA (no temporal or multisample anti-aliasing). Only the directional light casts shadows, from four cascades covering `renderer.shadow_distance` in front of the camera, without blending between cascades. Culling is per object against the view frustum, and transparent objects are sorted per object, not per triangle.
- The RHI is main-thread only.
- No audio or scripting modules yet; physics covers dynamic spheres against spheres and oriented static boxes; dynamic boxes are not simulated.
- Only built and run on Linux (GCC and Clang). Windows and macOS code paths exist (library loading, executable path, export macros) but are untested.
