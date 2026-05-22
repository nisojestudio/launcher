#include "platform/panel_http_json.hpp"

#include <chrono>
#include <set>
#include <sstream>
#include <string_view>

#include "bridge/tiktok_bridge_session.hpp"
#include "gamesdk/game_catalog.hpp"
#include "gamesdk/game_runtime_controller.hpp"
#include "platform/panel_activity.hpp"
#include "platform/panel_app.hpp"
#include "platform/panel_diagnostics.hpp"
#include "platform/panel_view_model_builder.hpp"

namespace {

using nlp3::platform::PanelActivityEntry;
using nlp3::platform::PanelActivityKind;
using nlp3::platform::PanelDiagnosticLevel;
using nlp3::platform::PanelDiagnosticsReport;
using nlp3::platform::PanelExternalBridgeStatus;
using nlp3::platform::PanelExternalGameStatus;
using nlp3::platform::PanelExternalWsStatus;
using nlp3::platform::PanelAuthStatus;
using nlp3::platform::PanelHttpServerStatus;
using nlp3::platform::PanelLicenseStatus;
using nlp3::platform::PanelSnapshot;
using nlp3::platform::PanelViewModel;
using nlp3::platform::PanelViewSection;
using nlp3::platform::PanelViewSectionItem;

std::int64_t now_wall_clock_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string escape_json_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for (const auto ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20u) {
                escaped += '?';
            } else {
                escaped.push_back(ch);
            }
            break;
        }
    }
    return escaped;
}

std::string quote(std::string_view value) {
    return "\"" + escape_json_string(value) + "\"";
}

std::string bool_json(bool value) {
    return value ? "true" : "false";
}

std::string activity_kind_text(PanelActivityKind kind) {
    switch (kind) {
    case PanelActivityKind::host_event:
        return "host_event";
    case PanelActivityKind::tts_chat_enqueued:
        return "tts_chat";
    case PanelActivityKind::tts_automation_enqueued:
        return "tts_auto";
    case PanelActivityKind::tts_periodic_enqueued:
        return "tts_periodic";
    case PanelActivityKind::unknown:
        break;
    }
    return "unknown";
}

std::string diagnostic_level_text(PanelDiagnosticLevel level) {
    switch (level) {
    case PanelDiagnosticLevel::info:
        return "info";
    case PanelDiagnosticLevel::warning:
        return "warning";
    case PanelDiagnosticLevel::error:
        return "error";
    }
    return "info";
}

std::string license_status_text(nlp3::platform::LicenseStatus status) {
    switch (status) {
    case nlp3::platform::LicenseStatus::active:
        return "active";
    case nlp3::platform::LicenseStatus::inactive:
        return "inactive";
    case nlp3::platform::LicenseStatus::unknown:
        break;
    }
    return "unknown";
}

std::string game_runtime_state_text(nlp3::gamesdk::GameRuntimeState state) {
    switch (state) {
    case nlp3::gamesdk::GameRuntimeState::idle:
        return "idle";
    case nlp3::gamesdk::GameRuntimeState::activating:
        return "activating";
    case nlp3::gamesdk::GameRuntimeState::active:
        return "active";
    case nlp3::gamesdk::GameRuntimeState::paused:
        return "paused";
    case nlp3::gamesdk::GameRuntimeState::faulted:
        return "faulted";
    }
    return "unknown";
}

std::string activity_json(const PanelActivityEntry& entry) {
    return "{"
        "\"kind\":" + quote(activity_kind_text(entry.kind)) + ","
        "\"label\":" + quote(entry.label) + ","
        "\"source\":" + quote(entry.source) + ","
        "\"actorName\":" + quote(entry.actor_name) + ","
        "\"details\":" + quote(entry.details) + ","
        "\"timestampMs\":" + std::to_string(entry.timestamp_ms)
        + "}";
}

std::string diagnostics_json(const PanelDiagnosticsReport& report) {
    std::ostringstream output;
    output << "{"
           << "\"ok\":" << bool_json(report.ok) << ","
           << "\"entries\":[";
    for (std::size_t index = 0; index < report.entries.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        const auto& entry = report.entries[index];
        output << "{"
               << "\"level\":" << quote(diagnostic_level_text(entry.level)) << ","
               << "\"code\":" << quote(entry.code) << ","
               << "\"message\":" << quote(entry.message)
               << "}";
    }
    output << "]}";
    return output.str();
}

