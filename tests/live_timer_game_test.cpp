#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
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

    assert(config.get_double("initial_time_s", 0) == 300.0);
    assert(config.get_double("time_per_like_s", 0) == 2.0);
    assert(config.get_double("time_per_share_s", 0) == 5.0);
    assert(config.get_double("time_per_follow_s", 0) == 10.0);
    assert(config.get_double("time_per_gift_coin_s", 0) == 0.5);
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
    game.on_activated();

    auto rem = game.remaining_seconds();
    assert(std::abs(rem - 300.0) < 1.0);
    assert(game.state().running == true);
    assert(game.state().completed == false);

    std::cout << "PASS: on_activated starts timer\n";
}

void test_like_adds_time() {
    LiveTimerGame game;
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
    game.on_activated();
    auto before = game.remaining_seconds();

    game.on_game_input_event(make_test_event(GameInputEventKind::share), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(after >= before + 4.9);

    std::cout << "PASS: share adds time\n";
}

void test_follow_adds_time() {
    LiveTimerGame game;
    game.on_activated();
    auto before = game.remaining_seconds();

    game.on_game_input_event(make_test_event(GameInputEventKind::follow), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(after >= before + 9.9);

    std::cout << "PASS: follow adds time\n";
}

void test_gift_adds_time_based_on_diamonds() {
    LiveTimerGame game;
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
    config.set("time_per_like_s", -1.5);
    game.apply_config(config);
    game.on_activated();

    auto before = game.remaining_seconds();

    game.on_game_input_event(make_test_event(GameInputEventKind::like), kEmptySnapshot);

    auto after = game.remaining_seconds();
    assert(after <= before - 1.0);

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
    game.apply_config(config);
    game.on_activated();
    fmt = game.format_time();
    assert(fmt.find("dia") != std::string::npos);

    // Test zero
    config.set("initial_time_s", 0.0);
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
    assert(game.config().get_double("initial_time_s", 0) == 300.0);

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
    test_restore_state_running_paused_keeps_paused();
    test_event_id_monotonic_across_arm();
    test_poll_tick_sound_below_60s();
    test_build_live_timer_state_json_contract();
    test_set_enabled_preserves_runtime();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
