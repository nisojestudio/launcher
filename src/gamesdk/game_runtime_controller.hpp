#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "gamesdk/game_module.hpp"
#include "gamesdk/game_registry.hpp"

namespace nlp3::gamesdk {

enum class GameRuntimeState {
    idle,
    activating,
    active,
    paused,
    faulted,
};

struct GameRuntimeStatus {
    GameRuntimeState state = GameRuntimeState::idle;
    std::string active_game_id{};
    std::string last_error{};
    bool has_game = false;
};

class GameRuntimeController {
public:
    explicit GameRuntimeController(GameRegistry* registry = nullptr) noexcept;

    bool activate(std::string_view game_id);
    bool pause();
    bool resume();
    void deactivate();
    bool restart();

    GameRuntimeStatus status() const;

    IGameModule* active_game() noexcept;
    const IGameModule* active_game() const noexcept;

private:
    GameRegistry* registry_ = nullptr;
    std::unique_ptr<IGameModule> active_game_{};
    GameRuntimeState state_ = GameRuntimeState::idle;
    std::string active_game_id_{};
    std::string last_error_{};
};

} // namespace nlp3::gamesdk
