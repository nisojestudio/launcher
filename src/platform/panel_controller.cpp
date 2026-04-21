#include "platform/panel_controller.hpp"

#include <utility>

#include "bridge/tiktok_bridge_controller.hpp"
#include "gamesdk/game_runtime_controller.hpp"
#include "host/host_runtime.hpp"

namespace {

void attach_active_game(
    nlp3::host::HostRuntime* runtime,
    nlp3::gamesdk::GameRuntimeController* game_runtime_controller) {
    if (runtime == nullptr || game_runtime_controller == nullptr) {
        return;
    }

    auto* active_game = game_runtime_controller->active_game();
    if (active_game != nullptr) {
        active_game->apply_config(active_game->default_config());
    }

    runtime->attach_game(active_game);
}

} // namespace

namespace nlp3::platform {

PanelController::PanelController(
    host::HostRuntime* runtime,
    bridge::TikTokBridgeController* bridge_controller,
    gamesdk::GameRuntimeController* game_runtime_controller) noexcept
    : runtime_(runtime),
      bridge_controller_(bridge_controller),
      game_runtime_controller_(game_runtime_controller) {
}

PanelCommandResult PanelController::execute(const PanelCommand& command) {
    switch (command.kind) {
    case PanelCommandKind::bridge_start:
        if (bridge_controller_ == nullptr) {
            return {false, "bridge_controller_unavailable"};
        }
        {
            const auto started = bridge_controller_->start();
            return {started, started ? "bridge_started" : "bridge_start_failed"};
        }

    case PanelCommandKind::bridge_stop:
        if (bridge_controller_ == nullptr) {
            return {false, "bridge_controller_unavailable"};
        }
        bridge_controller_->stop();
        return {true, "bridge_stopped"};

    case PanelCommandKind::bridge_reset:
        if (bridge_controller_ == nullptr) {
            return {false, "bridge_controller_unavailable"};
        }
        bridge_controller_->reset();
        return {true, "bridge_reset"};

    case PanelCommandKind::game_activate:
        if (game_runtime_controller_ == nullptr) {
            return {false, "game_runtime_controller_unavailable"};
        }
        if (command.argument.empty()) {
            return {false, "game_id_required"};
        }
        if (!game_runtime_controller_->activate(command.argument)) {
            return {false, "game_activation_failed"};
        }
        attach_active_game(runtime_, game_runtime_controller_);
        return {true, "game_activated"};

    case PanelCommandKind::game_deactivate:
        if (game_runtime_controller_ == nullptr) {
            return {false, "game_runtime_controller_unavailable"};
        }
        game_runtime_controller_->deactivate();
        if (runtime_ != nullptr) {
            runtime_->attach_game(nullptr);
        }
        return {true, "game_deactivated"};

    case PanelCommandKind::game_restart:
        if (game_runtime_controller_ == nullptr) {
            return {false, "game_runtime_controller_unavailable"};
        }
        if (!game_runtime_controller_->restart()) {
            return {false, "game_restart_failed"};
        }
        attach_active_game(runtime_, game_runtime_controller_);
        return {true, "game_restarted"};

    case PanelCommandKind::tts_enqueue_announcement:
        if (runtime_ == nullptr) {
            return {false, "host_runtime_unavailable"};
        }
        {
            const auto enqueued = runtime_->queue_tts_announcement(command.argument);
            return {enqueued, enqueued ? "tts_announcement_enqueued" : "tts_announcement_failed"};
        }

    case PanelCommandKind::unknown:
        break;
    }

    return {false, "unsupported_command"};
}

} // namespace nlp3::platform
