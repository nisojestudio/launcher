#include "gamesdk/game_registry.hpp"

#include <utility>

namespace nlp3::gamesdk {

bool GameFactoryRegistry::register_factory(std::unique_ptr<IGameFactory> factory) {
    if (factory == nullptr) {
        return false;
    }

    const auto game_id = factory->manifest().game_id;
    if (find(game_id) != nullptr) {
        return false;
    }

    factories_.push_back(std::move(factory));
    return true;
}

std::size_t GameFactoryRegistry::registered_count() const noexcept {
    return factories_.size();
}

const IGameFactory* GameFactoryRegistry::find(std::string_view game_id) const noexcept {
    for (const auto& factory : factories_) {
        if (factory->manifest().game_id == game_id) {
            return factory.get();
        }
    }

    return nullptr;
}

bool GameFactoryRegistry::contains(std::string_view game_id) const noexcept {
    return find(game_id) != nullptr;
}

std::unique_ptr<IGameModule> GameFactoryRegistry::create(std::string_view game_id) const {
    const auto* factory = find(game_id);
    return factory != nullptr ? factory->create() : nullptr;
}

GameRegistry::GameRegistry(GameFactoryRegistry* factories) noexcept
    : factories_(factories) {
}

GameCatalog& GameRegistry::catalog() noexcept {
    return catalog_;
}

const GameCatalog& GameRegistry::catalog() const noexcept {
    return catalog_;
}

GameFactoryRegistry* GameRegistry::factories() const noexcept {
    return factories_;
}

bool GameRegistry::can_activate(std::string_view game_id) const noexcept {
    const auto* entry = catalog_.find_by_id(game_id);
    return entry != nullptr
        && entry->enabled
        && entry->installed
        && factories_ != nullptr
        && factories_->contains(game_id);
}

std::unique_ptr<IGameModule> GameRegistry::create(std::string_view game_id) const {
    if (!can_activate(game_id)) {
        return nullptr;
    }

    return factories_->create(game_id);
}

} // namespace nlp3::gamesdk
