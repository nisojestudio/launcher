#pragma once

#include "gamesdk/game_catalog.hpp"

namespace nlp3::platform {

class IGameCatalogSource {
public:
    virtual ~IGameCatalogSource() = default;

    virtual gamesdk::GameCatalog load_catalog() const = 0;
};

} // namespace nlp3::platform