std::string view_model_json(const PanelViewModel& view_model) {
    std::ostringstream output;
    output << "{"
           << "\"title\":" << quote(view_model.title) << ","
           << "\"sections\":[";
    for (std::size_t section_index = 0; section_index < view_model.sections.size(); ++section_index) {
        if (section_index > 0) {
            output << ",";
        }
        const auto& section = view_model.sections[section_index];
        output << "{"
               << "\"title\":" << quote(section.title) << ","
               << "\"items\":[";
        for (std::size_t item_index = 0; item_index < section.items.size(); ++item_index) {
            if (item_index > 0) {
                output << ",";
            }
            const auto& item = section.items[item_index];
            output << "{"
                   << "\"label\":" << quote(item.label) << ","
                   << "\"value\":" << quote(item.value)
                   << "}";
        }
        output << "]}";
    }
    output << "],\"recentActivityLines\":[";
    for (std::size_t index = 0; index < view_model.recent_activity_lines.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << quote(view_model.recent_activity_lines[index]);
    }
    output << "]}";
    return output.str();
}

std::string external_bridge_json(const PanelExternalBridgeStatus& status) {
    std::ostringstream output;
    output << "{"
           << "\"externalMode\":" << bool_json(status.external_mode) << ","
           << "\"recording\":" << bool_json(status.recording) << ","
           << "\"recordingPath\":" << quote(status.recording_path) << ","
           << "\"lastReplayPath\":" << quote(status.last_replay_path) << ","
           << "\"lastReplayAcceptedEvents\":" << status.last_replay_accepted_events << ","
           << "\"totalExternalEventsSubmitted\":" << status.total_external_events_submitted << ","
           << "\"targetUser\":" << quote(status.target_user) << ","
           << "\"connectionState\":" << quote(status.connection_state) << ","
           << "\"lastStatusMessage\":" << quote(status.last_status_message) << ","
           << "\"lastStatusTimestampMs\":" << status.last_status_timestamp_ms << ","
           << "\"currentRoomId\":" << quote(status.current_room_id) << ","
           << "\"lastEventKind\":" << quote(status.last_event_kind) << ","
           << "\"lastEventActor\":" << quote(status.last_event_actor) << ","
           << "\"lastEventTimestampMs\":" << status.last_event_timestamp_ms << ","
           << "\"chatEvents\":" << status.chat_events << ","
           << "\"likeEvents\":" << status.like_events << ","
           << "\"giftEvents\":" << status.gift_events << ","
           << "\"followEvents\":" << status.follow_events << ","
           << "\"shareEvents\":" << status.share_events << ","
           << "\"viewerJoinEvents\":" << status.viewer_join_events << ","
           << "\"viewerCountEvents\":" << status.viewer_count_events << ","
           << "\"liveStartEvents\":" << status.live_start_events << ","
           << "\"liveEndEvents\":" << status.live_end_events << ","
           << "\"moderationEvents\":" << status.moderation_events << ","
           << "\"customRawEvents\":" << status.custom_raw_events << ","
           << "\"runnerRunning\":" << bool_json(status.runner_running) << ","
           << "\"runnerProcessId\":" << status.runner_process_id << ","
           << "\"runnerWsUrl\":" << quote(status.runner_ws_url) << ","
           << "\"runtimeChecked\":" << bool_json(status.runtime_checked) << ","
           << "\"runtimeReady\":" << bool_json(status.runtime_ready) << ","
           << "\"runtimeCheckedTimestampMs\":" << status.runtime_checked_timestamp_ms << ","
           << "\"runtimeSummary\":" << quote(status.runtime_summary) << ","
           << "\"runtimeAlerts\":[";
    for (std::size_t index = 0; index < status.runtime_alerts.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << quote(status.runtime_alerts[index]);
    }
    output << "],"
           << "\"runnerHasExitCode\":" << bool_json(status.runner_has_exit_code) << ","
           << "\"runnerLastExitCode\":" << status.runner_last_exit_code << ","
           << "\"runnerLastError\":" << quote(status.runner_last_error) << ","
           << "\"runnerRecentLogLines\":[";
    for (std::size_t index = 0; index < status.runner_recent_log_lines.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << quote(status.runner_recent_log_lines[index]);
    }
    output << "]}";
    return output.str();
}

