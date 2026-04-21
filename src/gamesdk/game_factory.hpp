#pragma once

#include <memory>

#include "gamesdk/game_manifest.hpp"
#include "gamesdk/game_module.hpp"

namespace nlp3::gamesdk {

class IGameFactory {
public:
    virtual ~IGameFactory() = default;

    virtual const GameManifest& manifest() const noexcept = 0;
    virtual std::unique_ptr<IGameModule> create() const = 0;
};

} // namespace nlp3::gamesdk
