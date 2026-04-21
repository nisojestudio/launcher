#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gamesdk/game_manifest.hpp"

namespace nlp3::gamesdk {

struct GameCatalogEntry {
    std::string game_id;
    std::string display_name;
    std::string version;
    std::string source;
    bool installed = true;
    bool enabled = true;
    bool update_available = false;
    std::string install_state{};
    std::string install_message{};
    std::uint64_t install_progress_percent = 0;
    GameManifest manifest{};
};

class GameCatalog {
public:
    void add(GameCatalogEntry entry);
    const std::vector<GameCatalogEntry>& entries() const noexcept;

    const GameCatalogEntry* find_by_id(std::string_view game_id) const noexcept;
    bool contains(std::string_view game_id) const noexcept;

private:
    std::vector<GameCatalogEntry> entries_{};
};

} // namespace nlp3::gamesdk