std::string external_ws_json(const PanelExternalWsStatus& status) {
    return "{"
        "\"running\":" + bool_json(status.running) + ","
        "\"port\":" + std::to_string(status.port) + ","
        "\"acceptedMessages\":" + std::to_string(status.accepted_messages) + ","
        "\"rejectedMessages\":" + std::to_string(status.rejected_messages)
        + "}";
}

std::string external_game_json(const PanelExternalGameStatus& status) {
    std::ostringstream output;
    output << "{"
           << "\"discovered\":" << bool_json(status.discovered) << ","
           << "\"installed\":" << bool_json(status.installed) << ","
           << "\"active\":" << bool_json(status.active) << ","
           << "\"gameId\":" << quote(status.game_id) << ","
           << "\"displayName\":" << quote(status.display_name) << ","
           << "\"description\":" << quote(status.description) << ","
           << "\"detectedType\":" << quote(status.detected_type) << ","
           << "\"moduleRoot\":" << quote(status.module_root) << ","
           << "\"entryPath\":" << quote(status.entry_path) << ","
           << "\"configFile\":" << quote(status.config_file) << ","
           << "\"inboxFile\":" << quote(status.inbox_file) << ","
           << "\"statusFile\":" << quote(status.status_file) << ","
           << "\"logFile\":" << quote(status.log_file) << ","
           << "\"bridgeInboxFile\":" << quote(status.bridge_inbox_file) << ","
           << "\"bridgeStateFile\":" << quote(status.bridge_state_file) << ","
           << "\"bridgeLogFile\":" << quote(status.bridge_log_file) << ","
           << "\"bridgeRunning\":" << bool_json(status.bridge_running) << ","
           << "\"bridgeProcessId\":" << status.bridge_process_id << ","
           << "\"bridgeHasExitCode\":" << bool_json(status.bridge_has_exit_code) << ","
           << "\"bridgeLastExitCode\":" << status.bridge_last_exit_code << ","
           << "\"gameRunning\":" << bool_json(status.game_running) << ","
           << "\"gameProcessId\":" << status.game_process_id << ","
           << "\"gameReturnCode\":" << status.game_return_code << ","
           << "\"state\":" << quote(status.state) << ","
           << "\"lastError\":" << quote(status.last_error) << ","
           << "\"lastStatusType\":" << quote(status.last_status_type) << ","
           << "\"roundState\":" << quote(status.round_state) << ","
           << "\"modeId\":" << quote(status.mode_id) << ","
           << "\"lastStatusTimestampMs\":" << status.last_status_timestamp_ms << ","
           << "\"ranking\":[";
    for (std::size_t index = 0; index < status.ranking.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        const auto& entry = status.ranking[index];
        output << "{"
               << "\"rank\":" << entry.rank << ","
               << "\"playerId\":" << quote(entry.player_id) << ","
               << "\"playerName\":" << quote(entry.player_name) << ","
               << "\"score\":" << entry.score << ","
               << "\"avatarUrl\":" << quote(entry.avatar_url)
               << "}";
    }
    output << "],\"feed\":[";
    for (std::size_t index = 0; index < status.feed.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << quote(status.feed[index]);
    }
    output << "],\"achievements\":[";
    for (std::size_t index = 0; index < status.achievements.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << quote(status.achievements[index]);
    }
    output << "],\"recentLogs\":[";
    for (std::size_t index = 0; index < status.recent_log_lines.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << quote(status.recent_log_lines[index]);
    }
    output << "]}";
    return output.str();
}

std::string license_json(const PanelLicenseStatus& status) {
    return "{"
        "\"status\":" + quote(license_status_text(status.status)) + ","
        "\"tier\":" + quote(status.tier) + ","
        "\"message\":" + quote(status.message)
        + "}";
}

