#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "gamesdk/game_factory.hpp"
#include "host/host_runtime.hpp"

namespace nlp3::host {

class LocalGameRegistry {
public:
    bool register_factory(std::unique_ptr<gamesdk::IGameFactory> factory);

    std::size_t registered_count() const noexcept;
    std::vector<gamesdk::GameManifest> list_manifests() const;
    const gamesdk::GameManifest* find_manifest(std::string_view game_id) const noexcept;
    bool is_compatible(std::string_view game_id, const HostRuntime& runtime) const noexcept;
    bool activate_game(std::string_view game_id, HostRuntime& runtime) const;

private:
    const gamesdk::IGameFactory* find_factory(std::string_view game_id) const noexcept;

    std::vector<std::unique_ptr<gamesdk::IGameFactory>> factories_{};
};

} // namespace nlp3::host
