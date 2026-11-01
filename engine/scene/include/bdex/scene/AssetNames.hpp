#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace bdex::scene {

/// Names for shared assets (animation clips, sounds...), so scene files can
/// refer to them: a pointer means nothing once the program exits, a name
/// ("lamp/sway") does. A name means one asset and an asset has one name.
/// Keeps the assets it names alive.
template <class T>
class AssetNames {
public:
    /// Names an asset; the name now means it, and its old name, if any, goes.
    void name(std::shared_ptr<const T> asset, std::string_view name) {
        if (asset == nullptr) {
            return;
        }
        if (const auto named = byName_.find(std::string(name)); named != byName_.end()) {
            byAsset_.erase(named->second.get());
            byName_.erase(named);
        }
        if (const auto previous = byAsset_.find(asset.get()); previous != byAsset_.end()) {
            byName_.erase(previous->second);
            byAsset_.erase(previous);
        }
        byAsset_.emplace(asset.get(), std::string(name));
        byName_.emplace(std::string(name), std::move(asset));
    }

    /// Null for an unknown name.
    [[nodiscard]] std::shared_ptr<const T> find(std::string_view name) const {
        const auto it = byName_.find(std::string(name));
        return it == byName_.end() ? nullptr : it->second;
    }

    /// Empty for an asset without a name.
    [[nodiscard]] std::string_view nameOf(const T* asset) const {
        const auto it = byAsset_.find(asset);
        return it == byAsset_.end() ? std::string_view{} : std::string_view(it->second);
    }

private:
    std::unordered_map<std::string, std::shared_ptr<const T>> byName_;
    std::unordered_map<const T*, std::string> byAsset_;
};

}  // namespace bdex::scene
