#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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
    // Bloque A / M1: nombre del actor (vacío = anónimo / contador manual).
    std::string actor_name;
    // Bloque A / M5: etiqueta de motín cuando se recorta por tope.
    bool capped = false;
};

struct LiveTimerGameState {
    double remaining_seconds = 0.0;
    // V2: sin tiempo por defecto. El timer arranca en cero y no cuenta hasta
    // que el usuario configure su tiempo inicial y pulse Iniciar.
    double initial_seconds = 0.0;
    bool running = false;
    bool completed = false;
    bool paused = false;

    std::int64_t session_id = 0;

    double max_time_s = 0.0;

    double time_per_like = 0.0;
    double time_per_share = 0.0;
    double time_per_follow = 0.0;
    double time_per_gift_coin = 0.0;
    double time_per_chat = 0.0;

    // Bloque A / M2 — likes por magnitud. Cuando es true, un lote de N likes
    // suma N * time_per_like (antes solo sumaba time_per_like una vez).
    bool like_use_magnitude = true;
    // Bloque A / M3 — multiplicadores por tipo de espectador aplicados al delta
    // calculado por el evento (value = 0 anula el efecto para ese tipo). Cuando
    // un actor es varios tipos a la vez (p.ej. moderador y suscriptor), se usa
    // el multiplicador mas alto.
    double mult_subscriber = 1.0;
    double mult_follower = 1.0;
    double mult_moderator = 1.0;
    // Bloque A / M5 — topes. 0 = sin tope. Suelo del reloj: el contador nunca
    // baja por debajo de floor_time_seconds (tampoco completa mientras el suelo
    // no sea 0; eso convierte el timer en "no completable" si el operador lo
    // elige asi). cap_*_per_minute_s miden la suma de DELTAS POSITIVAS.
    double cap_per_event_s = 0.0;
    double cap_per_user_per_minute_s = 0.0;
    double cap_total_per_minute_s = 0.0;
    double floor_time_s = 0.0;
    // Bloque A / M4 — tramos de regalo por valor. Texto (una regla por linea):
    //   "10-99: 15"   -> regalos de 10 a 99 coins suman 15 s
    //   "100+: 300"   -> regalos de 100 coins o mas suman 300 s
    //   "Rosa: 3"     -> excepcion por nombre de regalo (contains, case-ins)
    // Si la linea no corresponde a ninguno, se ignora. Si una regla aplica,
    // sustituye por completo a time_per_gift_coin (no se suman).
    std::string gift_tiers;

    std::string title_text = "🎯 Extiende el Live";
    std::string subtitle_text = "📌 Cada coin suma {time_per_gift_coin}s";

    LiveTimerVisualStyle title_style{48, "#FFFFFF", "Segoe UI, sans-serif", true};
    LiveTimerVisualStyle counter_style{120, "#00FF88", "Segoe UI, monospace", true};
    LiveTimerVisualStyle subtitle_style{32, "#AAAAAA", "Segoe UI, sans-serif", false};

    LiveTimerPopupStyle popup_style{};

    std::string on_complete_sound_path;
    bool on_complete_repeat = false;
    double on_complete_volume = 1.0;
    std::string on_complete_text = "TIEMPO CUMPLIDO";
    std::string on_complete_text_color = "#FFD700";
    int on_complete_text_size = 48;

    std::string tick_sound_path;
    double tick_sound_volume = 1.0;
    std::string add_sound_path;
    double add_sound_volume = 1.0;

    // Visual effects — per element ("none" | "glow" | "pulse")
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
    double pulse_speed_s = 1.5;

    // V3: digit transition effect ("none" | "flip" | "roll" | "pop" | "fade")
    std::string digit_effect = "none";
    // V3: color preset ("neon-green" | "cyber-blue" | "clean-white" | "rose-gold")
    std::string color_preset = "neon-green";

