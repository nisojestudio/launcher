#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

#include "games/live_timer_game.hpp"
#include "events/host_event.hpp"
#include "gamesdk/game_input_event.hpp"
#include "host/session_state.hpp"
#include "platform/overlay_assets.hpp"

namespace {

using nlp3::events::HostEvent;
using nlp3::events::HostEventKind;
using nlp3::events::HostActor;
using nlp3::events::GiftEventData;
using nlp3::games::LiveTimerGame;
using nlp3::games::LiveTimerGameFactory;
using nlp3::gamesdk::GameInputEvent;
using nlp3::gamesdk::GameInputEventKind;
using nlp3::gamesdk::GameInputActor;
using nlp3::gamesdk::GameInputGift;
using nlp3::host::HostSessionSnapshot;

HostSessionSnapshot kEmptySnapshot{};

GameInputActor test_actor() {
    return {"user_1", "testuser", "Test User", ""};
}

GameInputEvent make_test_event(GameInputEventKind kind) {
    GameInputEvent ev;
    ev.kind = kind;
    ev.actor = test_actor();
    ev.like_count = 1;
    return ev;
}

GameInputEvent make_gift_event(std::uint32_t diamond_count) {
    GameInputEvent ev;
    ev.kind = GameInputEventKind::gift;
    ev.actor = test_actor();
    ev.gift = GameInputGift{"gift_1", "Rose", 1, diamond_count};
    return ev;
}

void test_default_config() {
    LiveTimerGame game;
    auto config = game.default_config();

    // V2: el default es 0. El timer no arranca con tiempo hasta que el usuario
    // lo configure (antes habia un default de 300s = 05:00).
    assert(config.get_double("initial_time_s", 0) == 0.0);
    assert(config.get_double("time_per_like_s", 0) == 0.0);
    assert(config.get_double("time_per_share_s", 0) == 0.0);
    assert(config.get_double("time_per_follow_s", 0) == 0.0);
    assert(config.get_double("time_per_gift_coin_s", 0) == 0.0);
    assert(config.get_double("time_per_chat_s", 0) == 0.0);
    assert(config.get_string("title_text", "") == "\xf0\x9f\x8e\xaf Extiende el Live");
    assert(config.get_bool("title_bold", false) == true);
    assert(config.get_int("counter_font_size", 0) == 120);

    std::cout << "PASS: default_config\n";
}

void test_manifest() {
    LiveTimerGame game;
    auto m = game.manifest();
    assert(m.game_id == "live-timer");
    assert(m.display_name == "Live Timer");
    assert(m.capabilities.uses_gifts == true);
    assert(m.capabilities.uses_follows == true);
    assert(m.capabilities.uses_shares == true);

    LiveTimerGameFactory factory;
    assert(factory.manifest().game_id == "live-timer");

    auto created_game = factory.create();
    assert(created_game != nullptr);
    assert(created_game->game_id() == "live-timer");

    std::cout << "PASS: manifest\n";
}

void test_on_activated_starts_timer() {
    LiveTimerGame game;

    // V2: sin tiempo configurado on_activated NO arranca nada. El default es 0,
    // asi que no puede quedar un contador corriendo sobre cero.
    game.on_activated();
    assert(game.state().running == false);
    assert(std::abs(game.remaining_seconds() - 0.0) < 1.0);

    // Con tiempo configurado si arranca, y carga el valor inicial.
    auto config = game.default_config();
    config.set("initial_time_s", 300.0);
    game.apply_config(config);
    game.on_activated();

    auto rem = game.remaining_seconds();
    assert(std::abs(rem - 300.0) < 1.0);
    assert(game.state().running == true);
    assert(game.state().completed == false);

    std::cout << "PASS: on_activated starts timer\n";
}

void test_like_adds_time() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("time_per_like_s", 2.0);
    game.apply_config(cfg);
    game.on_activated();
    auto before = game.remaining_seconds();

    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(after >= before + 1.9);
    assert(!game.state().recent_events.empty());
    assert(game.state().recent_events.back().delta_seconds >= 1.9);

    std::cout << "PASS: like adds time\n";
}

void test_share_adds_time() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("time_per_share_s", 5.0);
    game.apply_config(cfg);
    game.on_activated();
    auto before = game.remaining_seconds();

    game.on_game_input_event(make_test_event(GameInputEventKind::share), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(after >= before + 4.9);

    std::cout << "PASS: share adds time\n";
}

void test_follow_adds_time() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("time_per_follow_s", 10.0);
    game.apply_config(cfg);
    game.on_activated();
    auto before = game.remaining_seconds();

    game.on_game_input_event(make_test_event(GameInputEventKind::follow), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(after >= before + 9.9);

    std::cout << "PASS: follow adds time\n";
}

void test_gift_adds_time_based_on_diamonds() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("time_per_gift_coin_s", 0.5);
    game.apply_config(cfg);
    game.on_activated();
    auto before = game.remaining_seconds();

    game.on_game_input_event(make_gift_event(100), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(after >= before + 49.0); // 100 * 0.5 = 50s

    std::cout << "PASS: gift adds time based on diamonds\n";
}

