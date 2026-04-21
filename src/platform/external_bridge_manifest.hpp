#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nlp3::platform {

struct ExternalBridgeManifest {
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

} // namespace nlp3::platform
