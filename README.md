# bdex Game Engine

A modular 3D game engine written in **C++23**, featuring a **Vulkan 1.3** backend, an entity component system, and a sandbox for exploring rendering and simulation.

The project separates engine interfaces from their implementations: windowing, graphics backends, and rendering are modules selected at startup. The same application can display a scene in a window, render an image offscreen, or run its simulation without a GPU.

**Project status:** in development, currently version `0.1.0`. The repository includes the engine, its modules, a sample application, and unit and integration tests.

## Features

- **Modular core:** plugin loading, module dependency resolution, a service registry, an event bus, and INI configuration.
- **Simulation loop:** ordered execution phases, fixed timestep updates, and a multithreaded job system.
- **ECS and scenes:** entities with generational handles, application-defined components, typed views, and hierarchical transforms.
- **3D rendering:** perspective cameras, meshes, textures, materials with normal maps, Blinn-Phong lighting from a directional light with soft shadows and up to 16 point lights and 8 spot lights, frustum culling, draw sorting, instancing, blended transparency, and HDR rendering with bloom, ACES tone mapping, and FXAA through an extensible forward renderer.
- **Vulkan backend:** dynamic rendering, synchronization2, memory management with Vulkan Memory Allocator, mipmap generation, anisotropic filtering, and PNG screenshots.
- **Physics:** rigid bodies with gravity, sphere and oriented box colliders, triggers, raycasts, a collide-and-slide character controller, restitution, and friction that sets balls rolling, stepped deterministically in the fixed update.
- **Audio:** a software mixer with any number of voices, resampling, pitch, looping, and 3D panning and attenuation around a listener.
- **Assets:** a virtual file system, asset caching, OBJ, PNG, TGA, WAV, and SPIR-V loaders, plus procedural cubes, planes, spheres, textures, and tones.
- **Headless execution:** offscreen Vulkan rendering or a `null` graphics backend for tests without a GPU.

## Architecture

Libraries in `engine/` expose the APIs. Modules in `modules/` implement them and communicate through engine services. Their initialization order is determined by their declared dependencies.

```text
engine/
├── core/       Modules, services, events, configuration, and jobs
├── ecs/        Entities, components, and views
├── platform/   Window interfaces and input handling
├── rhi/        Render hardware abstraction
├── assets/     Asset loading, caching, and generation
├── scene/      Scenes and transform hierarchies
├── render/     Rendering interfaces, cameras, and graphics components
├── physics/    Rigid bodies and collisions
└── app/        Configuration, initialization, and application loop
modules/        Implementations of engine interfaces
samples/        3D sandbox and sample shaders
tests/          Unit and integration tests
cmake/          Helpers for building libraries, modules, and shaders
```

| Module | Purpose |
| --- | --- |
| `window_glfw` | Interactive window and keyboard/mouse input through GLFW. |
| `window_null` | Virtual window with no display. |
| `rhi_vulkan` | Vulkan 1.3 graphics backend. |
| `rhi_null` | Backend with no GPU execution, used for testing. |
| `renderer_forward` | Scene rendering and support for custom render features. |
| `audio_openal` | Sound output through OpenAL, streamed from the engine's mixer; silent if no device can be opened. Built when OpenAL is found. |
| `audio_null` | Audio mixer without a device: sounds play and finish in real time, silently. |

By default, modules are shared libraries loaded at runtime. Set the CMake option `BDEX_SHARED_MODULES=OFF` to link them statically and register their factories in the executable.

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) describes the kernel, the frame phases, the RHI frame protocol, and how to write a new module.

## Requirements

To build the default configuration:

| Dependency | Purpose |
| --- | --- |
| C++23 compiler and standard library | Including `std::expected` and `std::format`. |
| CMake ≥ 3.25 | Project configuration. |
| Make or Ninja | Building, depending on the selected CMake generator. |
| GLM | Engine mathematics. |
| GLFW ≥ 3.3 | Interactive window module. |
| Vulkan headers and loader library | Building the Vulkan and GLFW modules. |
| `glslc` | Compiling GLSL shaders to SPIR-V. |
| OpenAL (OpenAL Soft), optional | Sound output (`audio_openal`); without it, sound plays silently through `audio_null`. |
| GoogleTest, optional | Building the tests. |

CMake first looks for a system installation of **Vulkan Memory Allocator**. If none is found, it downloads version `3.3.0`, so the initial configuration requires network access in that case.

Vulkan rendering requires a **Vulkan 1.3** compatible driver with dynamic rendering and synchronization2 support. Install `VK_LAYER_KHRONOS_validation` to enable Vulkan validation diagnostics.

## Build and run

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
./build/bin/sandbox
```

The same steps are available as CMake presets (`debug`, `release`, `static`, `asan`), each building into its own `build-<preset>/` directory:

```sh
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