void test_negative_config_removes_time() {
    LiveTimerGame game;
    auto config = game.default_config();
    // V2: hace falta un tiempo base. Con el default ahora en 0 la resta se
    // clampearia a 0 y el test no probaria nada (0 no puede bajar de 0).
    config.set("initial_time_s", 100.0);
    config.set("time_per_like_s", -1.5);
    game.apply_config(config);
    game.on_activated();

    auto before = game.remaining_seconds();

    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(after <= before - 1.0);
    assert(after > 0.0);

    std::cout << "PASS: negative config removes time\n";
}

void test_timer_decrements_in_real_time() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 3.0);
    game.apply_config(config);
    game.on_activated();

    auto before = game.remaining_seconds();
    assert(std::abs(before - 3.0) < 0.1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    auto after = game.remaining_seconds();
    assert(after < before);
    assert(std::abs(after - 1.5) < 0.2);

    std::cout << "PASS: timer decrements in real time\n";
}

void test_format_time_display() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 3661.0);
    game.apply_config(config);
    game.on_activated();

    // Test HH:MM:SS (just over 1 hour)
    auto fmt = game.format_time();
    assert(fmt.find("dia") == std::string::npos);

    // Test with days
    config.set("initial_time_s", 90061.0); // 1 day 1:01:01
    // V2 (Fase 2): para reconfigurar el tiempo hay que estar en reposo. Con el
    // timer corriendo, apply_config no toca el reloj a proposito (asi cambiar
    // el diseno no puede mover la cuenta).
    game.stop();
    game.apply_config(config);
    game.on_activated();
    fmt = game.format_time();
    assert(fmt.find("dia") != std::string::npos);

    // Test zero
    config.set("initial_time_s", 0.0);
    game.stop();
    game.apply_config(config);
    game.on_activated();
    fmt = game.format_time();
    assert(fmt.find("00:00:00") != std::string::npos);

    std::cout << "PASS: format_time_display\n";
}

void test_telemetry_includes_timer_state() {
    LiveTimerGame game;
    game.on_activated();

    auto telemetry = game.telemetry();
    bool found_remaining = false;
    bool found_running = false;
    for (const auto& item : telemetry) {
        if (item.key == "remaining_seconds") found_remaining = true;
        if (item.key == "running") found_running = true;
    }
    assert(found_remaining);
    assert(found_running);

    std::cout << "PASS: telemetry includes timer state\n";
}

void test_completion_sound_poll() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 0.5);
    game.apply_config(config);
    game.on_activated();

    // Wait for timer to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // poll_completion_sound should trigger
    bool triggered = game.poll_completion_sound();
    if (!triggered) {
        // try once more in case timing is tight
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        triggered = game.poll_completion_sound();
    }
    assert(triggered);
    assert(game.state().completed);

    // Second call should return false (already triggered)
    assert(!game.poll_completion_sound());

    std::cout << "PASS: completion sound poll\n";
}

void test_chat_default_is_zero() {
    LiveTimerGame game;
    game.on_activated();
    auto before = game.remaining_seconds();

    game.on_game_input_event(make_test_event(GameInputEventKind::chat_message), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(std::abs(after - before) < 0.1);

    std::cout << "PASS: chat default is zero\n";
}

void test_pause_resume() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 60.0);
    game.apply_config(config);
    game.on_activated();

    assert(game.is_running());
    assert(!game.state().paused);

    double before = game.remaining_seconds();
    game.pause();
    assert(!game.is_running());
    assert(game.state().paused);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    double after_pause = game.remaining_seconds();
    assert(std::abs(after_pause - before) < 0.1);

    game.resume();
    assert(game.is_running());
    assert(!game.state().paused);
    assert(std::abs(game.remaining_seconds() - after_pause) < 0.1);

    std::cout << "PASS: pause_resume\n";
}

void test_set_enabled() {
    LiveTimerGame game;
    game.on_activated();

    assert(game.is_enabled());
    game.set_enabled(false);
    assert(!game.is_enabled());
    assert(!game.is_running());

    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    assert(game.state().recent_events.empty());

    game.set_enabled(true);
    assert(game.is_enabled());

    std::cout << "PASS: set_enabled\n";
}

void test_event_blocked_when_paused() {
    LiveTimerGame game;
    game.on_activated();
    game.pause();

    auto before = game.remaining_seconds();
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    auto after = game.remaining_seconds();

    assert(std::abs(after - before) < 0.1);
    assert(game.state().recent_events.empty());

    std::cout << "PASS: event_blocked_when_paused\n";
}

void test_event_blocked_when_disabled() {
    LiveTimerGame game;
    game.on_activated();
    game.set_enabled(false);

    auto before = game.remaining_seconds();
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    auto after = game.remaining_seconds();

    assert(std::abs(after - before) < 0.1);
    assert(game.state().recent_events.empty());

    std::cout << "PASS: event_blocked_when_disabled\n";
}