std::string auth_json(const PanelAuthStatus& status) {
    return "{"
        "\"required\":" + bool_json(status.required) + ","
        "\"authenticated\":" + bool_json(status.authenticated) + ","
        "\"email\":" + quote(status.email) + ","
        "\"firebaseUid\":" + quote(status.firebase_uid) + ","
        "\"licenseKey\":" + quote(status.license_key) + ","
        "\"message\":" + quote(status.message) + ","
        "\"lastErrorCode\":" + quote(status.last_error_code) + ","
        "\"lastValidatedTimestampMs\":" + std::to_string(status.last_validated_timestamp_ms)
        + "}";
}

std::string snapshot_json(const PanelSnapshot& snapshot) {
    std::ostringstream output;
    output << "{"
           << "\"panelName\":" << quote(snapshot.panel_name) << ","
           << "\"panelVersion\":" << quote(snapshot.panel_version) << ","
           << "\"latestVersion\":" << quote(snapshot.latest_version) << ","
           << "\"latestInstallerUrl\":" << quote(snapshot.latest_installer_url) << ","
           << "\"bridgeMode\":" << quote(snapshot.bridge_mode) << ","
           << "\"totalEvents\":" << snapshot.total_events << ","
           << "\"bridge\":{"
           << "\"provider\":" << quote(snapshot.bridge.provider) << ","
           << "\"state\":" << quote(nlp3::bridge::to_string(snapshot.bridge.state)) << ","
           << "\"integrated\":" << bool_json(snapshot.bridge.integrated) << ","
           << "\"metrics\":{"
           << "\"pollCalls\":" << snapshot.bridge.metrics.poll_calls << ","
           << "\"emptyPolls\":" << snapshot.bridge.metrics.empty_polls << ","
           << "\"rawEventsReceived\":" << snapshot.bridge.metrics.raw_events_received << ","
           << "\"rawEventsEmitted\":" << snapshot.bridge.metrics.raw_events_emitted << ","
           << "\"rawEventsDropped\":" << snapshot.bridge.metrics.raw_events_dropped << ","
           << "\"faultCount\":" << snapshot.bridge.metrics.fault_count << ","
           << "\"lastEventTimestampMs\":" << snapshot.bridge.metrics.last_event_timestamp_ms
           << "},"
           << "\"lastFault\":";
    if (snapshot.bridge.last_fault.has_value()) {
        output << "{"
               << "\"code\":" << quote(snapshot.bridge.last_fault->code) << ","
               << "\"message\":" << quote(snapshot.bridge.last_fault->message)
               << "}";
    } else {
        output << "null";
    }
    output << "},"
           << "\"tts\":{"
           << "\"available\":" << bool_json(snapshot.tts.available) << ","
           << "\"queuedMessages\":" << snapshot.tts.queued_messages
           << "},"
           << "\"game\":{"
           << "\"hasActiveGame\":" << bool_json(snapshot.game.has_active_game) << ","
           << "\"activeGameId\":" << quote(snapshot.game.active_game_id) << ","
           << "\"runtimeState\":" << quote(game_runtime_state_text(snapshot.game.runtime_state)) << ","
           << "\"lastError\":" << quote(snapshot.game.last_error)
           << "},"
           << "\"license\":" << license_json(snapshot.license) << ","
           << "\"auth\":" << auth_json(snapshot.auth) << ","
           << "\"externalBridge\":" << external_bridge_json(snapshot.external_bridge) << ","
           << "\"externalWs\":" << external_ws_json(snapshot.external_ws) << ","
           << "\"externalGame\":" << external_game_json(snapshot.external_game) << ","
           << "\"recentActivity\":[";
    for (std::size_t index = 0; index < snapshot.recent_activity.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << activity_json(snapshot.recent_activity[index]);
    }
    output << "]}";
    return output.str();
}

std::string http_status_json(const PanelHttpServerStatus& status) {
    return "{"
        "\"running\":" + bool_json(status.running) + ","
        "\"port\":" + std::to_string(status.port) + ","
        "\"requestsServed\":" + std::to_string(status.requests_served) + ","
        "\"lastError\":" + quote(status.last_error)
        + "}";
}

