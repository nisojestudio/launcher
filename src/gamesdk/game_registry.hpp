#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "gamesdk/game_catalog.hpp"
#include "gamesdk/game_factory.hpp"
#include "gamesdk/game_module.hpp"

namespace nlp3::gamesdk {

class GameFactoryRegistry {
public:
    bool register_factory(std::unique_ptr<IGameFactory> factory);

    std::size_t registered_count() const noexcept;
    const IGameFactory* find(std::string_view game_id) const noexcept;
    bool contains(std::string_view game_id) const noexcept;
    std::unique_ptr<IGameModule> create(std::string_view game_id) const;

private:
    std::vector<std::unique_ptr<IGameFactory>> factories_{};
};

class GameRegistry {
public:
    explicit GameRegistry(GameFactoryRegistry* factories = nullptr) noexcept;

    GameCatalog& catalog() noexcept;
    const GameCatalog& catalog() const noexcept;

    GameFactoryRegistry* factories() const noexcept;

    bool can_activate(std::string_view game_id) const noexcept;
    std::unique_ptr<IGameModule> create(std::string_view game_id) const;

private:
    GameCatalog catalog_{};
    GameFactoryRegistry* factories_ = nullptr;
};

} // namespace nlp3::gamesdk
