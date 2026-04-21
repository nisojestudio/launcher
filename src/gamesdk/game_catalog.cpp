#include "gamesdk/game_catalog.hpp"

#include <utility>

namespace nlp3::gamesdk {

void GameCatalog::add(GameCatalogEntry entry) {
    entries_.push_back(std::move(entry));
}

const std::vector<GameCatalogEntry>& GameCatalog::entries() const noexcept {
    return entries_;
}

const GameCatalogEntry* GameCatalog::find_by_id(std::string_view game_id) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.game_id == game_id) {
            return &entry;
        }
    }

    return nullptr;
}

bool GameCatalog::contains(std::string_view game_id) const noexcept {
    return find_by_id(game_id) != nullptr;
}

} // namespace nlp3::gamesdk
