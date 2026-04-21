#include "platform/panel_snapshot_builder.hpp"

#include <utility>

namespace nlp3::platform {

PanelSnapshot build_panel_snapshot(
    const PanelConfig& config,
    const host::HostRuntime& runtime,
    const gamesdk::GameRuntimeController& game_runtime_controller,
    const PanelActivityLog* activity_log,
    const ILicenseService* license_service,
    PanelAuthStatus auth_status,
    ExternalBridgeManifest external_bridge_manifest,
    PanelExternalWsStatus external_ws_status,
    PanelExternalGameStatus external_game_status) {
    const auto game_status = game_runtime_controller.status();

    PanelSnapshot snapshot{};
    snapshot.panel_name = config.panel_name;
    snapshot.bridge_mode = config.bridge_mode;
    snapshot.total_events = runtime.snapshot().total_events;
    snapshot.bridge = runtime.bridge_status();
    snapshot.tts.available = runtime.has_tts_service();
    snapshot.tts.queued_messages = runtime.queued_tts_messages();
    snapshot.game.has_active_game = game_status.has_game || runtime.has_active_game();
    snapshot.game.active_game_id = !game_status.active_game_id.empty()
        ? game_status.active_game_id
        : std::string(runtime.active_game_id());
    snapshot.game.runtime_state = game_status.has_game || game_status.state != gamesdk::GameRuntimeState::idle
        ? game_status.state
        : (runtime.has_active_game() ? gamesdk::GameRuntimeState::active : gamesdk::GameRuntimeState::idle);
    snapshot.game.last_error = game_status.last_error;
    snapshot.auth = std::move(auth_status);
    if (license_service != nullptr) {
        const auto license = license_service->snapshot();
        snapshot.license = PanelLicenseStatus{
            license.status,
            license.message,
            license.tier,
        };
    }
    snapshot.external_bridge.external_mode = external_bridge_manifest.external_mode;
    snapshot.external_bridge.recording = external_bridge_manifest.recording;
    snapshot.external_bridge.recording_path = std::move(external_bridge_manifest.recording_path);
    snapshot.external_bridge.last_replay_path = std::move(external_bridge_manifest.last_replay_path);
    snapshot.external_bridge.last_replay_accepted_events =
        external_bridge_manifest.last_replay_accepted_events;
    snapshot.external_bridge.total_external_events_submitted =
        external_bridge_manifest.total_external_events_submitted;
    snapshot.external_bridge.target_user = std::move(external_bridge_manifest.target_user);
    snapshot.external_bridge.connection_state = std::move(external_bridge_manifest.connection_state);
    snapshot.external_bridge.last_status_message = std::move(external_bridge_manifest.last_status_message);
    snapshot.external_bridge.last_status_timestamp_ms = external_bridge_manifest.last_status_timestamp_ms;
    snapshot.external_bridge.current_room_id = std::move(external_bridge_manifest.current_room_id);
    snapshot.external_bridge.last_event_kind = std::move(external_bridge_manifest.last_event_kind);
    snapshot.external_bridge.last_event_actor = std::move(external_bridge_manifest.last_event_actor);
    snapshot.external_bridge.last_event_timestamp_ms = external_bridge_manifest.last_event_timestamp_ms;
    snapshot.external_bridge.chat_events = external_bridge_manifest.chat_events;
    snapshot.external_bridge.like_events = external_bridge_manifest.like_events;
    snapshot.external_bridge.gift_events = external_bridge_manifest.gift_events;
    snapshot.external_bridge.follow_events = external_bridge_manifest.follow_events;
    snapshot.external_bridge.share_events = external_bridge_manifest.share_events;
    snapshot.external_bridge.viewer_join_events = external_bridge_manifest.viewer_join_events;
    snapshot.external_bridge.viewer_count_events = external_bridge_manifest.viewer_count_events;
    snapshot.external_bridge.live_start_events = external_bridge_manifest.live_start_events;
    snapshot.external_bridge.live_end_events = external_bridge_manifest.live_end_events;
    snapshot.external_bridge.moderation_events = external_bridge_manifest.moderation_events;
    snapshot.external_bridge.custom_raw_events = external_bridge_manifest.custom_raw_events;
    snapshot.external_bridge.runner_running = external_bridge_manifest.runner_running;
    snapshot.external_bridge.runner_process_id = external_bridge_manifest.runner_process_id;
    snapshot.external_bridge.runner_ws_url = std::move(external_bridge_manifest.runner_ws_url);
    snapshot.external_bridge.runtime_checked = external_bridge_manifest.runtime_checked;
    snapshot.external_bridge.runtime_ready = external_bridge_manifest.runtime_ready;
    snapshot.external_bridge.runtime_checked_timestamp_ms =
        external_bridge_manifest.runtime_checked_timestamp_ms;
    snapshot.external_bridge.runtime_summary = std::move(external_bridge_manifest.runtime_summary);
    snapshot.external_bridge.runtime_alerts = std::move(external_bridge_manifest.runtime_alerts);
    snapshot.external_bridge.runner_has_exit_code = external_bridge_manifest.runner_has_exit_code;
    snapshot.external_bridge.runner_last_exit_code = external_bridge_manifest.runner_last_exit_code;
    snapshot.external_bridge.runner_last_error = std::move(external_bridge_manifest.runner_last_error);
    snapshot.external_bridge.runner_recent_log_lines = std::move(external_bridge_manifest.runner_recent_log_lines);
    snapshot.external_ws = external_ws_status;
    snapshot.external_game = std::move(external_game_status);
    if (snapshot.external_game.active) {
        snapshot.game.has_active_game = true;
        snapshot.game.active_game_id = snapshot.external_game.game_id;
        if (snapshot.external_game.bridge_running || snapshot.external_game.game_running) {
            snapshot.game.runtime_state = gamesdk::GameRuntimeState::active;
        } else if (snapshot.external_game.state == "starting") {
            snapshot.game.runtime_state = gamesdk::GameRuntimeState::activating;
        } else if (!snapshot.external_game.last_error.empty()) {
            snapshot.game.runtime_state = gamesdk::GameRuntimeState::faulted;
        }
        if (!snapshot.external_game.last_error.empty()) {
            snapshot.game.last_error = snapshot.external_game.last_error;
        }
    }
    if (activity_log != nullptr) {
        snapshot.recent_activity = activity_log->entries();
    }
    return snapshot;
}

PanelSnapshot build_panel_snapshot(
    const PanelConfig& config,
    const host::HostRuntime& runtime,
    const gamesdk::GameRuntimeController& game_runtime_controller,
    const PanelActivityLog* activity_log,
    const ILicenseService* license_service,
    PanelAuthStatus auth_status,
    ExternalBridgeManifest external_bridge_manifest,
    PanelExternalWsStatus external_ws_status) {
    return build_panel_snapshot(
        config,
        runtime,
        game_runtime_controller,
        activity_log,
        license_service,
        std::move(auth_status),
        std::move(external_bridge_manifest),
        std::move(external_ws_status),
        {});
}

} // namespace nlp3::platform
