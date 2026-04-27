#include <cassert>
#include <filesystem>

#include "host/host_runtime.hpp"
#include "platform/local_game_catalog_source.hpp"
#include "platform/local_license_service.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_config_storage.hpp"
#include "platform/runtime.hpp"
#include "test_support.hpp"
#include "tts/mock_tts_backend.hpp"

int main() {
    const auto manifest = nlp3::platform::build_runtime_manifest();
    assert(manifest.modules.size() == 5);
    assert(manifest.modules[0].name == "host");
    assert(manifest.modules[0].stage == nlp3::ModuleStage::integrated);
    assert(manifest.modules[2].name == "gamesdk");
    assert(manifest.modules[3].name == "bridge");
    assert(manifest.modules[3].stage == nlp3::ModuleStage::integrated);

    nlp3::platform::PanelConfig persisted_config{};
    persisted_config.panel_name = "Panel Persisted";
    persisted_config.default_game_id = "event-counter";
    persisted_config.bridge_mode = "external";
    persisted_config.external_target_user = "persisted_user";
    persisted_config.external_ws_port = 28765;
    persisted_config.embedded_ui_enabled = true;
    persisted_config.embedded_ui_fallback_to_browser = false;
    persisted_config.embedded_ui_devtools = true;
    persisted_config.embedded_ui_url = "http://127.0.0.1:19991/";
    persisted_config.embedded_ui_startup_timeout_ms = 43210;
    persisted_config.bridge.enabled = false;
    persisted_config.bridge.emit_share_events = false;
    persisted_config.bridge.emit_viewer_join_events = false;
    persisted_config.bridge.emit_viewer_count_events = false;
    persisted_config.bridge.emit_live_start_events = false;
    persisted_config.bridge.emit_live_end_events = false;
    persisted_config.bridge.emit_moderation_events = false;
    persisted_config.bridge.emit_custom_raw_events = false;
    persisted_config.bridge.passthrough_avatar_url = false;
    persisted_config.bridge.source_name = "saved-bridge";
    persisted_config.tts_runtime.enabled = true;
    persisted_config.tts_runtime.max_queue_size = 9;
    persisted_config.tts_runtime.backend_queue_size = 17;
    persisted_config.tts_runtime.max_dispatch_per_tick = 3;
    persisted_config.tts_runtime.max_text_length = 144;
    persisted_config.tts_runtime.drop_oldest_on_overflow = true;
    persisted_config.tts_runtime.selected_voice_id = "english-female";
    persisted_config.tts_runtime.selected_language = "en";
    persisted_config.tts_runtime.frequency = "high";
    persisted_config.tts.allow_chat_messages = false;
    persisted_config.tts.allow_manual_messages = false;
    persisted_config.tts.include_actor_name_for_chat = false;
    persisted_config.tts.min_text_length = 7;
    persisted_config.tts.chat_filter_mode = nlp3::tts::TtsChatFilterMode::subscribers_only;
    persisted_config.tts.chat_cooldown_ms = 3456;
    persisted_config.tts.chat_message_template = "Mensaje de {user}: {message}";
    persisted_config.automation.enable_follow_thanks_tts = true;
    persisted_config.automation.enable_subscriber_thanks_tts = true;
    persisted_config.automation.gift_thanks_template = "Gracias por tu apoyo";
    persisted_config.automation.follow_thanks_template = "Gracias por seguir";
    persisted_config.automation.subscriber_thanks_template = "Gracias {user} por suscribirte";
    persisted_config.automation.subscriber_thanks_cooldown_ms = 6789;
    persisted_config.periodic_tts.enabled = true;
    persisted_config.periodic_tts.interval_ms = 4321;
    persisted_config.periodic_tts.messages = {"Persisted A", "Persisted B"};

    const auto config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_services_test_config.json",
        persisted_config);
    nlp3::platform::PanelConfigStorage config_storage;
    nlp3::platform::PanelConfig loaded_config{};
    assert(config_storage.load_from_file(config_path.string(), loaded_config));
    assert(loaded_config.panel_name == "Panel Persisted");
    assert(loaded_config.default_game_id == "event-counter");
    assert(loaded_config.bridge_mode == "external");
    assert(loaded_config.external_target_user == "persisted_user");
    assert(loaded_config.external_ws_port == 28765);
    assert(loaded_config.embedded_ui_enabled);
    assert(!loaded_config.embedded_ui_fallback_to_browser);
    assert(loaded_config.embedded_ui_devtools);
    assert(loaded_config.embedded_ui_url == "http://127.0.0.1:19991/");
    assert(loaded_config.embedded_ui_startup_timeout_ms == 43210);
    assert(!loaded_config.bridge.enabled);
    assert(!loaded_config.bridge.emit_share_events);
    assert(!loaded_config.bridge.emit_viewer_join_events);
    assert(!loaded_config.bridge.emit_viewer_count_events);
    assert(!loaded_config.bridge.emit_live_start_events);
    assert(!loaded_config.bridge.emit_live_end_events);
    assert(!loaded_config.bridge.emit_moderation_events);
    assert(!loaded_config.bridge.emit_custom_raw_events);
    assert(!loaded_config.bridge.passthrough_avatar_url);
    assert(loaded_config.bridge.source_name == "saved-bridge");
    assert(loaded_config.tts_runtime.enabled);
    assert(loaded_config.tts_runtime.max_queue_size == 9);
    assert(loaded_config.tts_runtime.backend_queue_size == 17);
    assert(loaded_config.tts_runtime.max_dispatch_per_tick == 3);
    assert(loaded_config.tts_runtime.max_text_length == 144);
    assert(loaded_config.tts_runtime.drop_oldest_on_overflow);
    assert(loaded_config.tts_runtime.selected_voice_id == "english-female");
    assert(loaded_config.tts_runtime.selected_language == "en");
    assert(loaded_config.tts_runtime.frequency == "high");
    assert(!loaded_config.tts.allow_chat_messages);
    assert(!loaded_config.tts.allow_manual_messages);
    assert(!loaded_config.tts.include_actor_name_for_chat);
    assert(loaded_config.tts.min_text_length == 7);
    assert(loaded_config.tts.chat_filter_mode == nlp3::tts::TtsChatFilterMode::subscribers_only);
    assert(loaded_config.tts.chat_cooldown_ms == 3456);
    assert(loaded_config.tts.chat_message_template == "Mensaje de {user}: {message}");
    assert(loaded_config.automation.enable_follow_thanks_tts);
    assert(loaded_config.automation.enable_subscriber_thanks_tts);
    assert(loaded_config.automation.gift_thanks_template == "Gracias por tu apoyo");
    assert(loaded_config.automation.follow_thanks_template == "Gracias por seguir");
    assert(loaded_config.automation.subscriber_thanks_template == "Gracias {user} por suscribirte");
    assert(loaded_config.automation.subscriber_thanks_cooldown_ms == 6789);
    assert(loaded_config.periodic_tts.enabled);
    assert(loaded_config.periodic_tts.interval_ms == 4321);
    assert(loaded_config.periodic_tts.messages.size() == 2);
    assert(loaded_config.periodic_tts.messages[0] == "Persisted A");
    assert(loaded_config.periodic_tts.messages[1] == "Persisted B");

    nlp3::platform::LocalLicenseService local_license_service;
    const auto local_license_snapshot = local_license_service.snapshot();
    assert(local_license_snapshot.status == nlp3::platform::LicenseStatus::active);
    assert(local_license_snapshot.message == "local development mode");
    assert(local_license_snapshot.tier == "local-dev");

    nlp3::gamesdk::GameCatalog source_catalog;
    source_catalog.add(nlp3::gamesdk::GameCatalogEntry{
        "catalog-probe",
        "Catalog Probe",
        "0.1.0",
        "local",
        true,
        true,
        false,
        {},
        {},
        0,
        {},
    });
    nlp3::platform::LocalGameCatalogSource local_catalog_source;
    local_catalog_source.set_catalog(source_catalog);
    const auto loaded_catalog = local_catalog_source.load_catalog();
    assert(loaded_catalog.contains("catalog-probe"));
    const auto* loaded_catalog_entry = loaded_catalog.find_by_id("catalog-probe");
    assert(loaded_catalog_entry != nullptr);
    assert(loaded_catalog_entry->display_name == "Catalog Probe");
    assert(loaded_catalog_entry->source == "local");

    nlp3::tts::MockTtsBackend mock_tts_backend;
    nlp3::tts::TtsConfig live_tts_runtime{};
    live_tts_runtime.enabled = true;
    live_tts_runtime.max_queue_size = 0;
    live_tts_runtime.max_dispatch_per_tick = 8;

    nlp3::tts::TtsPolicy live_tts_policy{};
    live_tts_policy.allow_manual_messages = true;
    live_tts_policy.min_text_length = 1;

    nlp3::tts::HostTtsService live_tts_service{live_tts_runtime, live_tts_policy, mock_tts_backend};
    nlp3::host::HostRuntime host_runtime{
        nullptr,
        &live_tts_service,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{},
        nullptr,
        nlp3::host::HostAutomationEngine{},
        nlp3::host::HostPeriodicTtsEngine{},
        nullptr,
    };

    nlp3::host::HostPeriodicTtsConfig first_periodic{};
    first_periodic.enabled = true;
    first_periodic.interval_ms = 1000;
    first_periodic.messages = {"Mensaje viejo"};
    host_runtime.apply_periodic_tts_config(first_periodic);
    assert(host_runtime.tick_periodic_tts(1000));
    assert(host_runtime.queued_tts_messages() == 1);

    host_runtime.clear_pending_tts();

    nlp3::host::HostPeriodicTtsConfig second_periodic = first_periodic;
    second_periodic.messages = {"Mensaje nuevo"};
    host_runtime.apply_periodic_tts_config(second_periodic);
    assert(host_runtime.queued_tts_messages() == 0);
    assert(host_runtime.tick_periodic_tts(1000));
    assert(host_runtime.flush_tts(8) == 1);
    assert(mock_tts_backend.spoken_messages().size() == 1);
    assert(mock_tts_backend.spoken_messages().back().text == "Mensaje nuevo");

    std::filesystem::remove(config_path);
    return 0;
}