std::string host_session_json(const nlp3::host::HostSessionSnapshot& session) {
    std::ostringstream output;
    output << "{"
           << "\"totalEvents\":" << session.total_events << ","
           << "\"chatMessages\":" << session.chat_messages << ","
           << "\"likes\":" << session.likes << ","
           << "\"gifts\":" << session.gifts << ","
           << "\"follows\":" << session.follows << ","
           << "\"shares\":" << session.shares << ","
           << "\"viewerJoins\":" << session.viewer_joins << ","
           << "\"viewerCountUpdates\":" << session.viewer_count_updates << ","
           << "\"liveStarts\":" << session.live_starts << ","
           << "\"liveEnds\":" << session.live_ends << ","
           << "\"moderationEvents\":" << session.moderation_events << ","
           << "\"customEvents\":" << session.custom_events << ","
           << "\"lastActor\":";
    if (session.last_actor.has_value()) {
        output << "{"
               << "\"id\":" << quote(session.last_actor->id) << ","
               << "\"displayName\":" << quote(session.last_actor->display_name) << ","
               << "\"avatarUrl\":" << quote(session.last_actor->avatar_url)
               << "}";
    } else {
        output << "null";
    }
    output << ",\"lastEvent\":";
    if (session.last_event.has_value()) {
        output << "{"
               << "\"kind\":" << quote(nlp3::events::to_string(session.last_event->kind)) << ","
               << "\"message\":" << quote(session.last_event->message) << ","
               << "\"viewerCount\":" << session.last_event->viewer_count << ","
               << "\"magnitude\":" << session.last_event->magnitude
               << "}";
    } else {
        output << "null";
    }
    output << "}";
    return output.str();
}

std::string host_controls_json(const nlp3::platform::PanelApp& app) {
    const auto automation = app.host_automation_config();
    const auto periodic = app.host_periodic_tts_config();
    const auto& config = app.config();
    const auto tts_runtime = app.host_tts_runtime_config();
    const auto voices = app.tts_voice_catalog();

    std::ostringstream output;
    output << "{"
           << "\"ttsBackendName\":" << quote(app.tts_backend_name()) << ","
           << "\"ttsBackendAvailable\":" << bool_json(app.tts_backend_available()) << ","
           << "\"ttsEnabled\":" << bool_json(tts_runtime.enabled) << ","
           << "\"voiceId\":" << quote(tts_runtime.selected_voice_id) << ","
           << "\"voiceLanguage\":" << quote(tts_runtime.selected_language) << ","
           << "\"voiceFrequency\":" << quote(tts_runtime.frequency) << ","
           << "\"energyLevel\":" << quote(config.host_energy_level) << ","
           << "\"toneStyle\":" << quote(config.host_tone_style) << ","
           << "\"allowChatMessages\":" << bool_json(config.tts.allow_chat_messages) << ","
           << "\"chatFilterMode\":" << quote(std::string(nlp3::tts::to_string(config.tts.chat_filter_mode))) << ","
           << "\"chatMessageTemplate\":" << quote(config.tts.chat_message_template) << ","
           << "\"giftThanksEnabled\":" << bool_json(automation.enable_gift_thanks_tts) << ","
           << "\"followThanksEnabled\":" << bool_json(automation.enable_follow_thanks_tts) << ","
           << "\"likeThanksEnabled\":" << bool_json(automation.enable_like_thanks_tts) << ","
           << "\"subscriberThanksEnabled\":" << bool_json(automation.enable_subscriber_thanks_tts) << ","
           << "\"shareThanksEnabled\":" << bool_json(automation.enable_share_thanks_tts) << ","
           << "\"giftThanksTemplate\":" << quote(automation.gift_thanks_template) << ","
           << "\"followThanksTemplate\":" << quote(automation.follow_thanks_template) << ","
           << "\"likeThanksTemplate\":" << quote(automation.like_thanks_template) << ","
           << "\"subscriberThanksTemplate\":" << quote(automation.subscriber_thanks_template) << ","
           << "\"shareThanksTemplate\":" << quote(automation.share_thanks_template) << ","
           << "\"periodicEnabled\":" << bool_json(periodic.enabled) << ","
           << "\"periodicIntervalMs\":" << periodic.interval_ms << ","
           << "\"periodicMessages\":[";
    for (std::size_t index = 0; index < periodic.messages.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << quote(periodic.messages[index]);
    }
    output << "],\"voiceCatalog\":[";
    for (std::size_t index = 0; index < voices.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << "{"
               << "\"id\":" << quote(voices[index].id) << ","
               << "\"displayName\":" << quote(voices[index].display_name) << ","
               << "\"language\":" << quote(voices[index].language) << ","
               << "\"gender\":" << quote(voices[index].gender) << ","
               << "\"available\":" << bool_json(voices[index].available)
               << "}";
    }
    output << "]}";
    return output.str();
}

