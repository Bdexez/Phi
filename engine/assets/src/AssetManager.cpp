#include <phi/assets/AssetManager.hpp>

#include <phi/assets/Audio.hpp>
#include <phi/assets/Image.hpp>
#include <phi/assets/Mesh.hpp>
#include <phi/core/JobSystem.hpp>
#include <phi/core/Log.hpp>

#include <cstring>
#include <filesystem>

namespace phi::assets {

AssetManager::AssetManager(const VirtualFileSystem& vfs, JobSystem* jobs)
    : vfs_(&vfs), jobs_(jobs) {
    registerLoader<MeshData>(".obj", [](std::span<const u8> bytes) {
        return parseObj(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    });

    registerLoader<ImageData>(".tga", [](std::span<const u8> bytes) { return decodeTga(bytes); });
    registerLoader<ImageData>(".png", [](std::span<const u8> bytes) { return decodePng(bytes); });
    registerLoader<AudioData>(".wav", [](std::span<const u8> bytes) { return decodeWav(bytes); });

    registerLoader<ShaderBinary>(".spv", [](std::span<const u8> bytes) -> Result<ShaderBinary> {
        constexpr u32 kSpirvMagic = 0x07230203;
        if (bytes.size() < 4 || bytes.size() % 4 != 0) {
            return fail("spv: size is not a multiple of 4 bytes");
        }
        ShaderBinary binary;
        binary.words.resize(bytes.size() / 4);
        std::memcpy(binary.words.data(), bytes.data(), bytes.size());
        if (binary.words.front() != kSpirvMagic) {
            return fail("spv: bad magic number");
        }
        return binary;
    });
}

AssetManager::~AssetManager() {
    // Running jobs reference this manager: let them finish first.
    std::vector<std::shared_future<detail::ErasedAsset>> pending;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [key, future] : inFlight_) {
            pending.push_back(future);
        }
    }
    for (const auto& future : pending) {
        future.wait();
    }
}

std::string AssetManager::normalizeExtension(std::string_view extension) {
    std::string result(extension);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

void AssetManager::addLoader(TypeId type, std::string_view extension, ErasedLoader loader) {
    std::lock_guard lock(mutex_);
    loaders_[Key{type, normalizeExtension(extension)}] = std::move(loader);
}

void AssetManager::clearCache() {
    std::lock_guard lock(mutex_);
    cache_.clear();
}

usize AssetManager::cachedCount() const {
    std::lock_guard lock(mutex_);
    return cache_.size();
}

std::shared_future<detail::ErasedAsset> AssetManager::request(TypeId type,
                                                              std::string_view typeName,
                                                              std::string_view virtualPath,
                                                              bool async) {
    const auto ready = [](detail::ErasedAsset result) {
        std::promise<detail::ErasedAsset> promise;
        promise.set_value(std::move(result));
        return promise.get_future().share();
    };

    Key key{type, std::string(virtualPath)};
    std::unique_lock lock(mutex_);
    if (const auto cached = cache_.find(key); cached != cache_.end()) {
        return ready(cached->second);
    }
    if (const auto running = inFlight_.find(key); running != inFlight_.end()) {
        if (running->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return running->second;  // join the load in progress
        }
        inFlight_.erase(running);  // finished (and failed, or it would be cached): retry
    }

    const std::string extension =
        normalizeExtension(std::filesystem::path(virtualPath).extension().string());
    const auto found = loaders_.find(Key{type, extension});
    if (found == loaders_.end()) {
        return ready(fail("no loader registered for '{}' files as {}", extension, typeName));
    }
    // A copy: registerLoader may replace the entry while the load runs.
    auto loader = std::make_shared<const ErasedLoader>(found->second);

    if (async && jobs_ != nullptr) {
        auto future = jobs_->submit([this, key, name = std::string(typeName), loader] {
            return loadNow(key, name, loader.get());
        });
        auto shared = future.share();
        inFlight_.emplace(std::move(key), shared);
        return shared;
    }

    // Synchronous: published as in flight so that concurrent requests wait
    // for this load instead of repeating it.
    std::promise<detail::ErasedAsset> promise;
    auto shared = promise.get_future().share();
    inFlight_.emplace(key, shared);
    lock.unlock();
    promise.set_value(loadNow(key, typeName, loader.get()));
    return shared;
}

void AssetManager::await(const std::shared_future<detail::ErasedAsset>& pending) const {
    if (jobs_ != nullptr) {
        jobs_->wait(pending);
    } else {
        pending.wait();
    }
}

detail::ErasedAsset AssetManager::loadNow(const Key& key, std::string_view typeName,
                                          const ErasedLoader* loader) {
    const std::string& virtualPath = key.second;
    detail::ErasedAsset asset = [&]() -> detail::ErasedAsset {
        const auto bytes = vfs_->readBinary(virtualPath);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto loaded = (*loader)(*bytes);
        if (!loaded) {
            return fail("failed to load '{}' as {}: {}", virtualPath, typeName,
                        loaded.error().message);
        }
        return loaded;
    }();

    if (asset) {
        log::debug("assets", "loaded '{}'", virtualPath);
        std::lock_guard lock(mutex_);
        cache_.emplace(key, *asset);
    }
    return asset;
}

}  // namespace phi::assets
