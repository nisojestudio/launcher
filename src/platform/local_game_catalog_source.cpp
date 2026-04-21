#include "platform/local_game_catalog_source.hpp"

#include <utility>

namespace nlp3::platform {

void LocalGameCatalogSource::set_catalog(gamesdk::GameCatalog catalog) {
    catalog_ = std::move(catalog);
}

gamesdk::GameCatalog LocalGameCatalogSource::load_catalog() const {
    return catalog_;
}

} // namespace nlp3::platform
