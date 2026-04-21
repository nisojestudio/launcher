#include <filesystem>
#include <sstream>
#include <string>

#include "bridge/tiktok_external_event_codec.hpp"
#include "platform/panel_app.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_console.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

int main() {
    nlp3::platform::PanelConfig config{};
    config.bridge_mode = "external";
    const auto config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_external_demo_test_config.json",
        config);

    nlp3::platform::PanelApp panel_app;
    NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));
    NLP3_TEST_REQUIRE(panel_app.is_external_bridge_mode());

    std::istringstream console_input;
    std::ostringstream console_output;
    nlp3::platform::PanelConsole panel_console{
        &panel_app,
        &console_input,
        &console_output,
    };

    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge demo live 18765"));
    const auto ws_status = panel_app.external_ws_status();
    NLP3_TEST_REQUIRE(ws_status.running);
    NLP3_TEST_REQUIRE(ws_status.port == 18765);

    const nlp3::bridge::TikTokExternalEventCodec codec{};
    const auto valid_payload = codec.encode_json(nlp3::testsupport::make_chat_event(
        "external-demo-user-01",
        "demo_alice",
        "Demo Alice",
        "evt-external-demo-chat-001",
        "room-external-demo-001",
        "Demo hello",
        1710000007000,
        "https://cdn.example.com/avatar-demo-alice.png"));

    NLP3_TEST_REQUIRE(panel_app.submit_external_ws_payload(valid_payload));
    const auto snapshot_after_payload = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_payload.total_events == 1);
    NLP3_TEST_REQUIRE(snapshot_after_payload.external_ws.accepted_messages == 1);
    NLP3_TEST_REQUIRE(snapshot_after_payload.external_ws.rejected_messages == 0);

    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge demo ready"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge demo observe 1 1 0 18765"));

    const auto output = console_output.str();
    NLP3_TEST_REQUIRE(output.find("bridge live demo ready on port 18765") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Demo ready: yes") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge demo observe: ticks=") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("diagnostics=ok") != std::string::npos);

    panel_app.stop_external_ws();
    std::filesystem::remove(config_path);
    return 0;
}
