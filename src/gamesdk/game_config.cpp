#include "gamesdk/game_config.hpp"

#include <utility>

namespace nlp3::gamesdk {

void GameConfig::set(std::string key, GameConfigValue value) {
    values_[std::move(key)] = std::move(value);
}

const GameConfigValue* GameConfig::find(std::string_view key) const noexcept {
    const auto it = values_.find(std::string(key));
    return it != values_.end() ? &it->second : nullptr;
}

bool GameConfig::get_bool(std::string_view key, bool fallback) const noexcept {
    const auto* value = find(key);
    return value != nullptr && std::holds_alternative<bool>(*value)
        ? std::get<bool>(*value)
        : fallback;
}

std::int64_t GameConfig::get_int(std::string_view key, std::int64_t fallback) const noexcept {
    const auto* value = find(key);
    return value != nullptr && std::holds_alternative<std::int64_t>(*value)
        ? std::get<std::int64_t>(*value)
        : fallback;
}

double GameConfig::get_double(std::string_view key, double fallback) const noexcept {
    const auto* value = find(key);
    return value != nullptr && std::holds_alternative<double>(*value)
        ? std::get<double>(*value)
        : fallback;
}

std::string GameConfig::get_string(std::string_view key, std::string fallback) const {
    const auto* value = find(key);
    return value != nullptr && std::holds_alternative<std::string>(*value)
        ? std::get<std::string>(*value)
        : fallback;
}

} // namespace nlp3::gamesdk
