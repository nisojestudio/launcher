#include "gamesdk/game_runtime_controller.hpp"

namespace nlp3::gamesdk {

GameRuntimeController::GameRuntimeController(GameRegistry* registry) noexcept
    : registry_(registry) {
}

bool GameRuntimeController::activate(std::string_view game_id) {
    if (registry_ == nullptr) {
        active_game_.reset();
        active_game_id_.clear();
        last_error_ = "game_registry_unavailable";
        state_ = GameRuntimeState::faulted;
        return false;
    }

    state_ = GameRuntimeState::activating;
    last_error_.clear();
    active_game_.reset();
    active_game_id_.clear();

    auto game = registry_->create(game_id);
    if (game == nullptr) {
        state_ = GameRuntimeState::faulted;
        last_error_ = "game_activation_failed";
        return false;
    }

    active_game_ = std::move(game);
    active_game_id_ = std::string(game_id);
    state_ = GameRuntimeState::active;
    return true;
}

bool GameRuntimeController::pause() {
    if (active_game_ == nullptr || state_ != GameRuntimeState::active) {
        return false;
    }

    state_ = GameRuntimeState::paused;
    return true;
}

bool GameRuntimeController::resume() {
    if (active_game_ == nullptr || state_ != GameRuntimeState::paused) {
        return false;
    }

    state_ = GameRuntimeState::active;
    return true;
}

void GameRuntimeController::deactivate() {
    active_game_.reset();
    active_game_id_.clear();
    last_error_.clear();
    state_ = GameRuntimeState::idle;
}

bool GameRuntimeController::restart() {
    if (active_game_id_.empty()) {
        return false;
    }

    const auto game_id = active_game_id_;
    deactivate();
    return activate(game_id);
}

GameRuntimeStatus GameRuntimeController::status() const {
    return GameRuntimeStatus{
        state_,
        active_game_id_,
        last_error_,
        active_game_ != nullptr,
    };
}

IGameModule* GameRuntimeController::active_game() noexcept {
    return active_game_.get();
}

const IGameModule* GameRuntimeController::active_game() const noexcept {
    return active_game_.get();
}

} // namespace nlp3::gamesdk