std::string capabilities_json(const nlp3::gamesdk::GameCapabilities& capabilities) {
    return "{"
        "\"usesChatMessages\":" + bool_json(capabilities.uses_chat_messages) + ","
        "\"usesGifts\":" + bool_json(capabilities.uses_gifts) + ","
        "\"usesFollows\":" + bool_json(capabilities.uses_follows) + ","
        "\"usesShares\":" + bool_json(capabilities.uses_shares) + ","
        "\"usesViewerJoins\":" + bool_json(capabilities.uses_viewer_joins) + ","
        "\"usesAvatarData\":" + bool_json(capabilities.uses_avatar_data) + ","
        "\"usesTts\":" + bool_json(capabilities.uses_tts)
        + "}";
}

std::string manifest_json(const nlp3::gamesdk::GameManifest& manifest) {
    return "{"
        "\"gameId\":" + quote(manifest.game_id) + ","
        "\"displayName\":" + quote(manifest.display_name) + ","
        "\"version\":" + quote(manifest.version) + ","
        "\"description\":" + quote(manifest.description) + ","
        "\"author\":" + quote(manifest.author) + ","
        "\"capabilities\":" + capabilities_json(manifest.capabilities)
        + "}";
}

std::string available_games_json(const nlp3::platform::PanelApp& app) {
    const auto games = app.available_games();
    std::ostringstream output;
    output << "{"
           << "\"registeredCount\":" << games.size() << ","
           << "\"items\":[";
    for (std::size_t index = 0; index < games.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        const auto& entry = games[index];
        output << "{"
               << "\"gameId\":" << quote(entry.game_id) << ","
               << "\"displayName\":" << quote(entry.display_name) << ","
               << "\"version\":" << quote(entry.version) << ","
               << "\"source\":" << quote(entry.source) << ","
               << "\"installed\":" << bool_json(entry.installed) << ","
                << "\"enabled\":" << bool_json(entry.enabled) << ","
               << "\"updateAvailable\":" << bool_json(entry.update_available) << ","
               << "\"installState\":" << quote(entry.install_state) << ","
               << "\"installMessage\":" << quote(entry.install_message) << ","
               << "\"installProgressPercent\":" << entry.install_progress_percent << ","
               << "\"manifest\":" << manifest_json(entry.manifest)
               << "}";
    }
    output << "]}";
    return output.str();
}

std::string game_telemetry_json(const std::vector<nlp3::gamesdk::GameTelemetryItem>& telemetry) {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < telemetry.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        const auto& item = telemetry[index];
        output << "{"
               << "\"key\":" << quote(item.key) << ","
               << "\"label\":" << quote(item.label) << ","
               << "\"value\":" << quote(item.value) << ","
               << "\"tone\":" << quote(item.tone)
               << "}";
    }
    output << "]";
    return output.str();
}

