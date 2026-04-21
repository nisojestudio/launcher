#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>

#include "platform/panel_app.hpp"
#include "platform/panel_config_storage.hpp"
#include "platform/panel_console.hpp"
#include "platform/panel_view_model_builder.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

int main() {
    const auto config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_console_general_test_config.json",
        []() {
            nlp3::platform::PanelConfig config{};
            config.bridge_mode = "stub";
            config.bridge.stub_mode = true;
            config.bridge.source_name = "tiktok-stub";
            return config;
        }());

    nlp3::platform::PanelApp uninitialized_panel_app;
    const auto uninitialized_diagnostics = uninitialized_panel_app.diagnostics();
    NLP3_TEST_REQUIRE(!uninitialized_diagnostics.ok);
    NLP3_TEST_REQUIRE(!uninitialized_diagnostics.entries.empty());
    NLP3_TEST_REQUIRE(uninitialized_diagnostics.entries.front().level == nlp3::platform::PanelDiagnosticLevel::error);
    NLP3_TEST_REQUIRE(uninitialized_diagnostics.entries.front().code == "panel.not_initialized");

    nlp3::platform::PanelApp panel_app;
    NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));

    const auto snapshot = panel_app.snapshot();
    const auto diagnostics = panel_app.diagnostics();
    NLP3_TEST_REQUIRE(!snapshot.panel_name.empty());
    NLP3_TEST_REQUIRE(snapshot.bridge_mode == "stub");
    NLP3_TEST_REQUIRE(snapshot.bridge.integrated);
    NLP3_TEST_REQUIRE(snapshot.tts.available);
    NLP3_TEST_REQUIRE(snapshot.game.has_active_game);
    NLP3_TEST_REQUIRE(snapshot.game.active_game_id == "event-counter");
    NLP3_TEST_REQUIRE(!panel_app.start_external_ws());
    NLP3_TEST_REQUIRE(!panel_app.external_ws_status().running);
    NLP3_TEST_REQUIRE(snapshot.license.status == nlp3::platform::LicenseStatus::active);
    NLP3_TEST_REQUIRE(snapshot.license.message == "local development mode");
    NLP3_TEST_REQUIRE(snapshot.license.tier == "local-dev");
    NLP3_TEST_REQUIRE(!diagnostics.entries.empty());
    NLP3_TEST_REQUIRE(std::find_if(
        diagnostics.entries.begin(),
        diagnostics.entries.end(),
        [](const nlp3::platform::PanelDiagnosticEntry& entry) {
            return entry.level == nlp3::platform::PanelDiagnosticLevel::info
                && entry.code == "bridge.integrated";
        }) != diagnostics.entries.end());
    NLP3_TEST_REQUIRE(std::find_if(
        diagnostics.entries.begin(),
        diagnostics.entries.end(),
        [](const nlp3::platform::PanelDiagnosticEntry& entry) {
            return entry.code == "game.active" || entry.code == "game.inactive";
        }) != diagnostics.entries.end());
    NLP3_TEST_REQUIRE(std::find_if(
        diagnostics.entries.begin(),
        diagnostics.entries.end(),
        [](const nlp3::platform::PanelDiagnosticEntry& entry) {
            return entry.level == nlp3::platform::PanelDiagnosticLevel::info
                && entry.code == "license.active";
        }) != diagnostics.entries.end());

    const auto tts_command_result = panel_app.execute_command({
        nlp3::platform::PanelCommandKind::tts_enqueue_announcement,
        "PanelApp hello",
    });
    NLP3_TEST_REQUIRE(tts_command_result.ok);
    const auto snapshot_after_command = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_command.tts.queued_messages == 1);

    nlp3::platform::PanelViewModelBuilder view_model_builder;
    const auto view_model = view_model_builder.build(snapshot_after_command);
    NLP3_TEST_REQUIRE(!view_model.title.empty());
    NLP3_TEST_REQUIRE(!view_model.sections.empty());
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
            return item.label == "Mode" && item.value == "stub";
        }) != bridge_section->items.end());
    NLP3_TEST_REQUIRE(std::find_if(
        view_model.sections.begin(),
        view_model.sections.end(),
        [](const nlp3::platform::PanelViewSection& section) {
            return section.title == "License";
        }) != view_model.sections.end());
    NLP3_TEST_REQUIRE(!view_model.actions.empty());

    std::istringstream console_input;
    std::ostringstream console_output;
    nlp3::platform::PanelConsole panel_console{&panel_app, &console_input, &console_output};
    NLP3_TEST_REQUIRE(panel_console.execute_line("help"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("status"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("diagnostics"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("tick"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("tick 42"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("run 2"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("run 2 1000"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("games"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("game list"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge mode"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge target"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge target cocadevidrio80"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge external"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge runner"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge runner status"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge ws"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge ws port"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge ws port 18765"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("ui"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("ui start 18080"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("ui"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("ui stop"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge attach"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge demo ready"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge demo live"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge demo session 1 1"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge demo observe 1 1"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge ws start"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge ws stop"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge mode external"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge runner status"));
    NLP3_TEST_REQUIRE(panel_app.config().bridge_mode == "external");
    NLP3_TEST_REQUIRE(panel_app.config().external_target_user == "cocadevidrio80");
    NLP3_TEST_REQUIRE(panel_app.config().external_ws_port == 18765);
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge mode stub"));
    NLP3_TEST_REQUIRE(panel_app.config().bridge_mode == "stub");
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge inject chat alice hola stub"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("config"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("license"));

    const auto available_game_ids = panel_app.available_game_ids();
    NLP3_TEST_REQUIRE(available_game_ids.size() >= 2);
    NLP3_TEST_REQUIRE(std::find(
        available_game_ids.begin(),
        available_game_ids.end(),
        "event-counter") != available_game_ids.end());

    NLP3_TEST_REQUIRE(panel_console.execute_line("config save"));
    panel_app.config().panel_name = "Mutated Panel Name";
    NLP3_TEST_REQUIRE(panel_console.execute_line("config reload"));
    NLP3_TEST_REQUIRE(panel_app.config().panel_name == "Nisoje Studio");
    NLP3_TEST_REQUIRE(panel_app.config().external_target_user == "cocadevidrio80");
    NLP3_TEST_REQUIRE(panel_app.config().external_ws_port == 18765);

    nlp3::platform::PanelConfig reloaded_live_config = panel_app.config();
    reloaded_live_config.periodic_tts.enabled = true;
    reloaded_live_config.periodic_tts.interval_ms = 10;
    reloaded_live_config.periodic_tts.messages = {"Reloaded periodic"};
    nlp3::platform::PanelConfigStorage live_config_storage;
    NLP3_TEST_REQUIRE(live_config_storage.save_to_file(reloaded_live_config, config_path.string()));
    NLP3_TEST_REQUIRE(panel_app.reload_config(config_path.string()));
    NLP3_TEST_REQUIRE(panel_app.tick_periodic_tts(10));
    NLP3_TEST_REQUIRE(panel_app.snapshot().tts.queued_messages >= 1);

    NLP3_TEST_REQUIRE(panel_console.execute_line("tts say Hola panel"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("game activate event-counter"));
    NLP3_TEST_REQUIRE(!panel_console.execute_line("unknown command"));
    NLP3_TEST_REQUIRE(panel_app.snapshot().game.active_game_id == "event-counter");
    NLP3_TEST_REQUIRE(panel_app.snapshot().tts.queued_messages >= 1);

    const auto output = console_output.str();
    NLP3_TEST_REQUIRE(output.find("Commands:") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("event-counter") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Bridge mode: stub") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Bridge target user: -") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge target user set to cocadevidrio80") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Diagnostics: ok") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge.integrated") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge mode set to external") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge mode set to stub") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge external mode is not active") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("External bridge:") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Bridge WS port: configured=8765") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge ws port set to 18765") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("UI server:") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("ui server started at http://127.0.0.1:18080") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("ui server stopped") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Demo ready: no") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("external_mode=no") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("tick processed 0 bridge events, periodic_tts=no") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("ran 2 ticks, processed 0 bridge events, periodic_tts=0") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("config reloaded and applied") != std::string::npos);

    std::filesystem::remove(config_path);

    const auto warning_config_path =
        std::filesystem::temp_directory_path() / "nlp3_panel_console_general_warning_config.json";
    std::filesystem::remove(warning_config_path);
    nlp3::platform::PanelApp warning_panel_app;
    NLP3_TEST_REQUIRE(warning_panel_app.initialize(warning_config_path.string()));
    const auto bridge_stop_result = warning_panel_app.execute_command({
        nlp3::platform::PanelCommandKind::bridge_stop,
        {},
    });
    NLP3_TEST_REQUIRE(bridge_stop_result.ok);
    const auto warning_diagnostics = warning_panel_app.diagnostics();
    NLP3_TEST_REQUIRE(std::find_if(
        warning_diagnostics.entries.begin(),
        warning_diagnostics.entries.end(),
        [](const nlp3::platform::PanelDiagnosticEntry& entry) {
            return entry.level == nlp3::platform::PanelDiagnosticLevel::warning
                && entry.code == "bridge.not_running";
        }) != warning_diagnostics.entries.end());
    std::filesystem::remove(warning_config_path);

    return 0;
}
