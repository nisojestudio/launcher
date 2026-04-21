#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "gamesdk/game_factory.hpp"

namespace nlp3::games {

struct EventCounterGameState {
    std::uint64_t chat_count = 0;
    std::uint64_t follow_count = 0;
    std::uint64_t follow_points_total = 0;
    std::uint64_t gift_count = 0;
    std::uint64_t gift_points = 0;
    std::string last_actor_name{};
    std::string last_avatar_url{};
    std::string last_event_label{};
};

class EventCounterGame final : public gamesdk::IGameModule {
public:
    EventCounterGame();

    std::string_view game_id() const noexcept override;
    gamesdk::GameManifest manifest() const override;
    void apply_config(const gamesdk::GameConfig& config) override;
    gamesdk::GameConfig default_config() const override;
    void on_activated() override;
    void on_host_event(
        const events::HostEvent& event,
        const host::HostSessionSnapshot& session_snapshot) override;
    void on_game_input_event(
        const gamesdk::GameInputEvent& event,
        const host::HostSessionSnapshot& session_snapshot) override;
    std::vector<gamesdk::GameTelemetryItem> telemetry() const override;

    const EventCounterGameState& state() const noexcept;

private:
    EventCounterGameState state_{};
    gamesdk::GameConfig config_{};
};

class EventCounterGameFactory final : public gamesdk::IGameFactory {
public:
    const gamesdk::GameManifest& manifest() const noexcept override;
    std::unique_ptr<gamesdk::IGameModule> create() const override;

private:
    gamesdk::GameManifest manifest_{
        "event-counter",
        "Event Counter",
        "0.1.0",
        {},
        "Juego de prueba que cuenta eventos del panel",
        "nlp3",
        gamesdk::GameCapabilities{
            true,
            true,
            true,
            false,
            false,
            true,
            false,
        },
    };
};

} // namespace nlp3::games
