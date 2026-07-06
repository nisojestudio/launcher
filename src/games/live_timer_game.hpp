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
    std::int64_t id = 0;
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

    std::int64_t session_id = 0;

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

    // Visual effects — per element
    // Main effect: "none" | "glow" | "pulse" | "shake" | "wave"
    std::string title_effect = "none";
    std::string counter_effect = "none";
    std::string subtitle_effect = "none";
    // Glow can stack on top of any main effect
    bool title_glow_enabled = false;
    bool counter_glow_enabled = false;
    bool subtitle_glow_enabled = false;

    // Global effect parameters
    std::string glow_color = "#FFD700";
    int glow_intensity_px = 8;
    std::string wave_colors = "#FF6B6B,#4ECDC4,#FFE66D";
    double pulse_speed_s = 1.5;
    std::string shake_intensity = "normal";   // "light" | "normal" | "heavy"
    bool particles_enabled = false;
    int particle_count = 15;
    std::string particle_color = "#FFD700";

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
    // T1.1: tick() advances SSOT state_. Drives completion without relying on
    // side-effecting const reads. Called by the polling loop before serialization.
    void tick() noexcept;
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
    // T1.3: 5-arg overload kept for backward compatibility; delegates to the
    // extended overload below with neutral defaults.
    void restore_state(double remaining_seconds, bool running, bool paused,
                       bool completed, bool enabled) noexcept;
    // T1.3: persisted runtime members are restored explicitly so event ids stay
    // monotonic across save/load and the overlay's lastShownEventId stays in sync.
    void restore_state(double remaining_seconds, bool running, bool paused,
                       bool completed, bool enabled,
                       std::int64_t event_id_counter,
                       std::int64_t session_id,
                       double total_time_added) noexcept;

    // T1.3: public getters used by persistence (PanelApp save/load).
    std::int64_t event_id_counter() const noexcept;
    std::int64_t session_id() const noexcept;
    double total_time_added() const noexcept;

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
    // T2.6: hidden_ replaces enabled_. When true, event input and adjust_time
    // are blocked and the overlay renders "--:--:--". Runtime counters and
    // recent_events are preserved; user starts the timer explicitly after restore.
    bool hidden_ = false;
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