void test_reset_restarts_timer() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 3.0);
    game.apply_config(config);
    game.on_activated();

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    double before_reset = game.remaining_seconds();
    assert(before_reset < 3.0);

    game.reset();
    assert(game.is_running());
    assert(!game.state().paused);
    assert(!game.state().completed);
    assert(std::abs(game.remaining_seconds() - 3.0) < 0.1);

    std::cout << "PASS: reset_restarts_timer\n";
}

void test_factory_creates() {
    LiveTimerGameFactory factory;
    auto created_game = factory.create();
    assert(created_game != nullptr);
    assert(created_game->game_id() == "live-timer");
    auto m = created_game->manifest();
    assert(m.game_id == "live-timer");

    std::cout << "PASS: factory creates\n";
}

void test_stop() {
    LiveTimerGame game;
    // V2: hay que configurar tiempo para que on_activated deje el timer
    // corriendo; con el default en 0 no arranca y stop() no tendria nada que
    // parar.
    auto config = game.default_config();
    config.set("initial_time_s", 10.0);
    game.apply_config(config);
    game.on_activated();
    assert(game.is_running());

    game.stop();
    assert(!game.is_running());
    assert(!game.state().paused);
    assert(game.state().completed);
    assert(game.remaining_seconds() == 0.0);

    std::cout << "PASS: stop\n";
}

void test_max_time_s_limits_addition() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 10.0);
    config.set("max_time_s", 15.0);
    config.set("time_per_like_s", 2.0);
    config.set("time_per_follow_s", 10.0);
    game.apply_config(config);
    game.on_activated();

    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    game.on_game_input_event(make_test_event(GameInputEventKind::follow), kEmptySnapshot);

    auto rem = game.remaining_seconds();
    assert(rem <= 15.1);

    std::cout << "PASS: max_time_s limits addition\n";
}

void test_substitute_placeholders_shared() {
    nlp3::games::LiveTimerGameState state;
    state.time_per_like = 2.5;
    state.time_per_share = 5.0;
    state.time_per_gift_coin = 0.5;
    state.initial_seconds = 300.0;
    state.title_text = "Test Title";

    auto result = nlp3::games::substitute_timer_placeholders(
        "{title}: {time_per_like}s per like", state);
    assert(result == "Test Title: 2.5s per like");

    result = nlp3::games::substitute_timer_placeholders(
        "{initial_time}s initial", state);
    assert(result == "300s initial");

    std::cout << "PASS: substitute_placeholders_shared\n";
}

void test_reset_config_to_defaults() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 999.0);
    game.apply_config(config);
    assert(game.config().get_double("initial_time_s", 0) == 999.0);

    game.reset_config_to_defaults();
    // V2: el default es 0, no 300.
    assert(game.config().get_double("initial_time_s", 0) == 0.0);

    std::cout << "PASS: reset_config_to_defaults\n";
}

void test_font_size_changes() {
    nlp3::games::LiveTimerGame game;
    
    // Default check
    assert(game.state().title_style.font_size_px == 48);
    assert(game.state().counter_style.font_size_px == 120);
    assert(game.state().subtitle_style.font_size_px == 32);
    
    // Apply new font sizes via partial config (simulating frontend HTTP request)
    nlp3::gamesdk::GameConfig config;
    config.set("title_font_size", std::int64_t{72});
    config.set("counter_font_size", std::int64_t{200});
    config.set("subtitle_font_size", std::int64_t{50});
    
    game.apply_config(config);
    
    // Read them back from state
    assert(game.state().title_style.font_size_px == 72);
    assert(game.state().counter_style.font_size_px == 200);
    assert(game.state().subtitle_style.font_size_px == 50);
    
    // Verify config_ has them too
    assert(game.config().get_int("title_font_size", 0) == 72);
    assert(game.config().get_int("counter_font_size", 0) == 200);
    assert(game.config().get_int("subtitle_font_size", 0) == 50);
    
    // Partial update preserves unchanged values
    nlp3::gamesdk::GameConfig config2;
    config2.set("title_font_size", std::int64_t{36});
    game.apply_config(config2);
    assert(game.state().title_style.font_size_px == 36);
    assert(game.state().counter_style.font_size_px == 200); // unchanged
    
    // Font size + effects together
    nlp3::gamesdk::GameConfig config3;
    config3.set("title_font_size", std::int64_t{60});
    config3.set("title_effect", std::string("pulse"));
    config3.set("title_glow_enabled", true);
    game.apply_config(config3);
    assert(game.state().title_style.font_size_px == 60);
    assert(game.state().title_effect == "pulse");
    assert(game.state().title_glow_enabled == true);
    
    // Reset restores defaults
    game.reset_config_to_defaults();
    assert(game.state().title_style.font_size_px == 48);
    assert(game.state().counter_style.font_size_px == 120);
    assert(game.state().subtitle_style.font_size_px == 32);
    
    std::cout << "PASS: font_size_changes\n";
}

void test_remaining_seconds_auto_completed() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 0.01);
    game.apply_config(config);
    game.on_activated();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // T1.1: remaining_seconds() is a pure read; poll_completion_sound() drives
    // tick() which is what commits the completion to the SSOT state.
    game.poll_completion_sound();

    auto rem = game.remaining_seconds();
    assert(rem == 0.0);
    assert(game.state().completed);
    assert(!game.state().running);

    std::cout << "PASS: remaining_seconds_auto_completed\n";
}

