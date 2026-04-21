#pragma once

#include <string>

namespace nlp3::platform {

enum class PanelCommandKind {
    unknown,
    bridge_start,
    bridge_stop,
    bridge_reset,
    game_activate,
    game_deactivate,
    game_restart,
    tts_enqueue_announcement,
};

struct PanelCommand {
    PanelCommandKind kind = PanelCommandKind::unknown;
    std::string argument{};
};

struct PanelCommandResult {
    bool ok = false;
    std::string message{};
};

} // namespace nlp3::platform
