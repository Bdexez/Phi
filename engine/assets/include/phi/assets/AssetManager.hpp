#pragma once

#include <phi/core/FileSystem.hpp>
#include <phi/core/Result.hpp>
#include <phi/core/TypeId.hpp>

#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace phi {
class JobSystem;
}

namespace phi::assets {

/// SPIR-V module as loaded from disk.
struct ShaderBinary {
    std::vector<u32> words;
};

namespace detail {
using ErasedAsset = Result<std::shared_ptr<void>>;
}

/// Pending result of AssetManager::loadAsync(). Cheap to copy; every copy
/// observes the same load.
template <class T>
class AssetFuture {
public:
    AssetFuture() = default;
    explicit AssetFuture(std::shared_future<detail::ErasedAsset> future)
        : future_(std::move(future)) {}

    [[nodiscard]] bool valid() const { return future_.valid(); }
    /// True once get() would not block.
    [[nodiscard]] bool ready() const {
        return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    void wait() const { future_.wait(); }

    /// Blocks until the load is done.
    [[nodiscard]] Result<std::shared_ptr<const T>> get() const {
        const detail::ErasedAsset& result = future_.get();
        if (!result) {
            return std::unexpected(result.error());
        }
        return std::static_pointer_cast<const T>(*result);
    }

private:
    std::shared_future<detail::ErasedAsset> future_;
};

/// Loads and caches assets by virtual path.
///
/// Loaders are registered per (asset type, file extension), so support for a
/// new format can be added from anywhere - an application or a module - without
/// modifying this class:
///
///   assets.registerLoader<ImageData>(".png", [](std::span<const u8> bytes) { ... });
///   auto image = assets.load<ImageData>("app:/textures/crate.png");
///   auto pending = assets.loadAsync<ImageData>("app:/textures/sky.png");
///
/// Built-in loaders: MeshData (.obj), ImageData (.png, .tga), ShaderBinary (.spv).
///
/// Thread-safe. Asynchronous loads run on the JobSystem given at
/// construction (synchronously without one); loaders must therefore be
/// callable from any thread. A request for an asset that is already being
/// loaded waits for that load instead of starting another.
class AssetManager {
public:
    explicit AssetManager(const VirtualFileSystem& vfs, JobSystem* jobs = nullptr);
    /// Waits for the asynchronous loads still running.
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    template <class T>
    using Loader = std::function<Result<T>(std::span<const u8>)>;

    /// `extension` includes the dot (".obj") and is matched without regard to
    /// ASCII case, so "MODEL.OBJ" uses the same loader.
    template <class T>
    void registerLoader(std::string_view extension, Loader<T> loader) {
        addLoader(typeId<T>(), extension,
                  [loader = std::move(loader)](std::span<const u8> bytes) -> detail::ErasedAsset {
                      Result<T> result = loader(bytes);
                      if (!result) {
                          return std::unexpected(result.error());
                      }
                      return std::make_shared<T>(std::move(*result));
                  });
    }

    /// Returns the cached asset, loading it on first use. If an asynchronous
    /// load of it is already under way, waits for that one, running queued
    /// jobs meanwhile: from inside a job, that load may be queued behind it.
    template <class T>
    [[nodiscard]] Result<std::shared_ptr<const T>> load(std::string_view virtualPath) {
        const auto pending = request(typeId<T>(), typeName<T>(), virtualPath, false);
        await(pending);
        return AssetFuture<T>(pending).get();
    }

    /// Starts loading the asset on a worker thread (or returns the cached
    /// one, already completed).
    template <class T>
    [[nodiscard]] AssetFuture<T> loadAsync(std::string_view virtualPath) {
        return AssetFuture<T>(request(typeId<T>(), typeName<T>(), virtualPath, true));
    }

    /// Drops the cache; assets still referenced elsewhere stay alive. Loads
    /// in flight complete and are cached afterwards.
    void clearCache();
    [[nodiscard]] usize cachedCount() const;

private:
    using ErasedLoader = std::function<detail::ErasedAsset(std::span<const u8>)>;
    using Key = std::pair<TypeId, std::string>;

    [[nodiscard]] static std::string normalizeExtension(std::string_view extension);
    void addLoader(TypeId type, std::string_view extension, ErasedLoader loader);

    std::shared_future<detail::ErasedAsset> request(TypeId type, std::string_view typeName,
                                                    std::string_view virtualPath, bool async);
    detail::ErasedAsset loadNow(const Key& key, std::string_view typeName,
                                const ErasedLoader* loader);
    /// Waits for a load, running queued jobs meanwhile (JobSystem::wait).
    void await(const std::shared_future<detail::ErasedAsset>& pending) const;

    const VirtualFileSystem* vfs_;
    JobSystem* jobs_;
    mutable std::mutex mutex_;
    std::map<Key, ErasedLoader> loaders_;                              // (type, extension)
    std::map<Key, std::shared_ptr<void>> cache_;                       // (type, virtual path)
    std::map<Key, std::shared_future<detail::ErasedAsset>> inFlight_;  // (type, virtual path)
};

}  // namespace phi::assets
