#pragma once

#include <phi/core/Result.hpp>
#include <phi/core/Types.hpp>

#include <string>
#include <vector>

namespace phi {

class Engine;

/// Bumped whenever the IModule contract or the core ABI changes. A plugin
/// built against another value is rejected at load time instead of crashing.
inline constexpr u32 kModuleAbiVersion = 1;

/// Static description of a module, used to compute the initialization order.
///
/// Dependencies are expressed on *capabilities*, not on module names: the
/// renderer requires "rhi", which can be satisfied by `rhi_vulkan`, `rhi_null`
/// or any future backend. A module always provides its own name.
struct ModuleInfo {
    std::string name;
    std::string description;
    /// Capabilities this module makes available (e.g. "window", "rhi").
    std::vector<std::string> provides;
    /// Capabilities that must be provided by another loaded module.
    std::vector<std::string> dependencies;
    /// Capabilities this module must be initialized after *if* they are
    /// present, without requiring them (e.g. the RHI wants to come after the
    /// window system, but also works headless).
    std::vector<std::string> after;
};

/// A unit of engine functionality with an explicit lifecycle.
///
/// A module talks to the rest of the engine exclusively through the Engine
/// facade: it provides and consumes services, publishes and subscribes to
/// events, and schedules tasks. It never links against another module.
class IModule {
public:
    virtual ~IModule() = default;

    [[nodiscard]] virtual ModuleInfo info() const = 0;

    /// Called once, after every dependency has been initialized.
    virtual Result<> onInit(Engine& engine) = 0;

    /// Called once, in reverse initialization order. Must release everything
    /// registered in onInit (services, tasks, subscriptions).
    virtual void onShutdown(Engine& engine) = 0;
};

using ModuleFactory = IModule* (*)();

}  // namespace phi

// --- Module entry point ------------------------------------------------------

#if defined(_WIN32)
#define PHI_MODULE_EXPORT __declspec(dllexport)
#else
#define PHI_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#define PHI_DETAIL_CONCAT_IMPL(a, b) a##b
#define PHI_DETAIL_CONCAT(a, b) PHI_DETAIL_CONCAT_IMPL(a, b)
#define PHI_DETAIL_STRINGIFY_IMPL(x) #x
#define PHI_DETAIL_STRINGIFY(x) PHI_DETAIL_STRINGIFY_IMPL(x)

/// Name of the module being compiled, as a string literal. PHI_MODULE_NAME is
/// defined by phi_add_module() in CMake.
#define PHI_MODULE_NAME_STRING PHI_DETAIL_STRINGIFY(PHI_MODULE_NAME)

/// Emits the entry points of a module. Use once, at global scope, in exactly
/// one source file of the module:
///
///   PHI_DEFINE_MODULE(MyModule)
///
/// The exported symbols are `phi_create_module_<name>` and
/// `phi_module_abi_<name>`; the same symbols serve both static registration
/// and runtime plugin loading.
#define PHI_DEFINE_MODULE(ModuleClass)                                              \
    extern "C" PHI_MODULE_EXPORT ::phi::IModule* PHI_DETAIL_CONCAT(               \
        phi_create_module_, PHI_MODULE_NAME)() {                                   \
        return new ModuleClass();                                                    \
    }                                                                                \
    extern "C" PHI_MODULE_EXPORT ::phi::u32 PHI_DETAIL_CONCAT(phi_module_abi_,   \
                                                                PHI_MODULE_NAME)() { \
        return ::phi::kModuleAbiVersion;                                            \
    }
