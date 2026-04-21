#pragma once

#include "events/host_event.hpp"
#include "gamesdk/game_input_event.hpp"

namespace nlp3::gamesdk {

class GameInputEventMapper {
public:
    GameInputEvent map(const events::HostEvent& event) const;
};

} // namespace nlp3::gamesdk
