#include "platform/external_game_state.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace {

std::string read_text_file(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

} // namespace

namespace nlp3::platform {

PanelExternalGameStatus read_external_game_bridge_state(
    const PanelExternalGameStatus& base_status) {
    auto status = base_status;
    const auto raw = read_text_file(base_status.bridge_state_file);
    if (raw.empty()) {
        return status;
    }

    try {
        const auto json = nlohmann::json::parse(raw);
        status.state = json.value("bridgeState", std::string{});
        status.last_error = json.value("lastError", std::string{});

        const auto process = json.value("process", nlohmann::json::object());
        status.game_running = process.value("running", false);
        status.game_process_id = process.value("processId", 0u);
        status.game_return_code = process.value("returnCode", 0);

        const auto runtime = json.value("runtime", nlohmann::json::object());
        status.last_status_type = runtime.value("lastStatusType", std::string{});
        status.last_status_timestamp_ms = runtime.value("lastStatusTimestampMs", static_cast<std::int64_t>(0));
        status.round_state = runtime.value("roundState", std::string{});
        status.mode_id = runtime.value("modeId", std::string{});

        status.ranking.clear();
        for (const auto& entry : runtime.value("ranking", nlohmann::json::array())) {
            status.ranking.push_back(ExternalGameRankingEntry{
                entry.value("rank", 0),
                entry.value("id", std::string{}),
                entry.value("name", std::string{}),
                entry.value("score", 0),
                entry.value("avatarUrl", std::string{}),
            });
        }

        status.feed.clear();
        for (const auto& item : runtime.value("feed", nlohmann::json::array())) {
            status.feed.push_back(item.get<std::string>());
        }

        status.achievements.clear();
        for (const auto& item : runtime.value("achievements", nlohmann::json::array())) {
            status.achievements.push_back(item.get<std::string>());
        }

        status.recent_log_lines.clear();
        for (const auto& item : runtime.value("recentLogs", nlohmann::json::array())) {
            status.recent_log_lines.push_back(item.get<std::string>());
        }

        if (status.recent_log_lines.empty()) {
            for (const auto& item : json.value("recentBridgeLogs", nlohmann::json::array())) {
                status.recent_log_lines.push_back(item.get<std::string>());
            }
        }
    } catch (...) {
        if (status.last_error.empty()) {
            status.last_error = "external_game_bridge_state_parse_failed";
        }
    }

    return status;
}

} // namespace nlp3::platform
