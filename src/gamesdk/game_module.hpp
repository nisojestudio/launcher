#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "events/host_event.hpp"
#include "gamesdk/game_config.hpp"
#include "gamesdk/game_input_event.hpp"
#include "gamesdk/game_manifest.hpp"

namespace nlp3::host {

struct HostSessionSnapshot;

} // namespace nlp3::host

namespace nlp3::gamesdk {

struct GameTelemetryItem {
    std::string key{};
    std::string label{};
    std::string value{};
    std::string tone{}; // neutral, accent, warning, danger
};

class IGameModule {
public:
    virtual ~IGameModule() = default;

    virtual std::string_view game_id() const noexcept = 0;
    virtual GameManifest manifest() const {
        return {};
    }
    virtual void apply_config(const GameConfig& config) {
        (void)config;
    }
    virtual GameConfig default_config() const {
        return {};
    }
    virtual void on_activated() = 0;
    virtual void on_host_event(
        const events::HostEvent& event,
        const host::HostSessionSnapshot& session_snapshot) = 0;
    virtual void on_game_input_event(
        const GameInputEvent& event,
        const host::HostSessionSnapshot& session_snapshot) {
        (void)event;
        (void)session_snapshot;
    }
    virtual std::vector<GameTelemetryItem> telemetry() const {
        return {};
    }
};

} // namespace nlp3::gamesdk
