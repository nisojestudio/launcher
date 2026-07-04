#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gamesdk/game_factory.hpp"

namespace nlp3::games {

constexpr std::string_view kLiveTimerGameId = "live-timer";

std::string substitute_timer_placeholders(
    std::string_view template_str,
    const struct LiveTimerGameState& state);

struct LiveTimerVisualStyle {
    int font_size_px = 120;
    std::string font_color = "#00FF88";
    std::string font_family = "Segoe UI, monospace";
    bool bold = true;
};

struct LiveTimerPopupStyle {
    std::string add_color = "#00AAFF";
    std::string subtract_color = "#FF4444";
};

struct LiveTimerRecentEvent {
    int64_t id = 0;
    std::string icon;
    std::string label;
    double delta_seconds = 0.0;
    bool is_addition = true;
    std::chrono::steady_clock::time_point occurred_at;
};

struct LiveTimerGameState {
    double remaining_seconds = 0.0;
    double initial_seconds = 300.0;
    bool running = false;
    bool completed = false;
    bool paused = false;

    double max_time_s = 0.0;

    double time_per_like = 2.0;
    double time_per_share = 5.0;
    double time_per_follow = 10.0;
    double time_per_gift_coin = 0.5;
    double time_per_chat = 0.0;

    std::string title_text = "🎯 Extiende el Live";
    std::string subtitle_text = "📌 Cada coin suma {time_per_gift_coin}s";

    LiveTimerVisualStyle title_style{48, "#FFFFFF", "Segoe UI, sans-serif", true};
    LiveTimerVisualStyle counter_style{120, "#00FF88", "Segoe UI, monospace", true};
    LiveTimerVisualStyle subtitle_style{32, "#AAAAAA", "Segoe UI, sans-serif", false};

    LiveTimerPopupStyle popup_style{};

    std::string on_complete_sound_path;
    bool on_complete_repeat = false;
    double on_complete_volume = 1.0;
    std::string on_complete_video_url;
    std::string on_complete_text = "TIEMPO CUMPLIDO";
    std::string on_complete_text_color = "#FFD700";
    int on_complete_text_size = 48;

    std::string tick_sound_path;
    double tick_sound_volume = 1.0;
    std::string add_sound_path;
    double add_sound_volume = 1.0;

    std::vector<LiveTimerRecentEvent> recent_events;
};

class LiveTimerGame final : public gamesdk::IGameModule {
public:
    LiveTimerGame();

    std::string_view game_id() const noexcept override;
    gamesdk::GameManifest manifest() const override;
    void apply_config(const gamesdk::GameConfig& config) override;
    gamesdk::GameConfig default_config() const override;
    void on_activated() override;
    void arm() noexcept;
    void on_host_event(
        const events::HostEvent& event,
        const host::HostSessionSnapshot& session_snapshot) override;
    void on_game_input_event(
        const gamesdk::GameInputEvent& event,
        const host::HostSessionSnapshot& session_snapshot) override;
    std::vector<gamesdk::GameTelemetryItem> telemetry() const override;

    const LiveTimerGameState& state() const noexcept;
    const gamesdk::GameConfig& config() const noexcept;

    double remaining_seconds() const noexcept;
    std::string format_time() const;
    bool poll_completion_sound() noexcept;
    bool poll_tick_sound() noexcept;

    void pause() noexcept;
    void resume() noexcept;
    void reset() noexcept;
    void stop() noexcept;

    void adjust_time(double delta) noexcept;
    void set_enabled(bool enabled) noexcept;
    bool is_enabled() const noexcept;
    bool is_running() const noexcept;
    void reset_config_to_defaults() noexcept;

private:
    void add_event_popup(std::string_view icon, std::string_view label, double delta);
    void prune_old_events();
    void play_completion_sound() const;
    void play_event_sound(const std::string& path, double volume) const;
    void stop_sound() const noexcept;

    LiveTimerGameState state_;
    gamesdk::GameConfig config_;
    std::chrono::steady_clock::time_point start_time_;
    double paused_remaining_seconds_ = 0.0;
    double total_time_added_ = 0.0;
    int64_t event_id_counter_ = 0;
    bool completion_sound_triggered_ = false;
    int last_tick_second_ = -1;
    bool enabled_ = true;
};

class LiveTimerGameFactory final : public gamesdk::IGameFactory {
public:
    const gamesdk::GameManifest& manifest() const noexcept override;
    std::unique_ptr<gamesdk::IGameModule> create() const override;

private:
    gamesdk::GameManifest manifest_{
        std::string(nlp3::games::kLiveTimerGameId),
        "Live Timer",
        "0.1.0",
        {},
        "Contador regresivo extensible para lives. Cada evento suma o resta tiempo.",
        "nlp3",
        gamesdk::GameCapabilities{
            true,
            true,
            true,
            true,
            false,
            true,
            false,
        },
    };
};

} // namespace nlp3::games