Executables, shared libraries, and modules are placed in `build/bin/`. Shaders are compiled automatically into `build/bin/assets/`, and the sandbox configuration is copied to `build/bin/sandbox.ini`.

### The sandbox

The demo features a textured ground plane, a sphere, animated cubes, and two moons carrying colored point lights, attached to a rotating pivot. A procedural sky demonstrates how applications can add custom render features. The window title displays FPS and average frame time.

| Control | Action |
| --- | --- |
| Hold the right mouse button | Look around. |
| `W` / `A` / `S` / `D` | Move the camera. |
| `Space` / `Left Ctrl` | Move up / down. |
| `Left Shift` | Move faster. |
| `F2` | Save a screenshot as `screenshot_<frame>.png`. |
| `F3` | Log the CPU time of every engine task, the last frame's draw statistics, and the GPU time of each render pass. |
| `F4` | Toggle debug gizmos: the sun's direction, point light ranges, object axes and names, and physics colliders. |
| Left click | Select the object under the cursor (logged and circled). |
| `F5` / `F9` | Save the scene to `sandbox_scene.ini` / load it back. |
| `F6` | Toggle a flashlight (a spot light carried by the camera). |
| `F7` | Reload shaders: edit a shader, rebuild (`cmake --build build`), press `F7`. |
| `F8` | Throw a bouncing ball (rigid-body physics); impacts are heard where they happen, as loud as they are hard. |
| `F10` / `Delete` | Duplicate the selected object, with everything attached to it / delete it. |
| `G` | Walk instead of flying: the camera gets a body that falls, walks on what it stands on (collide and slide), and jumps with `Space`; `G` again to fly. |
| `F12` | Toggle the stats panel: frame rate, GPU time, entity and voice counts. |
| `F11` | Toggle fullscreen (or start with `window.fullscreen = true`). |
| `Escape` | Quit. |

A gamepad works too: the left stick moves, the right stick looks around, the bumpers go up and down, and the right trigger goes faster. Every control is an input action that can be rebound in the `[input]` section of `sandbox.ini` (for example `move_forward = Z, Up` on an AZERTY keyboard), from keys, mouse buttons, gamepad buttons, or stick directions. Applications define their actions the same way and query them through `InputActions`, with `value()` for analog strength.

Screenshots are saved in the current working directory.

### Run without a window

Capture an image using the offscreen Vulkan backend:

```sh
./build/bin/sandbox --headless --frames 3 --screenshot /tmp/bdex-sandbox.png
```

`--headless` replaces the window module with `window_null` (and any audio module with `audio_null`) and keeps the configured graphics backend. With the default configuration, this mode still requires Vulkan.

Run the same application with the `null` backend, without a GPU:

```sh
./build/bin/sandbox --modules window_null,rhi_null,renderer_forward --frames 120
```

To run the simulation alone, load only the virtual window module:

```sh
./build/bin/sandbox --modules window_null --frames 120
```

### The benchmark

`benchmark` renders a grid of cubes offscreen and reports the average CPU time of every engine task, to compare changes on equal terms:

```sh
./build/bin/benchmark                                   # 10,000 cubes on Vulkan
./build/bin/benchmark --set bench.entities=50000 --set renderer.shadows=false
./build/bin/benchmark --modules window_null,rhi_null,renderer_forward   # CPU side only
```

`bench.entities`, `bench.materials`, `bench.warmup`, and `bench.frames` size the run; renderer settings apply as usual.

## Configuration

Configuration is applied in the following order, from lowest to highest priority: engine defaults, application defaults, the INI file, then command-line arguments.

The sandbox uses [sandbox.ini](samples/sandbox/sandbox.ini), which CMake copies next to the executable during configuration, with the audio module the build has:

```ini
[engine]
modules = window_glfw, rhi_vulkan, renderer_forward, audio_openal

[window]
title = bdex sandbox
width = 1280
height = 720

[renderer]
vsync = true
anisotropy = 8
```

Override any key at startup with `--set`:

```sh
./build/bin/sandbox --set window.width=1920 --set window.height=1080 --set renderer.vsync=false
```

With `renderer.vsync=false`, `engine.max_fps` caps the frame rate (for example `--set engine.max_fps=144`) instead of running flat out.

Shadows from the directional light are on by default, in four cascades that keep them sharp near the camera; `renderer.shadows`, `renderer.shadow_map_size` (texels per side of each cascade, default `1024`), and `renderer.shadow_distance` (how far from the camera shadows are drawn, default `100`) tune them.

The scene is rendered in HDR and tone mapped for display: `renderer.exposure` scales the light (default `1`), `renderer.tonemap` picks `aces` (default) or `none` (clamp), `renderer.bloom` / `renderer.bloom_intensity` (default `0.04`) control the glow around bright light, `renderer.antialiasing` picks `fxaa` (default) or `none`, and `renderer.hdr=false` draws straight into the display target instead, which is cheaper but clips anything brighter than white.