std::string active_game_json(const nlp3::platform::PanelApp& app, const PanelSnapshot& snapshot) {
    if (snapshot.external_game.active) {
        std::ostringstream telemetry;
        telemetry << "[";
        for (std::size_t index = 0; index < snapshot.external_game.ranking.size() && index < 3; ++index) {
            if (index > 0) {
                telemetry << ",";
            }
            const auto& entry = snapshot.external_game.ranking[index];
            telemetry << "{"
                      << "\"key\":" << quote("rank_" + std::to_string(entry.rank)) << ","
                      << "\"label\":" << quote("Top " + std::to_string(entry.rank)) << ","
                      << "\"value\":" << quote(entry.player_name + " · " + std::to_string(entry.score)) << ","
                      << "\"tone\":" << quote(entry.rank == 1 ? "positive" : "neutral")
                      << "}";
        }
        telemetry << "]";

        return "{"
            "\"manifest\":{"
            "\"gameId\":" + quote(snapshot.external_game.game_id) + ","
            "\"displayName\":" + quote(snapshot.external_game.display_name) + ","
            "\"version\":" + quote("external") + ","
            "\"description\":" + quote(snapshot.external_game.description) + ","
            "\"author\":" + quote("Nisoje external module") + ","
            "\"capabilities\":{"
            "\"usesChatMessages\":true,"
            "\"usesGifts\":true,"
            "\"usesFollows\":true,"
            "\"usesShares\":true,"
            "\"usesViewerJoins\":true,"
            "\"usesAvatarData\":true,"
            "\"usesTts\":false"
            "}"
            "},"
            "\"telemetry\":" + telemetry.str() + ","
            "\"runtimeState\":" + quote(game_runtime_state_text(snapshot.game.runtime_state)) + ","
            "\"activeGameId\":" + quote(snapshot.external_game.game_id) + ","
            "\"hasActiveGame\":true,"
            "\"canPause\":false,"
            "\"canResume\":false,"
            "\"canReset\":" + bool_json(snapshot.external_game.discovered) + ","
            "\"external\":true"
            + "}";
    }

    const auto manifest = app.active_game_manifest();
    const auto telemetry = app.active_game_telemetry();
    return "{"
        "\"manifest\":" + manifest_json(manifest) + ","
        "\"telemetry\":" + game_telemetry_json(telemetry) + ","
        "\"runtimeState\":" + quote(game_runtime_state_text(snapshot.game.runtime_state)) + ","
        "\"activeGameId\":" + quote(snapshot.game.active_game_id) + ","
        "\"hasActiveGame\":" + bool_json(snapshot.game.has_active_game) + ","
        "\"canPause\":" + bool_json(snapshot.game.runtime_state == nlp3::gamesdk::GameRuntimeState::active) + ","
        "\"canResume\":" + bool_json(snapshot.game.runtime_state == nlp3::gamesdk::GameRuntimeState::paused) + ","
        "\"canReset\":" + bool_json(snapshot.game.has_active_game) + ","
        "\"external\":false"
        + "}";
}

std::string metrics_json(const nlp3::platform::PanelApp& app, const PanelSnapshot& snapshot) {
    const auto session = app.host_session_snapshot();
    const auto now_ms = now_wall_clock_ms();
    std::size_t events_per_minute = 0;
    std::size_t messages_per_minute = 0;
    std::size_t gifts_per_minute = 0;
    std::size_t unique_players = 0;
    std::set<std::string> actors;

    for (const auto& entry : snapshot.recent_activity) {
        if (entry.timestamp_ms <= 0) {
            continue;
        }
        if ((now_ms - entry.timestamp_ms) > 60000) {
            continue;
        }
        if (entry.kind == PanelActivityKind::host_event) {
            ++events_per_minute;
            if (entry.label == "chat_message") {
                ++messages_per_minute;
            }
            if (entry.label == "gift") {
                ++gifts_per_minute;
            }
            if (!entry.actor_name.empty()) {
                actors.insert(entry.actor_name);
            }
        }
    }
    unique_players = actors.size();

    const auto ws_total = snapshot.external_ws.accepted_messages + snapshot.external_ws.rejected_messages;
    const auto error_rate = ws_total == 0
        ? 0.0
        : (static_cast<double>(snapshot.external_ws.rejected_messages) * 100.0 / static_cast<double>(ws_total));

    const auto last_event_timestamp = snapshot.external_bridge.last_event_timestamp_ms != 0
        ? snapshot.external_bridge.last_event_timestamp_ms
        : static_cast<std::int64_t>(snapshot.bridge.metrics.last_event_timestamp_ms);
    const auto latency_ms = last_event_timestamp > 0 && now_ms > last_event_timestamp
        ? static_cast<std::uint64_t>(now_ms - last_event_timestamp)
        : 0ull;

    std::ostringstream output;
    output << "{"
           << "\"uptimeMs\":" << app.uptime_ms() << ","
           << "\"eventsPerMinute\":" << events_per_minute << ","
           << "\"messagesPerMinute\":" << messages_per_minute << ","
           << "\"giftsPerMinute\":" << gifts_per_minute << ","
           << "\"activePlayers\":" << unique_players << ","
           << "\"pipelineLatencyMs\":" << latency_ms << ","
           << "\"errorRate\":" << error_rate << ","
           << "\"bridge\":{"
           << "\"pollCalls\":" << snapshot.bridge.metrics.poll_calls << ","
           << "\"emptyPolls\":" << snapshot.bridge.metrics.empty_polls << ","
           << "\"rawEventsReceived\":" << snapshot.bridge.metrics.raw_events_received << ","
           << "\"rawEventsEmitted\":" << snapshot.bridge.metrics.raw_events_emitted << ","
           << "\"rawEventsDropped\":" << snapshot.bridge.metrics.raw_events_dropped << ","
           << "\"faultCount\":" << snapshot.bridge.metrics.fault_count
           << "},"
           << "\"hostSession\":" << host_session_json(session)
           << "}";
    return output.str();
}

