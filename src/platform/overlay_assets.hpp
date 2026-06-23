#pragma once

#include <string>
#include <string_view>

#include "games/live_timer_game.hpp"

namespace nlp3::platform {

std::string_view panel_overlay_live_timer_html() noexcept;

std::string build_live_timer_state_json(const games::LiveTimerGame* game);

} // namespace nlp3::platform