// Fase 0 regression test
void test_pause_remaining_seconds_correct() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 300.0);
    game.apply_config(config);
    game.on_activated();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    game.pause();
    double at_pause = game.remaining_seconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    double after_pause = game.remaining_seconds();

    assert(std::abs(at_pause - 299.5) < 0.15);
    assert(std::abs(after_pause - at_pause) < 0.05);
    assert(game.state().paused);

    std::cout << "PASS: pause_remaining_seconds_correct\n";
}

// EXPECTED-FAIL until T1.1/T2.1
// Fase 0 regression test
void test_apply_config_completed_does_not_inflate() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 0.01);
    game.apply_config(config);
    game.on_activated();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bool triggered = game.poll_completion_sound();
    if (!triggered) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        triggered = game.poll_completion_sound();
    }
    assert(triggered);
    assert(game.state().completed);
    assert(game.remaining_seconds() == 0.0);

    auto config2 = game.default_config();
    config2.set("initial_time_s", 600.0);
    game.apply_config(config2);

    assert(game.remaining_seconds() == 0.0);
    assert(game.state().completed);

    std::cout << "PASS: apply_config_completed_does_not_inflate\n";
}

// EXPECTED-FAIL until T2.1
// Fase 0 regression test
void test_apply_config_paused_does_not_alter_remaining() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 50.0);
    game.apply_config(config);
    game.on_activated();
    game.pause();
    double before = game.remaining_seconds();

    auto config2 = game.default_config();
    config2.set("initial_time_s", 120.0);
    game.apply_config(config2);

    double after = game.remaining_seconds();
    assert(std::abs(after - before) < 0.1);

    std::cout << "PASS: apply_config_paused_does_not_alter_remaining\n";
}

// V2 (Fase 2): cambiar configuracion con el timer CORRIENDO no debe mover el
// reloj. Era el bug reportado: el panel mandaba 30 claves juntas cada vez que
// tocabas un control visual, asi que cambiar un color cambiaba el tiempo.
void test_design_config_change_does_not_move_the_clock() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 600.0);
    game.apply_config(config);
    game.on_activated();
    assert(game.is_running());

    const double before = game.remaining_seconds();

    // 1) Cambio de diseno puro: distinto aspecto, mismos tiempos.
    auto design = game.config();
    design.set("counter_font_color", std::string("#FF00FF"));
    design.set("title_text", std::string("otro titulo"));
    design.set("color_preset", std::string("cyber-blue"));
    design.set("counter_font_size", static_cast<std::int64_t>(200));
    game.apply_config(design);

    assert(std::abs(game.remaining_seconds() - before) < 0.5);
    assert(game.state().counter_style.font_color == "#FF00FF");
    assert(game.state().counter_style.font_size_px == 200);

    // 2) Cambiar initial_time_s en caliente tampoco reajusta la cuenta en
    //    curso: el nuevo valor queda para el PROXIMO arranque.
    auto new_initial = game.config();
    new_initial.set("initial_time_s", 900.0);
    game.apply_config(new_initial);

    assert(std::abs(game.remaining_seconds() - before) < 0.5);
    assert(game.state().initial_seconds == 900.0);

    std::cout << "PASS: design_config_change_does_not_move_the_clock\n";
}

// EXPECTED-FAIL until T2.3
// Fase 0 regression test
void test_restore_state_running_paused_keeps_paused() {
    LiveTimerGame game;
    game.restore_state(50.0, true, true, false, true);
    assert(game.state().paused == true);
    assert(game.state().running == false);

    std::cout << "PASS: restore_state_running_paused_keeps_paused\n";
}

// EXPECTED-FAIL until T1.3
// Fase 0 regression test
void test_event_id_monotonic_across_arm() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("time_per_like_s", 2.0);
    game.apply_config(cfg);
    game.on_activated();
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    assert(!game.state().recent_events.empty());
    assert(game.state().recent_events.back().id == 3);

    game.arm();
    assert(game.event_id_counter() == 3);

    game.on_activated();
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    assert(!game.state().recent_events.empty());
    assert(game.state().recent_events.back().id == 4);

    std::cout << "PASS: event_id_monotonic_across_arm\n";
}

// EXPECTED-FAIL until T1.1
// Fase 0 regression test
void test_poll_tick_sound_below_60s() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 65.0);
    game.apply_config(config);
    game.on_activated();

    std::this_thread::sleep_for(std::chrono::milliseconds(6000));

    bool any_true = false;
    for (int i = 0; i < 10; ++i) {
        if (game.poll_tick_sound()) any_true = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(any_true);

    std::cout << "PASS: poll_tick_sound_below_60s\n";
}

