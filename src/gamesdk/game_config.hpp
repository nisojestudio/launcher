#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace nlp3::gamesdk {

using GameConfigValue = std::variant<bool, std::int64_t, double, std::string>;

struct GameConfigEntry {
    std::string key;
    GameConfigValue value{};
};

class GameConfig {
public:
    void set(std::string key, GameConfigValue value);
    const GameConfigValue* find(std::string_view key) const noexcept;

    bool get_bool(std::string_view key, bool fallback = false) const noexcept;
    std::int64_t get_int(std::string_view key, std::int64_t fallback = 0) const noexcept;
    double get_double(std::string_view key, double fallback = 0.0) const noexcept;
    std::string get_string(std::string_view key, std::string fallback = {}) const;

private:
    std::unordered_map<std::string, GameConfigValue> values_{};
};

} // namespace nlp3::gamesdk