    // === Fase 5: motor visual (specs/live-timer-mejoras/visual.md) ============
    // Todo lo de aqui tiene default neutro: con estos valores el overlay se ve
    // exactamente como antes de la fase. El diseño se enciende configurandolo.
    //
    // M16 — escala relativa. Los tamaños en px del diseño se interpretan DENTRO de
    // un lienzo de referencia, y el overlay escala el bloque entero al tamaño real
    // del browser source. Sin esto, un overlay pensado a 1920x1080 se desarma a
    // cualquier otra resolucion. "auto" = escalar; "off" = px literal (lo de antes).
    std::string scale_mode = "auto";
    int canvas_width = 1920;
    int canvas_height = 1080;
    // V1 — marco. "none" | "card" | "glass" | "neon" | "ribbon" | "badge"
    std::string frame_style = "none";
    std::string frame_color = "#00FFFF";
    int frame_opacity = 55;      // % de opacidad del fondo del marco
    int frame_border_px = 1;
    int frame_radius_px = 4;
    int frame_padding_px = 28;
    // V2/V3 — adornos del marco
    bool frame_brackets = false;
    bool frame_grid = false;
    bool frame_scanlines = false;
    // V8 — contorno del texto (0 = sin contorno)
    int text_outline_px = 0;
    std::string text_outline_color = "#000000";
    // V6 — formato del tiempo. El contador se compone de bloques HH:MM:SS; con
    // show_hours=false se oculta el bloque de horas (12:34 en vez de 00:12:34).
    std::string time_separator = ":";
    bool show_hours = true;
    // V13 — estados con umbrales configurables (antes 60 y 10 estaban en el codigo
    // del overlay) y efecto propio del estado de peligro.
    int warn_seconds = 60;
    int danger_seconds = 10;
    std::string danger_effect = "pulse";   // "none" | "pulse" | "glitch" | "flash"
    // V5 — medidor de progreso respecto al maximo (o al tiempo inicial si no hay tope).
    std::string progress_style = "none";   // "none" | "bar" | "ring"
    int progress_thickness_px = 6;
    std::string progress_color = "";       // vacio = seguir el color del contador
    // V11 — particulas. Apagadas por defecto: son decoracion, y en un browser
    // source estrangulado (OBS pone sus procesos en «Efficiency Mode» y Windows
    // los frena cuando OBS pierde el foco) es lo primero que hay que poder apagar.
    // Ademas el overlay se auto-apaga si detecta que le cuestan FPS.
    bool particles_enabled = false;
    std::string particles_style = "none";  // "none" | "confetti" | "sparks" | "stars"
    int particles_budget = 120;            // tope de particulas simultaneas
    double particles_density = 1.0;        // multiplicador dentro del tope (0.25..2)
    bool particles_force = false;          // true = no auto-apagar por FPS
    // V3: counter font family — merged into counter_style.font_family. The panel's
    // "Fuente" dropdown now includes mono fonts (Space Mono, JetBrains Mono, Share Tech Mono)
    // alongside standard fonts. No separate counter_font field exists.

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
    // T1.1f-r2: tick() is a completion-detector. It commits completed=true
    // when remaining_seconds() reaches 0, but does NOT mutate the SSOT baseline
    // (state_.remaining_seconds / start_time_) while the timer is running.
    // Called by the polling loop and PanelApp::tick() before serialization.
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

    // Bloque A / M4 — tramo parseado de la regla de regalos. Una excepcion por
    // nombre tiene has_name=true y aplica si el nombre del regalo la contiene.
    // Publico porque el parseador (namespace anonimo del .cpp) devuelve este tipo.
    struct GiftTier {
        int min_coins = 0;
        int max_coins = -1;  // -1 = sin techo
        double seconds = 0.0;
        bool has_name = false;
        std::string name;    // lowercase
    };

    // Bloque A / M5 — ventana deslizante de aportes (solo positivos) por actor
    // y global. Se limpia en on_activated/stop (eventos = ventanas de la sesion
    // en curso; topes entre directos no tienen sentido).
    struct ContributionWindow {
        struct Entry {
            std::chrono::steady_clock::time_point at;
            double seconds = 0.0;
        };
        std::deque<Entry> entries;

        // window_s = longitud de la ventana deslizante (la purga la necesita
        // para no borrar los aportes aun vigentes — bug: purgar con 0 vacia
        // la ventana porque cada evento vive en su propio instante).
        void push(double seconds, double window_s, const std::chrono::steady_clock::time_point& now);
        double sum_recent(double window_s, const std::chrono::steady_clock::time_point& now);
        void clear() noexcept;
    };

private:
    void add_event_popup(std::string_view icon, std::string_view label, double delta,
                         std::string_view actor_name = {}, bool capped = false);
    void prune_old_events();
    void play_completion_sound() const;
    void play_event_sound(const std::string& path, double volume) const;
    void stop_sound() const noexcept;
    // Bloque A: calculo de deltas por tipo/tamaño.
    double actor_multiplier(const gamesdk::GameInputActor& actor) const noexcept;
    double resolve_gift_seconds(std::string_view gift_name, double coins) const noexcept;
    double apply_caps(double delta, const std::string& actor_key, bool* capped);

    LiveTimerGameState state_;
    gamesdk::GameConfig config_;
    std::chrono::steady_clock::time_point start_time_;
    double paused_remaining_seconds_ = 0.0;
    double total_time_added_ = 0.0;
    int64_t event_id_counter_ = 0;
    bool completion_sound_triggered_ = false;
    int last_tick_second_ = -1;
    // Bloque A / M4: tramos parseados (cacheados tras apply_config).
    std::vector<GiftTier> gift_tiers_;
    // Bloque A / M5: topes por actor y global.
    std::unordered_map<std::string, ContributionWindow> user_contributions_;
    ContributionWindow total_contributions_;
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