// EXPECTED-FAIL until T1.3
// Fase 0 regression test
void test_build_live_timer_state_json_contract() {
    LiveTimerGame game;
    game.apply_config(game.default_config());
    game.on_activated();

    const std::string json = nlp3::platform::build_live_timer_state_json(&game);
    assert(json.find("\"remainingSeconds\"") != std::string::npos);
    assert(json.find("\"running\"") != std::string::npos);
    assert(json.find("\"paused\"") != std::string::npos);
    assert(json.find("\"enabled\"") != std::string::npos);
    assert(json.find("\"completed\"") != std::string::npos);
    assert(json.find("\"recentEvents\"") != std::string::npos);
    assert(json.find("\"sessionId\"") != std::string::npos);

    std::cout << "PASS: build_live_timer_state_json_contract\n";
}

// EXPECTED-FAIL until T2.6/T1.3
// Fase 0 regression test
void test_set_enabled_preserves_runtime() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 30.0);
    config.set("time_per_like_s", 2.0);
    game.apply_config(config);
    game.on_activated();
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    assert(!game.state().recent_events.empty());
    assert(game.state().recent_events.back().id == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    double R = game.remaining_seconds();
    assert(R > 0.0);

    game.set_enabled(false);
    assert(!game.is_enabled());
    game.set_enabled(true);
    assert(game.is_enabled());

    double after = game.remaining_seconds();
    assert(after > 0.0);
    assert(std::abs(after - R) < 0.2);

    assert(game.event_id_counter() == 1);

    game.on_activated();
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    assert(!game.state().recent_events.empty());
    assert(game.state().recent_events.back().id == 2);

    std::cout << "PASS: set_enabled_preserves_runtime\n";
}

// A7: adjust_time clamp by max_time_s reports the actually-applied delta
// in the popup (not the requested delta).
void test_adjust_time_clamp_reports_real_delta() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 60.0);
    config.set("max_time_s", 10.0);
    game.apply_config(config);
    game.on_activated();

    assert(std::abs(game.remaining_seconds() - 60.0) < 0.1);
    game.adjust_time(100.0);
    assert(std::abs(game.remaining_seconds() - 10.0) < 0.01);
    assert(!game.state().recent_events.empty());
    const auto& last_event = game.state().recent_events.back();
    // We were at ~60 and capped at 10, so the real delta is ~-50, not 100.
    assert(last_event.delta_seconds < 0.0);
    assert(std::abs(last_event.delta_seconds - (-50.0)) < 1.0);

    std::cout << "PASS: adjust_time_clamp_reports_real_delta\n";
}

// A8: an event that exhausts the timer (negative delta brings remaining
// below 0) skips the popup so the overlay does not flash "-Xs" alongside
// the completion confetti.
void test_event_exhausts_timer_skips_popup() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 5.0);
    config.set("time_per_like_s", -10.0);
    game.apply_config(config);
    game.on_activated();

    assert(game.state().recent_events.empty());
    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    assert(game.state().completed);
    // The popup was suppressed because this event is what completed the timer.
    assert(game.state().recent_events.empty());
    assert(game.event_id_counter() == 0);

    std::cout << "PASS: event_exhausts_timer_skips_popup\n";
}

// A12: non-finite input to adjust_time is ignored, and non-finite input
// to on_game_input_event is sanitized so the SSOT never holds NaN/inf.
void test_non_finite_input_is_sanitized() {
    LiveTimerGame game;
    game.on_activated();
    const double before = game.remaining_seconds();
    game.adjust_time(std::numeric_limits<double>::quiet_NaN());
    assert(std::abs(game.remaining_seconds() - before) < 0.01);
    game.adjust_time(std::numeric_limits<double>::infinity());
    assert(std::isfinite(game.remaining_seconds()));
    assert(game.remaining_seconds() < 365.0 * 86400.0);

    // apply_config with NaN must not propagate. Se fija antes un valor valido
    // conocido: con el default ahora en 0, "initial_seconds > 0" ya no probaria
    // que el NaN se descarto (el valor previo tambien seria 0).
    nlp3::gamesdk::GameConfig valid;
    valid.set("initial_time_s", 120.0);
    game.apply_config(valid);
    assert(game.state().initial_seconds == 120.0);

    nlp3::gamesdk::GameConfig bad;
    bad.set("initial_time_s", std::numeric_limits<double>::quiet_NaN());
    game.apply_config(bad);
    assert(std::isfinite(game.remaining_seconds()));
    // NaN dropped on the floor; the previous valid initial_seconds is kept.
    assert(game.state().initial_seconds == 120.0);

    std::cout << "PASS: non_finite_input_is_sanitized\n";
}

