#include "platform/panel_view_model_builder.hpp"

#include <string>
#include <utility>

#include "bridge/tiktok_bridge_session.hpp"

namespace {

std::string bool_text(bool value) {
    return value ? "yes" : "no";
}

std::string license_status_text(nlp3::platform::LicenseStatus status) {
    switch (status) {
    case nlp3::platform::LicenseStatus::inactive:
        return "inactive";
    case nlp3::platform::LicenseStatus::active:
        return "active";
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

std::string activity_prefix(nlp3::platform::PanelActivityKind kind) {
    switch (kind) {
    case nlp3::platform::PanelActivityKind::host_event:
        return "event";
    case nlp3::platform::PanelActivityKind::tts_chat_enqueued:
        return "tts_chat";
    case nlp3::platform::PanelActivityKind::tts_automation_enqueued:
        return "tts_auto";
    case nlp3::platform::PanelActivityKind::tts_periodic_enqueued:
        return "tts_periodic";
    case nlp3::platform::PanelActivityKind::unknown:
        break;
    }

    return "unknown";
}

std::string external_counts_text(const nlp3::platform::PanelExternalBridgeStatus& external_bridge) {
    return "chat=" + std::to_string(external_bridge.chat_events)
        + ", like=" + std::to_string(external_bridge.like_events)
        + ", gift=" + std::to_string(external_bridge.gift_events)
        + ", follow=" + std::to_string(external_bridge.follow_events)
        + ", share=" + std::to_string(external_bridge.share_events)
        + ", viewer_join=" + std::to_string(external_bridge.viewer_join_events)
        + ", viewer_count=" + std::to_string(external_bridge.viewer_count_events)
        + ", live_start=" + std::to_string(external_bridge.live_start_events)
        + ", live_end=" + std::to_string(external_bridge.live_end_events)
        + ", moderation=" + std::to_string(external_bridge.moderation_events)
        + ", custom_raw=" + std::to_string(external_bridge.custom_raw_events);
}

std::string format_activity_line(const nlp3::platform::PanelActivityEntry& entry) {
    std::string line = activity_prefix(entry.kind);

    if (!entry.label.empty()) {
        line += " | " + entry.label;
    }

    if (!entry.actor_name.empty()) {
        line += " | " + entry.actor_name;
    }

    if (!entry.details.empty()) {
        line += " | " + entry.details;
    }

    return line;
}

nlp3::platform::PanelViewSection make_section(
    std::string title,
    std::vector<nlp3::platform::PanelViewSectionItem> items) {
    return nlp3::platform::PanelViewSection{
        std::move(title),
        std::move(items),
    };
}

nlp3::platform::PanelViewAction make_action(
    nlp3::platform::PanelCommandKind command,
    std::string label,
    std::string argument_hint = {}) {
    return nlp3::platform::PanelViewAction{
        command,
        std::move(label),
        std::move(argument_hint),
    };
}

} // namespace

namespace nlp3::platform {

PanelViewModel PanelViewModelBuilder::build(const PanelSnapshot& snapshot) const {
    PanelViewModel view_model{};
    view_model.title = snapshot.panel_name;

    view_model.sections.push_back(make_section("Panel", {
        {"Name", snapshot.panel_name},
        {"Total events", std::to_string(snapshot.total_events)},
    }));

    view_model.sections.push_back(make_section("Bridge", {
        {"Mode", snapshot.bridge_mode.empty() ? "stub" : snapshot.bridge_mode},
        {"External mode", bool_text(snapshot.external_bridge.external_mode)},
        {"External WS", bool_text(snapshot.external_ws.running)},
        {"WS port", snapshot.external_ws.port == 0 ? "-" : std::to_string(snapshot.external_ws.port)},
        {"WS accepted", std::to_string(snapshot.external_ws.accepted_messages)},
        {"WS rejected", std::to_string(snapshot.external_ws.rejected_messages)},
        {"Recording", bool_text(snapshot.external_bridge.recording)},
        {"Recording path", snapshot.external_bridge.recording_path.empty()
            ? "-"
            : snapshot.external_bridge.recording_path},
        {"Last replay path", snapshot.external_bridge.last_replay_path.empty()
            ? "-"
            : snapshot.external_bridge.last_replay_path},
        {"Last replay accepted", std::to_string(snapshot.external_bridge.last_replay_accepted_events)},
        {"Total external submitted", std::to_string(snapshot.external_bridge.total_external_events_submitted)},
        {"Target user", snapshot.external_bridge.target_user.empty()
            ? "-"
            : snapshot.external_bridge.target_user},
        {"Connection state", snapshot.external_bridge.connection_state.empty()
            ? "-"
            : snapshot.external_bridge.connection_state},
        {"Last status", snapshot.external_bridge.last_status_message.empty()
            ? "-"
            : snapshot.external_bridge.last_status_message},
        {"Last status timestamp", snapshot.external_bridge.last_status_timestamp_ms == 0
            ? "-"
            : std::to_string(snapshot.external_bridge.last_status_timestamp_ms)},
        {"Current room", snapshot.external_bridge.current_room_id.empty()
            ? "-"
            : snapshot.external_bridge.current_room_id},
        {"Last event kind", snapshot.external_bridge.last_event_kind.empty()
            ? "-"
            : snapshot.external_bridge.last_event_kind},
        {"Last event actor", snapshot.external_bridge.last_event_actor.empty()
            ? "-"
            : snapshot.external_bridge.last_event_actor},
        {"Last event timestamp", snapshot.external_bridge.last_event_timestamp_ms == 0
            ? "-"
            : std::to_string(snapshot.external_bridge.last_event_timestamp_ms)},
        {"External counts", external_counts_text(snapshot.external_bridge)},
        {"Runner", bool_text(snapshot.external_bridge.runner_running)},
        {"Runner pid", snapshot.external_bridge.runner_process_id == 0
            ? "-"
            : std::to_string(snapshot.external_bridge.runner_process_id)},
        {"Runner ws", snapshot.external_bridge.runner_ws_url.empty()
            ? "-"
            : snapshot.external_bridge.runner_ws_url},
        {"Runner exit", snapshot.external_bridge.runner_has_exit_code
            ? std::to_string(snapshot.external_bridge.runner_last_exit_code)
            : "-"},
        {"Runner error", snapshot.external_bridge.runner_last_error.empty()
            ? "-"
            : snapshot.external_bridge.runner_last_error},
        {"Runner last log", snapshot.external_bridge.runner_recent_log_lines.empty()
            ? "-"
            : snapshot.external_bridge.runner_recent_log_lines.back()},
        {"Integrated", bool_text(snapshot.bridge.integrated)},
        {"State", std::string(nlp3::bridge::to_string(snapshot.bridge.state))},
        {"Raw received", std::to_string(snapshot.bridge.metrics.raw_events_received)},
        {"Raw emitted", std::to_string(snapshot.bridge.metrics.raw_events_emitted)},
        {"Faults", std::to_string(snapshot.bridge.metrics.fault_count)},
    }));

    view_model.sections.push_back(make_section("TTS", {
        {"Available", bool_text(snapshot.tts.available)},
        {"Queued", std::to_string(snapshot.tts.queued_messages)},
    }));

    view_model.sections.push_back(make_section("Game", {
        {"Active", bool_text(snapshot.game.has_active_game)},
        {"Game id", snapshot.game.active_game_id.empty() ? "none" : snapshot.game.active_game_id},
        {"Runtime", game_runtime_state_text(snapshot.game.runtime_state)},
        {"Error", snapshot.game.last_error.empty() ? "-" : snapshot.game.last_error},
    }));

    view_model.sections.push_back(make_section("License", {
        {"Status", license_status_text(snapshot.license.status)},
        {"Tier", snapshot.license.tier.empty() ? "-" : snapshot.license.tier},
        {"Message", snapshot.license.message.empty() ? "-" : snapshot.license.message},
    }));

    view_model.actions.push_back(make_action(PanelCommandKind::bridge_start, "Start bridge"));
    view_model.actions.push_back(make_action(PanelCommandKind::bridge_stop, "Stop bridge"));
    view_model.actions.push_back(make_action(PanelCommandKind::bridge_reset, "Reset bridge"));
    view_model.actions.push_back(make_action(PanelCommandKind::game_activate, "Activate game", "game_id"));
    view_model.actions.push_back(make_action(PanelCommandKind::game_deactivate, "Deactivate game"));
    view_model.actions.push_back(make_action(PanelCommandKind::game_restart, "Restart game"));
    view_model.actions.push_back(make_action(
        PanelCommandKind::tts_enqueue_announcement,
        "Queue announcement",
        "message"));

    view_model.recent_activity_lines.reserve(snapshot.recent_activity.size());
    for (const auto& entry : snapshot.recent_activity) {
        view_model.recent_activity_lines.push_back(format_activity_line(entry));
    }

    return view_model;
}

} // namespace nlp3::platform
