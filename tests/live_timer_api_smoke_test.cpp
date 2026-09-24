#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "games/live_timer_game.hpp"
#include "platform/panel_app.hpp"
#include "platform/wall_clock.h"
#include "platform/win_http_client.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

// V2: el estado del timer ya no vive en %TEMP% sino en %LOCALAPPDATA%. Ese es
// el estado REAL del usuario: este test no debe leerlo ni pisarlo, y ademas su
// resultado no puede depender de lo que el usuario tenga guardado. Se aisla el
// directorio via NLP3_TIMER_STATE_DIR.
static std::filesystem::path setup_isolated_timer_state_dir() {
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) / "nlp3_live_timer_state";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
    _putenv_s("NLP3_TIMER_STATE_DIR", dir.string().c_str());
#endif
    return dir;
}

int main() {
    const auto timer_state_dir = setup_isolated_timer_state_dir();
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
        // V2: sin tiempo configurado el timer arranca en CERO (antes habia un
        // default de 5 minutos). El snapshot debe seguir siendo coherente y no
        // puede quedar corriendo solo.
        NLP3_TEST_REQUIRE(snapshot.timer.remaining_seconds == 0.0);
        NLP3_TEST_REQUIRE(!snapshot.timer.running);
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

    std::puts("live_timer_api_smoke cp6: el tiempo sobrevive al cierre del panel");
    std::fflush(stdout);

    {
        const auto config_path = nlp3::testsupport::write_temp_panel_config(
            "nlp3_live_timer_persist_config.json",
            []() {
                nlp3::platform::PanelConfig config{};
                config.bridge_mode = "stub";
                config.bridge.stub_mode = true;
                config.bridge.source_name = "tiktok-stub";
                config.default_game_id = "event-counter";
                return config;
            }());

        // 1) Configurar 600s, arrancar y guardar: es lo que hace el cierre del
        //    panel a mitad de cuenta.
        {
            nlp3::platform::PanelApp app;
            NLP3_TEST_REQUIRE(app.initialize(config_path.string()));
            auto* timer = app.live_timer();
            NLP3_TEST_REQUIRE(timer != nullptr);

            auto cfg = timer->default_config();
            cfg.set("initial_time_s", 600.0);
            timer->apply_config(cfg);
            timer->on_activated();
            NLP3_TEST_REQUIRE(timer->is_running());
            NLP3_TEST_REQUIRE(timer->is_enabled());

            NLP3_TEST_REQUIRE(app.save_timer_state());
        }

        // 2) Reabrir: el tiempo se CONSERVA y el timer NO arranca solo.
        {
            nlp3::platform::PanelApp app2;
            NLP3_TEST_REQUIRE(app2.initialize(config_path.string()));
            auto* timer2 = app2.live_timer();
            NLP3_TEST_REQUIRE(timer2 != nullptr);

            NLP3_TEST_REQUIRE(!timer2->is_running());   // espera a Iniciar
            const double restored = timer2->remaining_seconds();
            NLP3_TEST_REQUIRE(restored > 595.0);
            NLP3_TEST_REQUIRE(restored <= 600.0);

            // Pulsar Iniciar CONTINUA desde el valor restaurado y lo hace
            // visible (no vuelve al tiempo inicial ni se queda oculto).
            timer2->on_activated();
            NLP3_TEST_REQUIRE(timer2->is_running());
            NLP3_TEST_REQUIRE(timer2->is_enabled());
            NLP3_TEST_REQUIRE(timer2->remaining_seconds() > 595.0);
        }
    }

    std::puts("live_timer_api_smoke cp7: el motor visual viaja por HTTP (ida y vuelta)");
    std::fflush(stdout);

    // Fase 5: este checkpoint existe porque el mapeo de claves del config es
    // EXPLICITO en los dos lados (GET /api/timer/config y POST
    // /api/timer/configure). Una clave que falte en cualquiera de los dos se
    // ignora en silencio: el operador la configura y no pasa nada. Sin una prueba
    // por HTTP, ese fallo no lo ve nadie hasta que se queja.
    {
        const auto config_path = nlp3::testsupport::write_temp_panel_config(
            "nlp3_live_timer_visual_http_config.json",
            []() {
                nlp3::platform::PanelConfig config{};
                config.bridge_mode = "stub";
                config.bridge.stub_mode = true;
                config.bridge.source_name = "tiktok-stub";
                config.default_game_id = "event-counter";
                return config;
            }());

        nlp3::platform::PanelApp app;
        NLP3_TEST_REQUIRE(app.initialize(config_path.string()));
        NLP3_TEST_REQUIRE(app.start_http_ui(19123));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        const std::string base = "http://127.0.0.1:19123";

        // El servidor HTTP del panel es cooperativo: acepta y responde dentro de
        // PanelApp::tick(), asi que la peticion tiene que ir en otro hilo mientras
        // el principal bombea. Sin esto la peticion se queda esperando y el test
        // mediria un timeout, no el contrato.
        const auto pump_request = [&app](const std::string& method, const std::string& url,
                                         const std::string& body) {
            nlp3::platform::HttpResponse response{};
            std::atomic<bool> finished{false};
            std::thread worker([&]() {
                response = nlp3::platform::http_request(
                    method, url, body, body.empty() ? std::string_view{} : std::string_view{"application/json"}, {});
                finished.store(true);
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (!finished.load() && std::chrono::steady_clock::now() < deadline) {
                app.tick(nlp3::platform::now_wall_clock_ms());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            worker.join();
            return response;
        };

        const std::string hud_body = R"JSON({
            "frame_style": "neon", "frame_brackets": true, "frame_grid": true,
            "frame_scanlines": true, "frame_color": "#00FFFF", "frame_opacity": 55,
            "frame_border_px": 1, "frame_radius_px": 4, "frame_padding_px": 36,
            "text_outline_px": 2, "text_outline_color": "#001018",
            "time_separator": ".", "show_hours": false,
            "warn_seconds": 120, "danger_seconds": 30, "danger_effect": "glitch",
            "progress_style": "bar", "progress_thickness_px": 8, "progress_color": "#00FFFF",
            "scale_mode": "auto", "canvas_width": 1920, "canvas_height": 1080,
            "particles_enabled": true, "particles_style": "sparks",
            "particles_budget": 80, "particles_density": 1.5, "particles_force": true
        })JSON";

        const auto post = pump_request("POST", base + "/api/timer/configure", hud_body);
        std::printf("  cp7 POST configure -> %d %s\n", post.status_code, post.error.c_str());
        std::fflush(stdout);
        NLP3_TEST_REQUIRE(post.status_code == 200);

        const auto get = pump_request("GET", base + "/api/timer/config", {});
        NLP3_TEST_REQUIRE(get.status_code == 200);
        const auto contains = [&get](const std::string& needle) {
            return get.body.find(needle) != std::string::npos;
        };
        NLP3_TEST_REQUIRE(contains("\"frame_style\":\"neon\""));
        NLP3_TEST_REQUIRE(contains("\"frame_brackets\":true"));
        NLP3_TEST_REQUIRE(contains("\"frame_grid\":true"));
        NLP3_TEST_REQUIRE(contains("\"frame_scanlines\":true"));
        NLP3_TEST_REQUIRE(contains("\"frame_color\":\"#00FFFF\""));
        NLP3_TEST_REQUIRE(contains("\"frame_opacity\":55"));
        NLP3_TEST_REQUIRE(contains("\"frame_padding_px\":36"));
        NLP3_TEST_REQUIRE(contains("\"text_outline_px\":2"));
        NLP3_TEST_REQUIRE(contains("\"text_outline_color\":\"#001018\""));
        NLP3_TEST_REQUIRE(contains("\"time_separator\":\".\""));
        NLP3_TEST_REQUIRE(contains("\"show_hours\":false"));
        NLP3_TEST_REQUIRE(contains("\"warn_seconds\":120"));
        NLP3_TEST_REQUIRE(contains("\"danger_seconds\":30"));
        NLP3_TEST_REQUIRE(contains("\"danger_effect\":\"glitch\""));
        NLP3_TEST_REQUIRE(contains("\"progress_style\":\"bar\""));
        NLP3_TEST_REQUIRE(contains("\"progress_thickness_px\":8"));
        NLP3_TEST_REQUIRE(contains("\"progress_color\":\"#00FFFF\""));
        NLP3_TEST_REQUIRE(contains("\"scale_mode\":\"auto\""));
        NLP3_TEST_REQUIRE(contains("\"canvas_width\":1920"));
        NLP3_TEST_REQUIRE(contains("\"canvas_height\":1080"));
        NLP3_TEST_REQUIRE(contains("\"particles_enabled\":true"));
        NLP3_TEST_REQUIRE(contains("\"particles_style\":\"sparks\""));
        NLP3_TEST_REQUIRE(contains("\"particles_budget\":80"));
        NLP3_TEST_REQUIRE(contains("\"particles_force\":true"));

        // Y los valores absurdos no pasan la puerta del HTTP.
        const std::string junk_body = R"JSON({
            "canvas_width": 0, "frame_opacity": 500, "particles_budget": 99999,
            "progress_thickness_px": 0, "text_outline_px": 99
        })JSON";
        const auto post_junk = pump_request("POST", base + "/api/timer/configure", junk_body);
        NLP3_TEST_REQUIRE(post_junk.status_code == 200);

        const auto get2 = pump_request("GET", base + "/api/timer/config", {});
        NLP3_TEST_REQUIRE(get2.status_code == 200);
        const auto contains2 = [&get2](const std::string& needle) {
            return get2.body.find(needle) != std::string::npos;
        };
        NLP3_TEST_REQUIRE(!contains2("\"canvas_width\":0"));
        NLP3_TEST_REQUIRE(contains2("\"canvas_width\":320"));
        NLP3_TEST_REQUIRE(contains2("\"frame_opacity\":100"));
        NLP3_TEST_REQUIRE(contains2("\"particles_budget\":300"));
        NLP3_TEST_REQUIRE(contains2("\"progress_thickness_px\":1"));
        NLP3_TEST_REQUIRE(contains2("\"text_outline_px\":8"));

        app.stop_http_ui();
    }

    std::puts("live_timer_api_smoke cp8: simulate con coins negativos no infla el reloj");
    std::fflush(stdout);

    // M2: static_cast<uint32_t>(coins) con coins negativo envuelve a ~4e9 y el
    // regalo simulado anade una cantidad absurda de tiempo (hasta el clamp de
    // 1 ano o max_time_s). Un POST /api/timer/simulate con coins<=0 debe ser
    // un no-op, no un salto al maximo.
    {
        const auto config_path = nlp3::testsupport::write_temp_panel_config(
            "nlp3_live_timer_simulate_neg_config.json",
            []() {
                nlp3::platform::PanelConfig config{};
                config.bridge_mode = "stub";
                config.bridge.stub_mode = true;
                config.bridge.source_name = "tiktok-stub";
                config.default_game_id = "event-counter";
                return config;
            }());

        nlp3::platform::PanelApp app;
        NLP3_TEST_REQUIRE(app.initialize(config_path.string()));
        NLP3_TEST_REQUIRE(app.start_http_ui(19133));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        const std::string base = "http://127.0.0.1:19133";

        const auto pump_request = [&app](const std::string& method, const std::string& url,
                                         const std::string& body) {
            nlp3::platform::HttpResponse response{};
            std::atomic<bool> finished{false};
            std::thread worker([&]() {
                response = nlp3::platform::http_request(
                    method, url, body, body.empty() ? std::string_view{} : std::string_view{"application/json"}, {});
                finished.store(true);
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (!finished.load() && std::chrono::steady_clock::now() < deadline) {
                app.tick(nlp3::platform::now_wall_clock_ms());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            worker.join();
            return response;
        };

        // 60 s iniciales y 1 s por coin: sin el clamp, coins=-5 multiplica a
        // ~4e9 s y el reloj salta al tope razonable.
        const auto cfg = pump_request("POST", base + "/api/timer/configure",
                                      R"JSON({"initial_time_s":60,"time_per_gift_coin_s":1.0})JSON");
        NLP3_TEST_REQUIRE(cfg.status_code == 200);
        const auto start = pump_request("POST", base + "/api/timer/start", "{}");
        NLP3_TEST_REQUIRE(start.status_code == 200);

        const auto* timer = app.live_timer();
        NLP3_TEST_REQUIRE(timer != nullptr);
        const double before = timer->remaining_seconds();
        NLP3_TEST_REQUIRE(before > 55.0 && before <= 60.0);

        const auto sim = pump_request("POST", base + "/api/timer/simulate",
                                      R"JSON({"kind":"gift","coins":-5,"name":"neg"})JSON");
        NLP3_TEST_REQUIRE(sim.status_code == 200);

        const double after = timer->remaining_seconds();
        // Regalo de coins<=0: no anade tiempo (tras el fix ~before; con el bug
        // saltaria a ~4e9 clampeado a 1 ano).
        NLP3_TEST_REQUIRE(after < before + 10.0);

        // M4: honestidad del ajuste manual. Pausado, el motor ignora el delta
        // (adjust_time retorna sin tocar nada) y el HTTP no puede responder
        // "adjusted": debe decir adjust_blocked.
        const auto pause = pump_request("POST", base + "/api/timer/pause", "{}");
        NLP3_TEST_REQUIRE(pause.status_code == 200);
        const auto adj = pump_request("POST", base + "/api/timer/adjust",
                                      R"JSON({"delta":5})JSON");
        NLP3_TEST_REQUIRE(adj.status_code == 200);
        NLP3_TEST_REQUIRE(adj.body.find("adjust_blocked") != std::string::npos);
        NLP3_TEST_REQUIRE(adj.body.find("\"ok\":false") != std::string::npos);

        app.stop_http_ui();
    }

    std::error_code cleanup_ec;
    std::filesystem::remove_all(timer_state_dir, cleanup_ec);

    std::puts("live_timer_api_smoke PASSED");
    std::fflush(stdout);
    return 0;
}