// Round-trip a timer state through the JSON envelope used by PanelApp.
void test_state_json_round_trip() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("initial_time_s", 30.0);
    config.set("time_per_gift_coin_s", 1.5);
    config.set("time_per_like_s", 2.0);
    config.set("title_text", std::string("hola mundo"));
    game.apply_config(config);
    game.on_activated();
    for (int i = 0; i < 4; ++i) {
        game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    }
    assert(game.event_id_counter() == 4);

    // Serialise
    const std::string json = nlp3::platform::build_live_timer_state_json(&game);
    assert(json.find("\"remainingSeconds\"") != std::string::npos);
    assert(json.find("\"title\":\"hola mundo\"") != std::string::npos);
    assert(json.find("\"sessionId\"") != std::string::npos);

    // Now build a fresh game, simulate the panel restoring from this snapshot.
    LiveTimerGame restored;
    auto restored_cfg = restored.default_config();
    restored_cfg.set("time_per_like_s", 2.0);
    restored.apply_config(restored_cfg);
    // The PanelApp side does: apply_config(saved_config); restore_state(...).
    // We replicate the minimum we need: bring back the runtime fields.
    restored.restore_state(15.0, true, false, false, true,
                           game.event_id_counter(), game.session_id(),
                           game.total_time_added());
    assert(restored.is_running());
    assert(!restored.state().paused);
    assert(!restored.state().completed);
    // event_id counter was preserved across the round trip.
    restored.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    assert(!restored.state().recent_events.empty());
    assert(restored.state().recent_events.back().id == 5);

    std::cout << "PASS: state_json_round_trip\n";
}

// --- V3 feature tests ---
void test_v3_digit_effect_validation() {
    LiveTimerGame game;
    auto config = game.default_config();

    // Valid effects accepted
    config.set("digit_effect", std::string("flip"));
    game.apply_config(config);
    assert(game.state().digit_effect == "flip");
    assert(game.config().get_string("digit_effect") == "flip");

    config.set("digit_effect", std::string("roll"));
    game.apply_config(config);
    assert(game.state().digit_effect == "roll");

    config.set("digit_effect", std::string("pop"));
    game.apply_config(config);
    assert(game.state().digit_effect == "pop");

    config.set("digit_effect", std::string("fade"));
    game.apply_config(config);
    assert(game.state().digit_effect == "fade");

    // Invalid effects normalized to "none"
    config.set("digit_effect", std::string("invalid_effect"));
    game.apply_config(config);
    assert(game.state().digit_effect == "none");
    assert(game.config().get_string("digit_effect") == "none");

    std::cout << "PASS: v3_digit_effect_validation\n";
}

void test_v3_color_preset_validation() {
    LiveTimerGame game;
    auto config = game.default_config();

    // Valid presets accepted
    config.set("color_preset", std::string("cyber-blue"));
    game.apply_config(config);
    assert(game.state().color_preset == "cyber-blue");
    assert(game.config().get_string("color_preset") == "cyber-blue");

    config.set("color_preset", std::string("clean-white"));
    game.apply_config(config);
    assert(game.state().color_preset == "clean-white");

    config.set("color_preset", std::string("rose-gold"));
    game.apply_config(config);
    assert(game.state().color_preset == "rose-gold");

    // Invalid preset normalized to "neon-green"
    config.set("color_preset", std::string("invalid_preset"));
    game.apply_config(config);
    assert(game.state().color_preset == "neon-green");
    assert(game.config().get_string("color_preset") == "neon-green");

    std::cout << "PASS: v3_color_preset_validation\n";
}

void test_v3_fields_round_trip() {
    LiveTimerGame game;
    auto config = game.default_config();
    config.set("digit_effect", std::string("flip"));
    config.set("color_preset", std::string("rose-gold"));
    game.apply_config(config);
    game.on_activated();

    // Verify state
    assert(game.state().digit_effect == "flip");
    assert(game.state().color_preset == "rose-gold");

    // Verify JSON serialization includes V3 fields
    const std::string json = nlp3::platform::build_live_timer_state_json(&game);
    assert(json.find("\"digit_effect\":\"flip\"") != std::string::npos);
    assert(json.find("\"color_preset\":\"rose-gold\"") != std::string::npos);

    // Reset to defaults
    game.reset_config_to_defaults();
    assert(game.state().digit_effect == "none");
    assert(game.state().color_preset == "neon-green");

    std::cout << "PASS: v3_fields_round_trip\n";
}

// ============================================================================
// Bloque A — reglas de eventos (M1, M2, M3, M4, M5)
// ============================================================================

void test_m2_likes_por_magnitud() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_like_s", 2.0);
    cfg.set("like_use_magnitude", true);
    game.apply_config(cfg);
    game.on_activated();

    auto like5 = make_test_event(GameInputEventKind::like);
    like5.like_count = 5;
    game.on_game_input_event(like5, kEmptySnapshot);
    // 5 likes a 2 s c/u = 10 s (no 2 s como antes del flag).
    assert(std::abs(game.state().remaining_seconds - 300.0 - 10.0) < 0.5);
    assert(!game.state().recent_events.empty());
    assert(std::abs(game.state().recent_events.back().delta_seconds - 10.0) < 0.01);

    std::cout << "PASS: m2_likes_por_magnitud\n";
}

void test_m2_likes_sin_magnitud_legacy() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 100.0);
    cfg.set("time_per_like_s", 2.0);
    cfg.set("like_use_magnitude", false);
    game.apply_config(cfg);
    game.on_activated();

    auto like5 = make_test_event(GameInputEventKind::like);
    like5.like_count = 5;
    game.on_game_input_event(like5, kEmptySnapshot);
    // Modo historico: un solo cargo por lote.
    assert(std::abs(game.state().remaining_seconds - 100.0 - 2.0) < 0.5);

    std::cout << "PASS: m2_likes_sin_magnitud_legacy\n";
}

