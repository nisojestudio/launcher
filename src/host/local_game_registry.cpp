#include "host/local_game_registry.hpp"

#include <utility>

namespace nlp3::host {

bool LocalGameRegistry::register_factory(std::unique_ptr<gamesdk::IGameFactory> factory) {
    if (factory == nullptr) {
        return false;
    }

    const auto game_id = factory->manifest().game_id;
    if (find_factory(game_id) != nullptr) {
        return false;
    }

    factories_.push_back(std::move(factory));
    return true;
}

std::size_t LocalGameRegistry::registered_count() const noexcept {
    return factories_.size();
}

std::vector<gamesdk::GameManifest> LocalGameRegistry::list_manifests() const {
    std::vector<gamesdk::GameManifest> manifests;
    manifests.reserve(factories_.size());

    for (const auto& factory : factories_) {
        manifests.push_back(factory->manifest());
    }

    return manifests;
}

const gamesdk::GameManifest* LocalGameRegistry::find_manifest(std::string_view game_id) const noexcept {
    const auto* factory = find_factory(game_id);
    return factory != nullptr ? &factory->manifest() : nullptr;
}

bool LocalGameRegistry::is_compatible(std::string_view game_id, const HostRuntime& runtime) const noexcept {
    const auto* manifest = find_manifest(game_id);
    return manifest != nullptr
        && gamesdk::is_compatible(manifest->compatibility, runtime.compatibility_profile());
}

bool LocalGameRegistry::activate_game(std::string_view game_id, HostRuntime& runtime) const {
    const auto* factory = find_factory(game_id);
    if (factory == nullptr) {
        return false;
    }

    if (!gamesdk::is_compatible(factory->manifest().compatibility, runtime.compatibility_profile())) {
        return false;
    }

    auto game = factory->create();
    if (game == nullptr) {
        return false;
    }

    runtime.activate_game(std::move(game));
    return runtime.has_active_game() && runtime.active_game_id() == factory->manifest().game_id;
}

const gamesdk::IGameFactory* LocalGameRegistry::find_factory(std::string_view game_id) const noexcept {
    for (const auto& factory : factories_) {
        if (factory->manifest().game_id == game_id) {
            return factory.get();
        }
    }

    return nullptr;
}

} // namespace nlp3::host
