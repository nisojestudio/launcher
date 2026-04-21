#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nlp3::platform {

struct ExternalGameRankingEntry {
    std::int32_t rank = 0;
    std::string player_id{};
    std::string player_name{};
    std::int32_t score = 0;
    std::string avatar_url{};
};

struct PanelExternalGameStatus {
    bool discovered = false;
    bool installed = false;
    bool active = false;
    std::string game_id{};
    std::string display_name{};
    std::string description{};
    std::string detected_type{};
    std::string module_root{};
    std::string entry_path{};
    std::string config_file{};
    std::string inbox_file{};
    std::string status_file{};
    std::string log_file{};
    std::string bridge_inbox_file{};
    std::string bridge_state_file{};
    std::string bridge_log_file{};
    bool bridge_running = false;
    std::uint32_t bridge_process_id = 0;
    bool bridge_has_exit_code = false;
    std::int32_t bridge_last_exit_code = 0;
    bool game_running = false;
    std::uint32_t game_process_id = 0;
    std::int32_t game_return_code = 0;
    std::string state{};
    std::string last_error{};
    std::string last_status_type{};
    std::string round_state{};
    std::string mode_id{};
    std::int64_t last_status_timestamp_ms = 0;
    std::vector<ExternalGameRankingEntry> ranking{};
    std::vector<std::string> feed{};
    std::vector<std::string> achievements{};
    std::vector<std::string> recent_log_lines{};
};

PanelExternalGameStatus read_external_game_bridge_state(
    const PanelExternalGameStatus& base_status);

} // namespace nlp3::platform
