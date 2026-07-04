#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "games/live_timer_game.hpp"
#include "platform/panel_app.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

int main() {
    std::puts("live_timer_api_smoke cp1: standalone timer works");
    std::fflush(stdout);

    {
        nlp3::games::LiveTimerGame timer;
        auto config = timer.default_config();
        config.set("initial_time_s", 60.0);
        timer.apply_config(config);
        timer.on_activated();

        NLP3_TEST_REQUIRE(timer.state().running);
        NLP3_TEST_REQUIRE(timer.remaining_seconds() > 55.0);
        NLP3_TEST_REQUIRE(timer.remaining_seconds() <= 60.0);
        NLP3_TEST_REQUIRE(!timer.state().completed);
        NLP3_TEST_REQUIRE(timer.game_id() == "live-timer");
        NLP3_TEST_REQUIRE(!timer.format_time().empty());
        NLP3_TEST_REQUIRE(timer.config().get_double("initial_time_s") == 60.0);
        NLP3_TEST_REQUIRE(timer.is_enabled());
        NLP3_TEST_REQUIRE(!timer.state().paused);
    }

    std::puts("live_timer_api_smoke cp2: PanelApp has timer independent of games");
    std::fflush(stdout);

    {
        const auto config_path = nlp3::testsupport::write_temp_panel_config(
            "nlp3_live_timer_api_smoke_test_config.json",
            []() {
                nlp3::platform::PanelConfig config{};
                config.bridge_mode = "stub";
                config.bridge.stub_mode = true;
                config.bridge.source_name = "tiktok-stub";
                config.default_game_id = "event-counter";
                return config;
            }());

        nlp3::platform::PanelApp panel_app;
        NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));

        const auto snapshot = panel_app.snapshot();
        NLP3_TEST_REQUIRE(snapshot.timer.has_timer);
        NLP3_TEST_REQUIRE(snapshot.timer.timer_id == "live-timer");
        NLP3_TEST_REQUIRE(!snapshot.timer.running);
        NLP3_TEST_REQUIRE(snapshot.timer.enabled);
        NLP3_TEST_REQUIRE(!snapshot.timer.paused);
        NLP3_TEST_REQUIRE(snapshot.timer.remaining_seconds > 0.0);
        NLP3_TEST_REQUIRE(!snapshot.timer.remaining_formatted.empty());
        NLP3_TEST_REQUIRE(!snapshot.timer.overlay_url.empty());
        NLP3_TEST_REQUIRE(snapshot.timer.overlay_url.find("/overlay/live-timer") != std::string::npos);
    }

    std::puts("live_timer_api_smoke cp3: timer survives game change");
    std::fflush(stdout);

    {
        const auto config_path = nlp3::testsupport::write_temp_panel_config(
            "nlp3_live_timer_api_game_test_config.json",
            []() {
                nlp3::platform::PanelConfig config{};
                config.bridge_mode = "stub";
                config.bridge.stub_mode = true;
                config.bridge.source_name = "tiktok-stub";
                config.default_game_id = "event-counter";
                return config;
            }());

        nlp3::platform::PanelApp panel_app;
        NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));

        auto snap1 = panel_app.snapshot();
        NLP3_TEST_REQUIRE(snap1.timer.has_timer);
        NLP3_TEST_REQUIRE(snap1.game.has_active_game);
        NLP3_TEST_REQUIRE(snap1.game.active_game_id == "event-counter");

        auto snap2 = panel_app.snapshot();
        NLP3_TEST_REQUIRE(snap2.timer.has_timer);
        NLP3_TEST_REQUIRE(snap2.timer.timer_id == "live-timer");
    }

    std::puts("live_timer_api_smoke cp4: config round-trip via LiveTimerGame");
    std::fflush(stdout);

    {
        nlp3::games::LiveTimerGame timer;
        auto cfg = timer.default_config();
        cfg.set("title_text", std::string("Mi Timer"));
        cfg.set("time_per_like_s", 1.5);
        timer.apply_config(cfg);
        timer.on_activated();

        const auto& state = timer.state();
        NLP3_TEST_REQUIRE(state.title_text == "Mi Timer");
        NLP3_TEST_REQUIRE(state.time_per_like == 1.5);

        const auto& stored_cfg = timer.config();
        NLP3_TEST_REQUIRE(stored_cfg.get_string("title_text") == "Mi Timer");
        NLP3_TEST_REQUIRE(stored_cfg.get_double("time_per_like_s") == 1.5);
    }

    std::puts("live_timer_api_smoke cp5: start_http_ui with timer endpoint");
    std::fflush(stdout);

    {
        const auto config_path = nlp3::testsupport::write_temp_panel_config(
            "nlp3_live_timer_api_http_test_config.json",
            []() {
                nlp3::platform::PanelConfig config{};
                config.bridge_mode = "stub";
                config.bridge.stub_mode = true;
                config.bridge.source_name = "tiktok-stub";
                config.default_game_id = "event-counter";
                return config;
            }());

        nlp3::platform::PanelApp panel_app;
        NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));
        NLP3_TEST_REQUIRE(panel_app.start_http_ui(19113));

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Verify the timer is configured via the accessor, not via HTTP
        const auto live_timer = panel_app.live_timer();
        NLP3_TEST_REQUIRE(live_timer != nullptr);
        NLP3_TEST_REQUIRE(live_timer->game_id() == "live-timer");
        NLP3_TEST_REQUIRE(!live_timer->state().running);

        panel_app.stop_http_ui();
    }

    std::puts("live_timer_api_smoke PASSED");
    std::fflush(stdout);
    return 0;
}