void test_m3_multiplicador_suscriptor() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_follow_s", 10.0);
    cfg.set("mult_subscriber", 2.0);
    game.apply_config(cfg);
    game.on_activated();

    auto ev = make_test_event(GameInputEventKind::follow);
    ev.actor.is_subscriber = true;
    game.on_game_input_event(ev, kEmptySnapshot);
    assert(std::abs(game.state().remaining_seconds - 300.0 - 20.0) < 0.5);

    std::cout << "PASS: m3_multiplicador_suscriptor\n";
}

void test_m3_moderador_gana_al_suscriptor_mas_barato() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_follow_s", 10.0);
    cfg.set("mult_subscriber", 5.0);
    cfg.set("mult_moderator", 1.0);
    game.apply_config(cfg);
    game.on_activated();

    // Actor moderador + suscriptor: el multiplicador MAS ALTO gana (la regla
    // beneficia al generoso; no castiga).
    auto ev = make_test_event(GameInputEventKind::follow);
    ev.actor.is_moderator = true;
    ev.actor.is_subscriber = true;
    game.on_game_input_event(ev, kEmptySnapshot);
    assert(std::abs(game.state().remaining_seconds - 300.0 - 50.0) < 0.5);

    std::cout << "PASS: m3_moderador_gana_al_suscriptor_mas_barato\n";
}

void test_m1_popup_con_nombre_actor() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_follow_s", 10.0);
    game.apply_config(cfg);
    game.on_activated();

    auto ev = make_test_event(GameInputEventKind::follow);
    ev.actor.display_name = "musitogamer";
    game.on_game_input_event(ev, kEmptySnapshot);
    assert(!game.state().recent_events.empty());
    assert(game.state().recent_events.back().actor_name == "musitogamer");

    std::cout << "PASS: m1_popup_con_nombre_actor\n";
}

void test_m4_tramo_por_valor_sustituye_multiplicador() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_gift_coin_s", 0.5);   // seria 50 s por 100 coins
    cfg.set("gift_tiers", std::string("1-9: 5\n10-99: 60\n100+: 300\n"));
    game.apply_config(cfg);
    game.on_activated();

    game.on_game_input_event(make_gift_event(100), kEmptySnapshot);
    assert(std::abs(game.state().remaining_seconds - 300.0 - 300.0) < 0.5);

    std::cout << "PASS: m4_tramo_por_valor_sustituye_multiplicador\n";
}

void test_m4_excepcion_por_nombre_sobre_tramo() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_gift_coin_s", 0.5);
    cfg.set("gift_tiers", std::string("1-9: 5\n10-99: 60\n100+: 300\nRosa: 3\n"));
    game.apply_config(cfg);
    game.on_activated();

    auto ev = make_gift_event(1);
    ev.gift->gift_name = "Rosa";
    game.on_game_input_event(ev, kEmptySnapshot);
    assert(std::abs(game.state().remaining_seconds - 300.0 - 3.0) < 0.5);

    std::cout << "PASS: m4_excepcion_por_nombre_sobre_tramo\n";
}

void test_m4_regla_vacia_no_cambia_comportamiento() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_gift_coin_s", 0.5);
    game.apply_config(cfg);
    game.on_activated();

    game.on_game_input_event(make_gift_event(100), kEmptySnapshot);
    assert(std::abs(game.state().remaining_seconds - 300.0 - 50.0) < 0.5);

    std::cout << "PASS: m4_regla_vacia_no_cambia_comportamiento\n";
}

void test_m5_tope_por_evento() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_gift_coin_s", 1.0);
    cfg.set("cap_per_event_s", 10.0);
    game.apply_config(cfg);
    game.on_activated();

    game.on_game_input_event(make_gift_event(500), kEmptySnapshot); // seria 500 s
    assert(std::abs(game.state().remaining_seconds - 300.0 - 10.0) < 0.5);
    assert(!game.state().recent_events.empty());
    assert(game.state().recent_events.back().capped);

    std::cout << "PASS: m5_tope_por_evento\n";
}

void test_m5_tope_por_usuario() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_like_s", 20.0);
    cfg.set("like_use_magnitude", true);
    cfg.set("cap_per_user_per_minute_s", 30.0);
    game.apply_config(cfg);
    game.on_activated();

    auto like = make_test_event(GameInputEventKind::like);
    like.like_count = 1;
    game.on_game_input_event(like, kEmptySnapshot);   // +20 s (quedan 10)
    game.on_game_input_event(like, kEmptySnapshot);   // +10 s (alcanza tope)
    game.on_game_input_event(like, kEmptySnapshot);   // +0 s (tope)
    assert(std::abs(game.state().remaining_seconds - 300.0 - 30.0) < 0.5);
    assert(game.state().recent_events.size() == 2);   // el tercero no genera popup

    std::cout << "PASS: m5_tope_por_usuario\n";
}

