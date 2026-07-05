#include <cassert>
#include <iostream>
#include "games/live_timer_game.hpp"
#include "gamesdk/game_config.hpp"

int main() {
    nlp3::games::LiveTimerGame game;
    
    // Test 1: Default font sizes
    assert(game.state().title_style.font_size_px == 48);
    assert(game.state().counter_style.font_size_px == 120);
    assert(game.state().subtitle_style.font_size_px == 32);
    std::cout << "PASS: Default font sizes\n";
    
    // Test 2: Apply new font sizes via partial config (simulating what frontend sends)
    nlp3::gamesdk::GameConfig config;
    config.set("title_font_size", std::int64_t{72});
    config.set("counter_font_size", std::int64_t{200});
    config.set("subtitle_font_size", std::int64_t{50});
    
    game.apply_config(config);
    
    // Read them back from state
    assert(game.state().title_style.font_size_px == 72);
    assert(game.state().counter_style.font_size_px == 200);
    assert(game.state().subtitle_style.font_size_px == 50);
    std::cout << "PASS: Font sizes applied to state: " 
              << game.state().title_style.font_size_px << ", "
              << game.state().counter_style.font_size_px << ", "
              << game.state().subtitle_style.font_size_px << "\n";
    
    // Test 3: Verify config_ has all values
    assert(game.config().get_int("title_font_size", 0) == 72);
    assert(game.config().get_int("counter_font_size", 0) == 200);
    assert(game.config().get_int("subtitle_font_size", 0) == 50);
    std::cout << "PASS: Config has correct font sizes\n";
    
    // Test 4: Apply second config change (overwrite)
    nlp3::gamesdk::GameConfig config2;
    config2.set("title_font_size", std::int64_t{36});
    game.apply_config(config2);
    assert(game.state().title_style.font_size_px == 36);
    // Counter should remain at previous value since not in config2
    assert(game.state().counter_style.font_size_px == 200);
    std::cout << "PASS: Partial update preserves unchanged values\n";
    
    // Test 5: Verify along with visual effects (both work together)
    nlp3::gamesdk::GameConfig config3;
    config3.set("title_font_size", std::int64_t{60});
    config3.set("title_effect", std::string("pulse"));
    config3.set("title_glow_enabled", true);
    game.apply_config(config3);
    assert(game.state().title_style.font_size_px == 60);
    assert(game.state().title_effect == "pulse");
    assert(game.state().title_glow_enabled == true);
    std::cout << "PASS: Font size + effects work together\n";
    
    // Test 6: Reset to defaults restores font sizes
    game.reset_config_to_defaults();
    assert(game.state().title_style.font_size_px == 48);
    assert(game.state().counter_style.font_size_px == 120);
    assert(game.state().subtitle_style.font_size_px == 32);
    std::cout << "PASS: Reset restores default font sizes\n";
    
    std::cout << "\nAll font size tests PASSED!\n";
    return 0;
}
