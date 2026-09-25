#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "bridge/tiktok_external_event_codec.hpp"
#include "bridge/tiktok_external_event_replay.hpp"
#include "bridge/tiktok_external_session_status.hpp"
#include "platform/panel_app.hpp"
#include "platform/panel_console.hpp"
#include "platform/panel_view_model_builder.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

int main() {
    nlp3::platform::PanelConfig config{};
    config.bridge_mode = "external";
    const auto config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_external_core_test_config.json",
        config);

    nlp3::platform::PanelApp panel_app;
    NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));

    const auto initial_snapshot = panel_app.snapshot();
    NLP3_TEST_REQUIRE(panel_app.is_external_bridge_mode());
    const auto initial_manifest = panel_app.external_bridge_manifest();
    NLP3_TEST_REQUIRE(initial_manifest.external_mode);
    NLP3_TEST_REQUIRE(!initial_manifest.recording);
    NLP3_TEST_REQUIRE(initial_manifest.recording_path.empty());
    NLP3_TEST_REQUIRE(initial_manifest.last_replay_path.empty());
    NLP3_TEST_REQUIRE(initial_manifest.last_replay_accepted_events == 0);
    NLP3_TEST_REQUIRE(initial_manifest.total_external_events_submitted == 0);
    NLP3_TEST_REQUIRE(initial_manifest.current_room_id.empty());
    NLP3_TEST_REQUIRE(initial_manifest.last_event_kind.empty());
    NLP3_TEST_REQUIRE(initial_manifest.last_event_actor.empty());
    NLP3_TEST_REQUIRE(initial_manifest.last_event_timestamp_ms == 0);
    NLP3_TEST_REQUIRE(initial_snapshot.bridge_mode == "external");
    NLP3_TEST_REQUIRE(initial_snapshot.bridge.integrated);
    NLP3_TEST_REQUIRE(initial_snapshot.bridge.state == nlp3::bridge::TikTokBridgeSessionState::running);

    NLP3_TEST_REQUIRE(panel_app.submit_external_bridge_event(nlp3::testsupport::make_chat_event(
        "external-panel-user-01",
        "alice",
        "Alice",
        "evt-external-panel-chat-001",
        "room-external-panel-001",
        "Hola panel external",
        1710000005000,
        "https://cdn.example.com/avatar-external-panel-alice.png")));
    const auto tick_result = panel_app.tick(0);
    NLP3_TEST_REQUIRE(tick_result.now_ms == 0);
    NLP3_TEST_REQUIRE(tick_result.bridge_events_processed == 1);
    NLP3_TEST_REQUIRE(!tick_result.periodic_tts_enqueued);

    const auto snapshot_after_event = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_event.total_events == 1);
    NLP3_TEST_REQUIRE(!snapshot_after_event.recent_activity.empty());
    NLP3_TEST_REQUIRE(snapshot_after_event.bridge.metrics.raw_events_received == 1);
    NLP3_TEST_REQUIRE(snapshot_after_event.bridge.metrics.raw_events_emitted == 1);
    NLP3_TEST_REQUIRE(snapshot_after_event.external_bridge.total_external_events_submitted == 1);
    NLP3_TEST_REQUIRE(snapshot_after_event.external_bridge.current_room_id == "room-external-panel-001");
    NLP3_TEST_REQUIRE(snapshot_after_event.external_bridge.last_event_kind == "chat");
    NLP3_TEST_REQUIRE(snapshot_after_event.external_bridge.last_event_actor == "Alice");
    NLP3_TEST_REQUIRE(snapshot_after_event.external_bridge.last_event_timestamp_ms == 1710000005000);
    NLP3_TEST_REQUIRE(snapshot_after_event.external_bridge.chat_events == 1);

    std::istringstream console_input;
    std::ostringstream console_output;
    nlp3::platform::PanelConsole panel_console{
        &panel_app,
        &console_input,
        &console_output,
    };

    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge inject chat alice hola desde consola"));
    NLP3_TEST_REQUIRE(panel_app.tick_bridge() == 0);
    const auto snapshot_after_console = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_console.total_events == 2);
    NLP3_TEST_REQUIRE(console_output.str().find("bridge chat injected and processed") != std::string::npos);

    const nlp3::bridge::TikTokExternalEventCodec codec{};
    const auto inject_json_command =
        std::string{"bridge inject json "} + codec.encode_json(nlp3::testsupport::make_chat_event(
            "external-console-user-01",
            "json_alice",
            "Json Alice",
            "evt-external-console-chat-001",
            "room-external-console-001",
            "Hola json panel",
            1710000005001,
            "https://cdn.example.com/avatar-json-alice.png"));
    NLP3_TEST_REQUIRE(panel_console.execute_line(inject_json_command));
    const auto snapshot_after_json = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_json.total_events == 3);
    NLP3_TEST_REQUIRE(snapshot_after_json.external_bridge.total_external_events_submitted == 3);
    NLP3_TEST_REQUIRE(snapshot_after_json.external_bridge.chat_events == 3);
    NLP3_TEST_REQUIRE(snapshot_after_json.external_bridge.follow_events == 0);
    NLP3_TEST_REQUIRE(console_output.str().find("bridge json event injected and processed") != std::string::npos);

    const auto record_path =
        std::filesystem::temp_directory_path() / "nlp3_panel_external_core_record.jsonl";
    std::filesystem::remove(record_path);
    NLP3_TEST_REQUIRE(panel_app.record_external_bridge_event(
        nlp3::testsupport::make_follow_event(
            "record-core-user-01",
            "record_core_alice",
            "Record Core Alice",
            "evt-record-core-follow-001",
            "room-record-core-001",
            1710000005002),
        record_path.string()));
    NLP3_TEST_REQUIRE(std::filesystem::exists(record_path));
    {
        std::ifstream record_input(record_path, std::ios::binary);
        NLP3_TEST_REQUIRE(record_input.good());
        const std::string payload{
            std::istreambuf_iterator<char>{record_input},
            std::istreambuf_iterator<char>{},
        };
        NLP3_TEST_REQUIRE(payload.find("evt-record-core-follow-001") != std::string::npos);
    }

    nlp3::bridge::TikTokExternalEventReplay replay{&panel_app};
    NLP3_TEST_REQUIRE(replay.replay_jsonl_file(record_path.string()) == 1);
    const auto manifest_after_replay = panel_app.external_bridge_manifest();
    const auto snapshot_after_replay = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_replay.total_events == 4);
    NLP3_TEST_REQUIRE(!snapshot_after_replay.recent_activity.empty());
    NLP3_TEST_REQUIRE(snapshot_after_replay.bridge.metrics.raw_events_received >= 4);
    NLP3_TEST_REQUIRE(snapshot_after_replay.bridge.metrics.raw_events_emitted >= 4);
    NLP3_TEST_REQUIRE(manifest_after_replay.total_external_events_submitted >= 4);
    NLP3_TEST_REQUIRE(snapshot_after_replay.external_bridge.total_external_events_submitted >= 4);

    NLP3_TEST_REQUIRE(panel_console.execute_line(std::string{"bridge replay "} + record_path.string()));
    const auto manifest_after_console_replay = panel_app.external_bridge_manifest();
    const auto snapshot_after_console_replay = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_console_replay.total_events == 5);
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge external"));
    NLP3_TEST_REQUIRE(manifest_after_console_replay.last_replay_path == record_path.string());
    NLP3_TEST_REQUIRE(manifest_after_console_replay.last_replay_accepted_events == 1);
    NLP3_TEST_REQUIRE(snapshot_after_console_replay.external_bridge.total_external_events_submitted >= 5);
    NLP3_TEST_REQUIRE(snapshot_after_console_replay.external_bridge.last_replay_path == record_path.string());
    NLP3_TEST_REQUIRE(snapshot_after_console_replay.external_bridge.last_replay_accepted_events == 1);
    NLP3_TEST_REQUIRE(console_output.str().find("replayed 1 external events") != std::string::npos);
    NLP3_TEST_REQUIRE(console_output.str().find("last_replay_accepted_events=1") != std::string::npos);
    NLP3_TEST_REQUIRE(console_output.str().find("total_external_events_submitted=5") != std::string::npos);
    NLP3_TEST_REQUIRE(console_output.str().find("current_room_id=room-record-core-001") != std::string::npos);
    NLP3_TEST_REQUIRE(console_output.str().find("last_event_kind=follow") != std::string::npos);
    NLP3_TEST_REQUIRE(console_output.str().find("event_counts=chat=3, like=0, gift=0, follow=2, share=0, viewer_join=0")
        != std::string::npos);

    nlp3::platform::PanelViewModelBuilder view_model_builder;
    const auto view_model = view_model_builder.build(initial_snapshot);
    const auto bridge_section = std::find_if(
        view_model.sections.begin(),
        view_model.sections.end(),
        [](const nlp3::platform::PanelViewSection& section) {
            return section.title == "Bridge";
        });
    NLP3_TEST_REQUIRE(bridge_section != view_model.sections.end());
    NLP3_TEST_REQUIRE(std::find_if(
        bridge_section->items.begin(),
        bridge_section->items.end(),
        [](const nlp3::platform::PanelViewSectionItem& item) {
            return item.label == "Mode" && item.value == "external";
        }) != bridge_section->items.end());
    NLP3_TEST_REQUIRE(std::find_if(
        bridge_section->items.begin(),
        bridge_section->items.end(),
        [](const nlp3::platform::PanelViewSectionItem& item) {
            return item.label == "Recording" && item.value == "no";
        }) != bridge_section->items.end());
    NLP3_TEST_REQUIRE(std::find_if(
        bridge_section->items.begin(),
        bridge_section->items.end(),
        [](const nlp3::platform::PanelViewSectionItem& item) {
            return item.label == "External counts"
                && item.value ==
                    "chat=0, like=0, gift=0, follow=0, share=0, viewer_join=0, "
                    "viewer_count=0, live_start=0, live_end=0, moderation=0, custom_raw=0";
        }) != bridge_section->items.end());

    // Verdad de estado (Fase 1): la alerta y la fase solo se limpian cuando la
    // sesion realmente conecta, y al detener el runner no queda nada de la
    // sesion anterior encolado.
    const auto submit_session_status =
        [&panel_app](
            nlp3::bridge::TikTokExternalSessionConnectionState state,
            const char* phase,
            const char* alert_code) {
            return panel_app.submit_external_session_status({
                "external-panel-user-01",
                "room-external-panel-001",
                state,
                "estado de sesion",
                1710000007000,
                phase,
                "warn",
                alert_code,
                0.0,
            });
        };

    // 1) Un fallo deja alerta y fase de espera en el snapshot.
    NLP3_TEST_REQUIRE(submit_session_status(
        nlp3::bridge::TikTokExternalSessionConnectionState::faulted,
        "waiting",
        "NOT_LIVE"));
    NLP3_TEST_REQUIRE(panel_app.snapshot().external_bridge.last_alert_code == "NOT_LIVE");

    // 2) Un status desconectado con la fase mintiendo "connected" NO borra la
    //    alerta: el motivo del fallo tiene que sobrevivir al silencio.
    NLP3_TEST_REQUIRE(submit_session_status(
        nlp3::bridge::TikTokExternalSessionConnectionState::disconnected,
        "connected",
        ""));
    const auto snapshot_during_silence = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_during_silence.external_bridge.last_alert_code == "NOT_LIVE");
    NLP3_TEST_REQUIRE(snapshot_during_silence.external_bridge.last_alert_severity == "warn");

    // 3) La sesion realmente conectada si limpia la alerta.
    NLP3_TEST_REQUIRE(submit_session_status(
        nlp3::bridge::TikTokExternalSessionConnectionState::connected,
        "connected",
        ""));
    const auto snapshot_reconnected = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_reconnected.external_bridge.last_alert_code.empty());
    NLP3_TEST_REQUIRE(snapshot_reconnected.external_bridge.last_phase == "connected");

    // 4) Detener el runner limpia fase y alerta junto con el estado: la franja
    //    no puede quedar en verde "Conectado" tras Desconectar.
    NLP3_TEST_REQUIRE(submit_session_status(
        nlp3::bridge::TikTokExternalSessionConnectionState::connected,
        "connected",
        "STREAM_DISCONNECTED"));
    panel_app.stop_external_runner();
    const auto snapshot_after_stop = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_stop.external_bridge.connection_state == "disconnected");
    NLP3_TEST_REQUIRE(snapshot_after_stop.external_bridge.last_phase.empty());
    NLP3_TEST_REQUIRE(snapshot_after_stop.external_bridge.last_alert_code.empty());
    NLP3_TEST_REQUIRE(snapshot_after_stop.external_bridge.last_alert_severity.empty());

    // 5) El presupuesto diario viaja del status al snapshot y sobrevive al
    //    Desconectar: sigue siendo el contador real de conexiones del dia.
    NLP3_TEST_REQUIRE(panel_app.submit_external_session_status({
        "external-panel-user-01",
        "room-external-panel-001",
        nlp3::bridge::TikTokExternalSessionConnectionState::connected,
        "estado con presupuesto",
        1710000008000,
        "connected",
        "info",
        "",
        0.0,
        50,
        12,
        10,
        2,
    }));
    const auto snapshot_with_budget = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_with_budget.external_bridge.daily_budget_total == 50);
    NLP3_TEST_REQUIRE(snapshot_with_budget.external_bridge.daily_budget_remaining == 12);
    NLP3_TEST_REQUIRE(snapshot_with_budget.external_bridge.daily_budget_manual_reserve == 10);
    NLP3_TEST_REQUIRE(snapshot_with_budget.external_bridge.daily_budget_remaining_auto == 2);
    panel_app.stop_external_runner();
    const auto snapshot_budget_after_stop = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_budget_after_stop.external_bridge.daily_budget_total == 50);
    NLP3_TEST_REQUIRE(snapshot_budget_after_stop.external_bridge.daily_budget_remaining == 12);

    // 6) La accion sugerida viaja con el codigo de alerta (el panel ofrece
    //    "que hacer") y se limpia al detener el runner: sobre una sesion que ya
    //    murio no queda ninguna accion que ofrecer.
    NLP3_TEST_REQUIRE(panel_app.submit_external_session_status({
        "external-panel-user-01",
        "room-external-panel-001",
        nlp3::bridge::TikTokExternalSessionConnectionState::reconnecting,
        "Rotando de API key",
        1710000009000,
        "waiting",
        "warn",
        "API_KEY_ROTATION_REQUESTED",
        30.0,
        0,
        0,
        0,
        0,
        "rotate_key",
    }));
    const auto snapshot_with_action = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_with_action.external_bridge.last_alert_code == "API_KEY_ROTATION_REQUESTED");
    NLP3_TEST_REQUIRE(snapshot_with_action.external_bridge.last_alert_action == "rotate_key");
    // Un status que no trae accion no arrastra la anterior.
    NLP3_TEST_REQUIRE(panel_app.submit_external_session_status({
        "external-panel-user-01",
        "room-external-panel-001",
        nlp3::bridge::TikTokExternalSessionConnectionState::faulted,
        "Otro fallo",
        1710000010000,
        "error",
        "error",
        "OTHER_CODE",
        0.0,
    }));
    const auto snapshot_without_action = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_without_action.external_bridge.last_alert_code == "OTHER_CODE");
    NLP3_TEST_REQUIRE(snapshot_without_action.external_bridge.last_alert_action.empty());
    panel_app.stop_external_runner();
    const auto snapshot_action_after_stop = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_action_after_stop.external_bridge.last_alert_action.empty());

    std::filesystem::remove(record_path);
    std::filesystem::remove(config_path);
    return 0;
}