void test_m5_tope_total_global() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_like_s", 10.0);
    cfg.set("cap_total_per_minute_s", 25.0);
    game.apply_config(cfg);
    game.on_activated();

    auto like = make_test_event(GameInputEventKind::like);
    like.like_count = 1;
    game.on_game_input_event(like, kEmptySnapshot);
    like.actor.id = "user_2";
    like.actor.display_name = "Otros";
    game.on_game_input_event(like, kEmptySnapshot);
    like.actor.id = "user_3";
    like.actor.display_name = "Tercero";
    game.on_game_input_event(like, kEmptySnapshot);
    // 10 + 10 + 5 = 25 s total (el tercero se recorta al resto).
    assert(std::abs(game.state().remaining_seconds - 300.0 - 25.0) < 0.5);

    std::cout << "PASS: m5_tope_total_global\n";
}

void test_m5_suelo_no_completa() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_like_s", -800.0);   // resta brutal
    cfg.set("floor_time_s", 60.0);
    game.apply_config(cfg);
    game.on_activated();

    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);
    // El reloj llego a 60 s (suelo), se mantiene corriendo: no completa.
    assert(std::abs(game.state().remaining_seconds - 60.0) < 0.5);
    assert(game.state().running);
    assert(!game.state().completed);

    std::cout << "PASS: m5_suelo_no_completa\n";
}

void test_bloque_a_serializacion_en_json() {
    LiveTimerGame game;
    auto cfg = game.default_config();
    cfg.set("initial_time_s", 300.0);
    cfg.set("time_per_follow_s", 10.0);
    cfg.set("mult_subscriber", 2.0);
    cfg.set("cap_per_event_s", 5.0);
    cfg.set("floor_time_s", 30.0);
    cfg.set("gift_tiers", std::string("100+: 300"));
    game.apply_config(cfg);
    game.on_activated();

    // Un evento con actor para que el JSON lleve su nombre en el popup.
    auto ev = make_test_event(GameInputEventKind::follow);
    ev.actor.display_name = "viewer99";
    game.on_game_input_event(ev, kEmptySnapshot);
    const std::string json = nlp3::platform::build_live_timer_state_json(&game);
    // Los popups serializan actor + "capped" en el estado del overlay.
    assert(json.find("\"actorName\":\"viewer99\"") != std::string::npos);
    // El evento suma 10 s pero cap_per_event_s = 5: queda recortado y marcado.
    assert(json.find("\"capped\":true") != std::string::npos);

    // Config expone las nuevas claves al panel.
    const auto& c = game.config();
    assert(c.get_double("mult_subscriber", 0.0) == 2.0);
    assert(c.get_double("cap_per_event_s", 0.0) == 5.0);
    assert(c.get_string("gift_tiers", "") == "100+: 300");

    std::cout << "PASS: bloque_a_serializacion_en_json\n";
}

} // namespace

int main() {
    test_default_config();
    test_manifest();
    test_on_activated_starts_timer();
    test_like_adds_time();
    test_share_adds_time();
    test_follow_adds_time();
    test_gift_adds_time_based_on_diamonds();
    test_negative_config_removes_time();
    test_timer_decrements_in_real_time();
    test_format_time_display();
    test_telemetry_includes_timer_state();
    test_completion_sound_poll();
    test_chat_default_is_zero();
    test_pause_resume();
    test_set_enabled();
    test_event_blocked_when_paused();
    test_event_blocked_when_disabled();
    test_reset_restarts_timer();
    test_factory_creates();
    test_stop();
    test_max_time_s_limits_addition();
    test_substitute_placeholders_shared();
    test_reset_config_to_defaults();
    test_remaining_seconds_auto_completed();
    test_font_size_changes();
    test_pause_remaining_seconds_correct();
    test_apply_config_completed_does_not_inflate();
    test_apply_config_paused_does_not_alter_remaining();
    test_design_config_change_does_not_move_the_clock();
    test_restore_state_running_paused_keeps_paused();
    test_event_id_monotonic_across_arm();
    test_poll_tick_sound_below_60s();
    test_build_live_timer_state_json_contract();
    test_set_enabled_preserves_runtime();
    test_adjust_time_clamp_reports_real_delta();
    test_event_exhausts_timer_skips_popup();
    test_non_finite_input_is_sanitized();
    test_state_json_round_trip();
    test_v3_digit_effect_validation();
    test_v3_color_preset_validation();
    test_v3_fields_round_trip();

    // Bloque A — reglas de eventos
    test_m2_likes_por_magnitud();
    test_m2_likes_sin_magnitud_legacy();
    test_m3_multiplicador_suscriptor();
    test_m3_moderador_gana_al_suscriptor_mas_barato();
    test_m1_popup_con_nombre_actor();
    test_m4_tramo_por_valor_sustituye_multiplicador();
    test_m4_excepcion_por_nombre_sobre_tramo();
    test_m4_regla_vacia_no_cambia_comportamiento();
    test_m5_tope_por_evento();
    test_m5_tope_por_usuario();
    test_m5_tope_total_global();
    test_m5_suelo_no_completa();
    test_bloque_a_serializacion_en_json();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
