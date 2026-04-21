#pragma once

#include "platform/game_catalog_source.hpp"

namespace nlp3::platform {

class LocalGameCatalogSource final : public IGameCatalogSource {
public:
    LocalGameCatalogSource() = default;

    void set_catalog(gamesdk::GameCatalog catalog);
    gamesdk::GameCatalog load_catalog() const override;

private:
    gamesdk::GameCatalog catalog_{};
};

} // namespace nlp3::platform
