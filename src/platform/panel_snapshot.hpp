#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gamesdk/game_runtime_controller.hpp"
#include "host/host_runtime.hpp"
#include "platform/license_service.hpp"
#include "platform/panel_activity.hpp"
#include "platform/external_game_state.hpp"

namespace nlp3::platform {

struct PanelTtsStatus {
    bool available = false;
    std::size_t queued_messages = 0;
};

struct PanelGameStatus {
    bool has_active_game = false;
    std::string active_game_id{};
    gamesdk::GameRuntimeState runtime_state = gamesdk::GameRuntimeState::idle;
    std::string last_error{};
};

struct PanelLicenseStatus {
    LicenseStatus status = LicenseStatus::unknown;
    std::string message{};
    std::string tier{};
};

struct PanelAuthStatus {
    bool required = false;
    bool authenticated = false;
    std::string email{};
    std::string firebase_uid{};
    std::string license_key{};
    std::string message{};
    std::string last_error_code{};
    std::int64_t last_validated_timestamp_ms = 0;
};

struct PanelExternalBridgeStatus {
    bool external_mode = false;
    bool recording = false;
    std::string recording_path{};
    std::string last_replay_path{};
    std::size_t last_replay_accepted_events = 0;
    std::size_t total_external_events_submitted = 0;
    std::string target_user{};
    std::string connection_state{};
    std::string last_status_message{};
    std::int64_t last_status_timestamp_ms = 0;
    std::string current_room_id{};
    std::string last_event_kind{};
    std::string last_event_actor{};
    std::int64_t last_event_timestamp_ms = 0;
    std::size_t chat_events = 0;
    std::size_t like_events = 0;
    std::size_t gift_events = 0;
    std::size_t follow_events = 0;
    std::size_t share_events = 0;
    std::size_t viewer_join_events = 0;
    std::size_t viewer_count_events = 0;
    std::size_t live_start_events = 0;
    std::size_t live_end_events = 0;
    std::size_t moderation_events = 0;
    std::size_t custom_raw_events = 0;
    bool runner_running = false;
    std::uint32_t runner_process_id = 0;
    std::string runner_ws_url{};
    bool runtime_checked = false;
    bool runtime_ready = false;
    std::int64_t runtime_checked_timestamp_ms = 0;
    std::string runtime_summary{};
    std::vector<std::string> runtime_alerts{};
    bool runner_has_exit_code = false;
    std::int32_t runner_last_exit_code = 0;
    std::string runner_last_error{};
    std::vector<std::string> runner_recent_log_lines{};
};

struct PanelExternalWsStatus {
    bool running = false;
    std::uint16_t port = 0;
    std::size_t accepted_messages = 0;
    std::size_t rejected_messages = 0;
};

struct PanelSnapshot {
    std::string panel_name = "Nisoje Studio";
    std::string bridge_mode = "stub";
    std::size_t total_events = 0;
    host::HostBridgeStatus bridge{};
    PanelTtsStatus tts{};
    PanelGameStatus game{};
    PanelLicenseStatus license{};
    PanelAuthStatus auth{};
    PanelExternalBridgeStatus external_bridge{};
    PanelExternalWsStatus external_ws{};
    PanelExternalGameStatus external_game{};
    std::vector<PanelActivityEntry> recent_activity{};
};

} // namespace nlp3::platform
