#pragma once

#include "platform/panel_command.hpp"

namespace nlp3 {
namespace host {
class HostRuntime;
}
namespace bridge {
class TikTokBridgeController;
}
namespace gamesdk {
class GameRuntimeController;
}
} // namespace nlp3

namespace nlp3::platform {

class PanelController {
public:
    PanelController(
        host::HostRuntime* runtime,
        bridge::TikTokBridgeController* bridge_controller,
        gamesdk::GameRuntimeController* game_runtime_controller) noexcept;

    PanelCommandResult execute(const PanelCommand& command);

private:
    host::HostRuntime* runtime_ = nullptr;
    bridge::TikTokBridgeController* bridge_controller_ = nullptr;
    gamesdk::GameRuntimeController* game_runtime_controller_ = nullptr;
};

} // namespace nlp3::platform