std::string events_json(const PanelSnapshot& snapshot) {
    std::ostringstream output;
    output << "{"
           << "\"total\":" << snapshot.recent_activity.size() << ","
           << "\"items\":[";
    for (std::size_t index = 0; index < snapshot.recent_activity.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << activity_json(snapshot.recent_activity[index]);
    }
    output << "]}";
    return output.str();
}

std::string realtime_json(const nlp3::platform::PanelApp& app, const PanelSnapshot& snapshot) {
    return "{"
        "\"metrics\":" + metrics_json(app, snapshot) + ","
        "\"events\":" + events_json(snapshot)
        + "}";
}

std::string system_json(const nlp3::platform::PanelApp& app, const PanelSnapshot& snapshot) {
    const auto diagnostics = app.diagnostics();
    const auto system_status = diagnostics.ok
        ? "ok"
        : (snapshot.bridge.last_fault.has_value() ? "error" : "warning");
    return "{"
        "\"uptimeMs\":" + std::to_string(app.uptime_ms()) + ","
        "\"status\":" + quote(system_status) + ","
        "\"roomId\":" + quote(snapshot.external_bridge.current_room_id) + ","
        "\"pipelineState\":" + quote(std::string(nlp3::bridge::to_string(snapshot.bridge.state))) + ","
        "\"bridgeMode\":" + quote(snapshot.bridge_mode)
        + "}";
}

} // namespace

namespace nlp3::platform {

std::string build_panel_http_state_json(
    const PanelApp& app,
    const PanelHttpServerStatus& http_status) {
    const auto snapshot = app.snapshot();
    const auto diagnostics = app.diagnostics();
    const auto view_model = PanelViewModelBuilder{}.build(snapshot);

    return "{"
        "\"snapshot\":" + snapshot_json(snapshot) + ","
        "\"diagnostics\":" + diagnostics_json(diagnostics) + ","
        "\"viewModel\":" + view_model_json(view_model) + ","
        "\"http\":" + http_status_json(http_status) + ","
        "\"system\":" + system_json(app, snapshot) + ","
        "\"host\":" + host_controls_json(app) + ","
        "\"catalog\":" + available_games_json(app) + ","
        "\"gameDetail\":" + active_game_json(app, snapshot) + ","
        "\"metrics\":" + metrics_json(app, snapshot) + ","
        "\"events\":" + events_json(snapshot)
        + "}";
}

std::string build_panel_http_events_json(const PanelApp& app) {
    return events_json(app.snapshot());
}

std::string build_panel_http_metrics_json(const PanelApp& app) {
    return metrics_json(app, app.snapshot());
}

std::string build_panel_http_realtime_json(const PanelApp& app) {
    const auto snapshot = app.snapshot();
    return realtime_json(app, snapshot);
}

std::string build_panel_http_tts_json(const PanelApp& app) {
    return host_controls_json(app);
}

std::string build_panel_http_command_json(
    bool recognized,
    const std::string& output) {
    const auto ok = recognized && output.rfind("error:", 0) != 0;
    return "{"
        "\"recognized\":" + bool_json(recognized) + ","
        "\"ok\":" + bool_json(ok) + ","
        "\"output\":" + quote(output)
        + "}";
}

std::string build_panel_http_error_json(const std::string& message) {
    return "{"
        "\"ok\":false,"
        "\"error\":" + quote(message)
        + "}";
}

} // namespace nlp3::platform