| Option | Effect |
| --- | --- |
| `--config <file>` | Load a different INI file. |
| `--set <key>=<value>` | Override a configuration key; may be repeated. |
| `--modules <a,b,c>` | Select the modules to load. |
| `--headless` | Use the virtual window module. |
| `--frames <n>` | Stop after the given number of frames. |
| `--screenshot <file>` | Save a PNG screenshot; limits the run to three frames if no frame limit is set. |
| `--log-level <level>` | Choose from `trace`, `debug`, `info`, `warn`, `error`, and `off`. |
| `--log-file <file>` | Also write the log to a file (`log.file`), flushed line by line. |
| `--strict` | Return exit code `3` if the graphics backend reports validation errors. |
| `--help` | Show usage information. |

Request Vulkan diagnostics during a headless run:

```sh
./build/bin/sandbox --headless --frames 120 --set rhi.validation=true --strict
```

The validation layer must be installed for these diagnostics to be active.

If validation reports errors about extensions the engine never enables, an implicit Vulkan layer installed on the machine (overlay, frame generation, capture tool) is most likely modifying the engine's requests. Rule it out by running once without implicit layers:

```sh
VK_LOADER_LAYERS_DISABLE='~implicit~' ./build/bin/sandbox --set rhi.validation=true --strict
```

## Build options

Pass options to CMake as `-DOPTION=ON` or `-DOPTION=OFF`.

| Option | Default | Effect |
| --- | --- | --- |
| `BDEX_SHARED_MODULES` | `ON` | Build shared libraries and modules; use `OFF` for static linking. |
| `BDEX_BUILD_SAMPLES` | `ON` | Build the sandbox. |
| `BDEX_BUILD_TESTS` | `ON` | Build tests if GoogleTest is available. |
| `BDEX_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |

Each module can also be disabled independently: `BDEX_MODULE_WINDOW_GLFW`, `BDEX_MODULE_WINDOW_NULL`, `BDEX_MODULE_RHI_VULKAN`, `BDEX_MODULE_RHI_NULL`, `BDEX_MODULE_RENDERER_FORWARD`, `BDEX_MODULE_AUDIO_NULL`, and `BDEX_MODULE_AUDIO_OPENAL`. All are enabled by default (`audio_openal` only if OpenAL is found; applications then load it by default, `audio_null` otherwise). The modules selected at runtime must be available in the build.

## Tests

With GoogleTest installed and `BDEX_BUILD_TESTS=ON`:

```sh
ctest --test-dir build --output-on-failure
```

Tests cover the core, plugin loading, ECS, assets, scenes, physics, and renderer integration with the `null` and Vulkan backends. Vulkan tests run offscreen and are skipped if no usable Vulkan device is available. Robustness tests feed tens of thousands of deterministic corruptions of valid files to every loader of outside data (PNG, TGA, WAV, OBJ, config and scene files, then a few physics steps over what loaded); they must never crash or throw, which the `asan` preset checks down to memory accesses and undefined behavior (any UBSan report, float-to-integer overflows included, fails the run).

If GoogleTest is missing, CMake prints a warning and continues configuration without building the tests.

[`.clang-tidy`](.clang-tidy) selects clang-tidy's bug-finding checks (bugprone, performance, concurrency, and the Clang static analyzer). Run them over the engine and modules with the compile commands of a build: `run-clang-tidy -p build -quiet '/(engine|modules)/.*\.cpp$'`.

[Continuous integration](.github/workflows/ci.yml) builds the `release`, `static`, and `asan` presets on Ubuntu 24.04 with GCC 14, and `release` with Clang 19, warnings treated as errors, then runs the tests. Vulkan tests run there too, on lavapipe, Mesa's CPU implementation of Vulkan (the `asan` preset leaves them out).

## Extending the engine

- **Create an application:** derive from [`bdex::app::Application`](engine/app/include/bdex/app/Application.hpp), implement the callbacks you need (`onStart`, `onUpdate`, `onFixedUpdate`, `onStop`), and define the entry point with `BDEX_MAIN`.
- **Add gameplay:** define components and iterate over [`ecs::World`](engine/ecs/include/bdex/ecs/World.hpp) views, then register your systems with the scheduler.
- **Add a render feature:** implement `IRenderFeature` and register it with the renderer. The [sandbox sky](samples/sandbox/src/SkyFeature.cpp) provides a complete example.
- **Add an asset format:** register a loader with [`AssetManager`](engine/assets/include/bdex/assets/AssetManager.hpp).
- **Add a module:** implement `IModule`, declare it with `bdex_add_module()`, and list it in `engine.modules`. See [Writing a module](docs/ARCHITECTURE.md#writing-a-module).

The [sandbox application](samples/sandbox/src/Sandbox.cpp) brings these mechanisms together in a working example.
