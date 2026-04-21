#include <algorithm>
#include <filesystem>

#include "bridge/tiktok_external_session_status.hpp"
#include "platform/panel_app.hpp"
#include "platform/panel_tick_result.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

int main() {
    nlp3::platform::PanelConfig external_config{};
    external_config.bridge_mode = "external";
    const auto external_config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_run_mode_external_config.json",
        external_config);

    nlp3::platform::PanelApp external_panel_app;
    NLP3_TEST_REQUIRE(external_panel_app.initialize(external_config_path.string()));
    NLP3_TEST_REQUIRE(external_panel_app.is_external_bridge_mode());

    NLP3_TEST_REQUIRE(external_panel_app.submit_external_bridge_event(nlp3::testsupport::make_chat_event(
        "run-user-01",
        "run_alice",
        "Run Alice",
        "evt-run-chat-001",
        "room-run-001",
        "Run hello one",
        1710000007000)));
    NLP3_TEST_REQUIRE(external_panel_app.submit_external_bridge_event(nlp3::testsupport::make_follow_event(
        "run-user-02",
        "run_bob",
        "Run Bob",
        "evt-run-follow-001",
        "room-run-001",
        1710000007001)));

    const auto external_run_result = external_panel_app.run_ticks(2, 0, 0);
    NLP3_TEST_REQUIRE(external_run_result.ticks_executed == 2);
    NLP3_TEST_REQUIRE(external_run_result.total_bridge_events_processed >= 2);
    NLP3_TEST_REQUIRE(external_run_result.periodic_tts_enqueues == 0);
    NLP3_TEST_REQUIRE(external_run_result.last_now_ms == 0);

    const auto external_snapshot = external_panel_app.snapshot();
    NLP3_TEST_REQUIRE(external_snapshot.total_events == 2);
    NLP3_TEST_REQUIRE(external_snapshot.external_bridge.external_mode);
    NLP3_TEST_REQUIRE(external_snapshot.external_bridge.total_external_events_submitted == 2);
    NLP3_TEST_REQUIRE(external_snapshot.external_bridge.current_room_id == "room-run-001");
    NLP3_TEST_REQUIRE(external_snapshot.external_bridge.last_event_kind == "follow");
    NLP3_TEST_REQUIRE(external_snapshot.external_bridge.last_event_actor == "Run Bob");
    NLP3_TEST_REQUIRE(external_snapshot.external_bridge.last_event_timestamp_ms == 1710000007001);
    NLP3_TEST_REQUIRE(external_snapshot.external_bridge.chat_events == 1);
    NLP3_TEST_REQUIRE(external_snapshot.external_bridge.follow_events == 1);

    nlp3::platform::PanelConfig periodic_config{};
    periodic_config.periodic_tts.enabled = true;
    periodic_config.periodic_tts.interval_ms = 1000;
    periodic_config.periodic_tts.messages = {"Periodic tick message"};
    const auto periodic_config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_run_mode_periodic_config.json",
        periodic_config);

    nlp3::platform::PanelApp periodic_panel_app;
    NLP3_TEST_REQUIRE(periodic_panel_app.initialize(periodic_config_path.string()));

    const auto periodic_before = periodic_panel_app.snapshot();
    const auto tick_before = periodic_panel_app.tick(500);
    NLP3_TEST_REQUIRE(tick_before.now_ms == 500);
    NLP3_TEST_REQUIRE(tick_before.bridge_events_processed == 0);
    NLP3_TEST_REQUIRE(!tick_before.periodic_tts_enqueued);
    NLP3_TEST_REQUIRE(periodic_panel_app.snapshot().tts.queued_messages == periodic_before.tts.queued_messages);

    const auto tick_after = periodic_panel_app.tick(1000);
    NLP3_TEST_REQUIRE(tick_after.now_ms == 1000);
    NLP3_TEST_REQUIRE(tick_after.bridge_events_processed == 0);
    NLP3_TEST_REQUIRE(tick_after.periodic_tts_enqueued);
    const auto periodic_after_tick = periodic_panel_app.snapshot();
    NLP3_TEST_REQUIRE(periodic_after_tick.tts.available == periodic_before.tts.available);
    NLP3_TEST_REQUIRE(std::find_if(
        periodic_after_tick.recent_activity.begin(),
        periodic_after_tick.recent_activity.end(),
        [](const nlp3::platform::PanelActivityEntry& entry) {
            return entry.kind == nlp3::platform::PanelActivityKind::tts_periodic_enqueued;
        }) != periodic_after_tick.recent_activity.end());

    nlp3::platform::PanelConfig periodic_run_config{};
    periodic_run_config.periodic_tts.enabled = true;
    periodic_run_config.periodic_tts.interval_ms = 1000;
    periodic_run_config.periodic_tts.messages = {"Periodic run message"};
    const auto periodic_run_config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_run_mode_periodic_run_config.json",
        periodic_run_config);

    nlp3::platform::PanelApp periodic_run_panel_app;
    NLP3_TEST_REQUIRE(periodic_run_panel_app.initialize(periodic_run_config_path.string()));
    const auto periodic_run_before = periodic_run_panel_app.snapshot();
    const auto periodic_run_result = periodic_run_panel_app.run_ticks(2, 0, 1000);
    NLP3_TEST_REQUIRE(periodic_run_result.ticks_executed == 2);
    NLP3_TEST_REQUIRE(periodic_run_result.total_bridge_events_processed == 0);
    NLP3_TEST_REQUIRE(periodic_run_result.periodic_tts_enqueues >= 1);
    NLP3_TEST_REQUIRE(periodic_run_result.last_now_ms == 1000);
    const auto periodic_run_after = periodic_run_panel_app.snapshot();
    NLP3_TEST_REQUIRE(periodic_run_after.tts.available == periodic_run_before.tts.available);

    nlp3::platform::PanelConfig external_periodic_config{};
    external_periodic_config.bridge_mode = "external";
    external_periodic_config.periodic_tts.enabled = true;
    external_periodic_config.periodic_tts.interval_ms = 1000;
    external_periodic_config.periodic_tts.messages = {"Periodic external message"};
    const auto external_periodic_config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_run_mode_external_periodic_config.json",
        external_periodic_config);

    nlp3::platform::PanelApp external_periodic_panel_app;
    NLP3_TEST_REQUIRE(external_periodic_panel_app.initialize(external_periodic_config_path.string()));

    const auto external_periodic_before = external_periodic_panel_app.tick(1000);
    NLP3_TEST_REQUIRE(!external_periodic_before.periodic_tts_enqueued);

    NLP3_TEST_REQUIRE(external_periodic_panel_app.submit_external_session_status({
        "periodic-user",
        "room-periodic-001",
        nlp3::bridge::TikTokExternalSessionConnectionState::connected,
        "connected",
        1000,
    }));

    const auto external_periodic_after = external_periodic_panel_app.tick(1000);
    NLP3_TEST_REQUIRE(external_periodic_after.periodic_tts_enqueued);

    std::filesystem::remove(external_config_path);
    std::filesystem::remove(periodic_config_path);
    std::filesystem::remove(periodic_run_config_path);
    std::filesystem::remove(external_periodic_config_path);
    return 0;
}
