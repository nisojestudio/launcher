#include "games/live_timer_game.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <iomanip>

#include "host/session_state.hpp"

namespace nlp3::games {

namespace {

constexpr std::string_view kInitialTimeS = "initial_time_s";
constexpr std::string_view kTimePerLikeS = "time_per_like_s";
constexpr std::string_view kTimePerShareS = "time_per_share_s";
constexpr std::string_view kTimePerFollowS = "time_per_follow_s";
constexpr std::string_view kTimePerGiftCoinS = "time_per_gift_coin_s";
constexpr std::string_view kTimePerChatS = "time_per_chat_s";

constexpr std::string_view kTitleText = "title_text";
constexpr std::string_view kSubtitleText = "subtitle_text";

constexpr std::string_view kTitleFontSize = "title_font_size";
constexpr std::string_view kTitleFontColor = "title_font_color";
constexpr std::string_view kTitleFontFamily = "title_font_family";
constexpr std::string_view kTitleBold = "title_bold";

constexpr std::string_view kCounterFontSize = "counter_font_size";
constexpr std::string_view kCounterFontColor = "counter_font_color";
constexpr std::string_view kCounterFontFamily = "counter_font_family";
constexpr std::string_view kCounterBold = "counter_bold";

constexpr std::string_view kSubtitleFontSize = "subtitle_font_size";
constexpr std::string_view kSubtitleFontColor = "subtitle_font_color";
constexpr std::string_view kSubtitleFontFamily = "subtitle_font_family";
constexpr std::string_view kSubtitleBold = "subtitle_bold";

constexpr std::string_view kSoundPath = "on_complete_sound_path";
constexpr std::string_view kSoundRepeat = "on_complete_repeat";
constexpr std::string_view kSoundVolume = "on_complete_volume";
constexpr std::string_view kVideoUrl = "on_complete_video_url";
constexpr std::string_view kMaxTimeS = "max_time_s";

constexpr std::string_view kPopupAddColor = "popup_add_color";
constexpr std::string_view kPopupSubtractColor = "popup_subtract_color";

constexpr std::string_view kOnCompleteText = "on_complete_text";
constexpr std::string_view kOnCompleteTextColor = "on_complete_text_color";
constexpr std::string_view kOnCompleteTextSize = "on_complete_text_size";

constexpr std::string_view kTickSoundPath = "tick_sound_path";
constexpr std::string_view kTickSoundVolume = "tick_sound_volume";
constexpr std::string_view kAddSoundPath = "add_sound_path";
constexpr std::string_view kAddSoundVolume = "add_sound_volume";

// Effect config keys
constexpr std::string_view kTitleEffect = "title_effect";
constexpr std::string_view kCounterEffect = "counter_effect";
constexpr std::string_view kSubtitleEffect = "subtitle_effect";
constexpr std::string_view kTitleGlow = "title_glow_enabled";
constexpr std::string_view kCounterGlow = "counter_glow_enabled";
constexpr std::string_view kSubtitleGlow = "subtitle_glow_enabled";
constexpr std::string_view kGlowColor = "glow_color";
constexpr std::string_view kGlowIntensity = "glow_intensity_px";
constexpr std::string_view kPulseSpeed = "pulse_speed_s";
constexpr std::string_view kDigitEffect = "digit_effect";
constexpr std::string_view kColorPreset = "color_preset";
constexpr std::string_view kCounterFont = "counter_font";

constexpr double kMaxRecentEventsAgeS = 4.0;
constexpr std::size_t kMaxRecentEvents = 6;

constexpr std::string_view kIconLike = "\xe2\x9d\xa4";     // ❤
constexpr std::string_view kIconShare = "\xf0\x9f\x94\x84"; // 🔄
constexpr std::string_view kIconFollow = "\xe2\x9c\xa8";    // ✨
constexpr std::string_view kIconGift = "\xf0\x9f\x8e\x81"; // 🎁
constexpr std::string_view kIconChat = "\xf0\x9f\x92\xac"; // 💬

std::string resolve_actor_name(const gamesdk::GameInputActor& actor) {
    if (!actor.display_name.empty()) return actor.display_name;
    if (!actor.username.empty()) return actor.username;
    return "unknown";
}

// T1.3: monotonic session id derived from the wall clock so save/restore and
// arm() flows can reset the overlay's lastShownEventId deterministically.
std::int64_t now_wall_ms_int64() noexcept {
    using namespace std::chrono;
    return static_cast<std::int64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

} // namespace

std::string substitute_timer_placeholders(
    std::string_view template_str,
    const LiveTimerGameState& state) {
    std::string result(template_str);

    auto format_compact = [](double value) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        std::string s = oss.str();
        if (s.find('.') != std::string::npos) {
            std::size_t last = s.find_last_not_of('0');
            if (last == s.find('.')) {
                s.resize(last);
            } else if (last != std::string::npos) {
                s.resize(last + 1);
            }
        }
        return s;
    };

    auto replace_num = [&](std::string_view placeholder, double value) {
        std::string val_str = format_compact(value);
        auto pos = result.find(placeholder);
        while (pos != std::string::npos) {
            result.replace(pos, placeholder.size(), val_str);
            pos = result.find(placeholder, pos + val_str.size());
        }
    };
    auto replace_str = [&](std::string_view placeholder, const std::string& value) {
        auto pos = result.find(placeholder);
        while (pos != std::string::npos) {
            result.replace(pos, placeholder.size(), value);
            pos = result.find(placeholder, pos + value.size());
        }
    };

    replace_num("{time_per_like}", state.time_per_like);
    replace_num("{time_per_share}", state.time_per_share);
    replace_num("{time_per_follow}", state.time_per_follow);
    replace_num("{time_per_gift_coin}", state.time_per_gift_coin);
    replace_num("{time_per_chat}", state.time_per_chat);
    replace_num("{initial_time}", state.initial_seconds);
    replace_str("{title}", state.title_text);

    return result;
}

namespace {

void apply_visual_style(const gamesdk::GameConfig& config, LiveTimerVisualStyle& style,
                         std::string_view size_key, std::string_view color_key,
                         std::string_view font_key, std::string_view bold_key,
                         int default_size, std::string_view default_color,
                         std::string_view default_font, bool default_bold) {
    if (const auto* v = config.find(size_key); v != nullptr) {
        if (std::holds_alternative<std::int64_t>(*v)) {
            style.font_size_px = static_cast<int>(std::get<std::int64_t>(*v));
        } else if (std::holds_alternative<double>(*v)) {
            style.font_size_px = static_cast<int>(std::get<double>(*v));
        }
    }
    if (const auto* v = config.find(color_key); v != nullptr) {
        if (const auto* s = std::get_if<std::string>(v)) {
            style.font_color = *s;
        }
    }
    if (const auto* v = config.find(font_key); v != nullptr) {
        if (const auto* s = std::get_if<std::string>(v)) {
            style.font_family = *s;
        }
    }
    if (const auto* v = config.find(bold_key); v != nullptr) {
        if (const auto* b = std::get_if<bool>(v)) {
            style.bold = *b;
        }
    }
}

} // namespace

LiveTimerGame::LiveTimerGame() {
    config_ = default_config();
}

std::string_view LiveTimerGame::game_id() const noexcept {
    return kLiveTimerGameId;
}

gamesdk::GameManifest LiveTimerGame::manifest() const {
    return gamesdk::GameManifest{
        std::string(kLiveTimerGameId),
        "Live Timer",
        "0.1.0",
        {},
        "Contador regresivo extensible para lives. Cada evento suma o resta tiempo.",
        "nlp3",
        gamesdk::GameCapabilities{true, true, true, true, false, true, false},
    };
}

gamesdk::GameConfig LiveTimerGame::default_config() const {
    gamesdk::GameConfig config;
    config.set(std::string(kInitialTimeS), 300.0);
    config.set(std::string(kTimePerLikeS), 0.0);
    config.set(std::string(kTimePerShareS), 0.0);
    config.set(std::string(kTimePerFollowS), 0.0);
    config.set(std::string(kTimePerGiftCoinS), 0.0);
    config.set(std::string(kTimePerChatS), 0.0);

    config.set(std::string(kTitleText), std::string("🎯 Extiende el Live"));
    config.set(std::string(kSubtitleText), std::string("📌 Cada coin suma {time_per_gift_coin}s"));

    config.set(std::string(kTitleFontSize), std::int64_t{48});
    config.set(std::string(kTitleFontColor), std::string("#FFFFFF"));
    config.set(std::string(kTitleFontFamily), std::string("Segoe UI, sans-serif"));
    config.set(std::string(kTitleBold), true);

    config.set(std::string(kCounterFontSize), std::int64_t{120});
    config.set(std::string(kCounterFontColor), std::string("#00FF88"));
    config.set(std::string(kCounterFontFamily), std::string("Segoe UI, monospace"));
    config.set(std::string(kCounterBold), true);

    config.set(std::string(kSubtitleFontSize), std::int64_t{32});
    config.set(std::string(kSubtitleFontColor), std::string("#AAAAAA"));
    config.set(std::string(kSubtitleFontFamily), std::string("Segoe UI, sans-serif"));
    config.set(std::string(kSubtitleBold), false);

    config.set(std::string(kSoundPath), std::string(""));
    config.set(std::string(kSoundRepeat), false);
    config.set(std::string(kSoundVolume), 1.0);
    config.set(std::string(kVideoUrl), std::string(""));
    config.set(std::string(kMaxTimeS), 0.0);

    config.set(std::string(kPopupAddColor), std::string("#00AAFF"));
    config.set(std::string(kPopupSubtractColor), std::string("#FF4444"));

    config.set(std::string(kOnCompleteText), std::string("TIEMPO CUMPLIDO"));
    config.set(std::string(kOnCompleteTextColor), std::string("#FFD700"));
    config.set(std::string(kOnCompleteTextSize), std::int64_t{48});

    config.set(std::string(kTickSoundPath), std::string(""));
    config.set(std::string(kTickSoundVolume), 1.0);
    config.set(std::string(kAddSoundPath), std::string(""));
    config.set(std::string(kAddSoundVolume), 1.0);

    // Visual effects defaults
    config.set(std::string(kTitleEffect), std::string("none"));
    config.set(std::string(kCounterEffect), std::string("none"));
    config.set(std::string(kSubtitleEffect), std::string("none"));
    config.set(std::string(kTitleGlow), false);
    config.set(std::string(kCounterGlow), false);
    config.set(std::string(kSubtitleGlow), false);
    config.set(std::string(kGlowColor), std::string("#FFD700"));
    config.set(std::string(kGlowIntensity), std::int64_t{8});
    config.set(std::string(kPulseSpeed), 1.5);
    // V3: digit transition + palette + font
    config.set(std::string(kDigitEffect), std::string("none"));
    config.set(std::string(kColorPreset), std::string("neon-green"));
    config.set(std::string(kCounterFont), std::string("Space Mono"));

    return config;
}

void LiveTimerGame::apply_config(const gamesdk::GameConfig& config) {
    auto effective = config_;

    double old_initial = effective.get_double(kInitialTimeS, 300.0);

    // A12: sanitize numeric values before storing so NaN/inf cannot poison
    // the SSOT. The server already clamps, but the gameplay layer must also
    // defend itself in case the config comes from somewhere less strict.
    auto read_finite_double = [](const gamesdk::GameConfigValue& v) -> std::optional<double> {
        if (std::holds_alternative<double>(v)) {
            const double d = std::get<double>(v);
            if (std::isfinite(d)) return d;
            return std::nullopt;
        }
        if (std::holds_alternative<std::int64_t>(v)) {
            return static_cast<double>(std::get<std::int64_t>(v));
        }
        return std::nullopt;
    };
    auto read_finite_int = [](const gamesdk::GameConfigValue& v) -> std::optional<std::int64_t> {
        if (std::holds_alternative<std::int64_t>(v)) {
            return std::get<std::int64_t>(v);
        }
        if (std::holds_alternative<double>(v)) {
            const double d = std::get<double>(v);
            if (!std::isfinite(d)) return std::nullopt;
            return static_cast<std::int64_t>(d);
        }
        return std::nullopt;
    };
    auto apply_double = [&](std::string_view key) {
        if (const auto* v = config.find(key); v != nullptr) {
            if (auto sanitized = read_finite_double(*v); sanitized.has_value()) {
                effective.set(std::string(key), *sanitized);
            }
        }
    };
    auto apply_int = [&](std::string_view key) {
        if (const auto* v = config.find(key); v != nullptr) {
            if (auto sanitized = read_finite_int(*v); sanitized.has_value()) {
                effective.set(std::string(key), *sanitized);
            }
        }
    };
    auto apply_string = [&](std::string_view key) {
        if (const auto* v = config.find(key); v != nullptr) {
            effective.set(std::string(key), *v);
        }
    };
    auto apply_bool = [&](std::string_view key) {
        if (const auto* v = config.find(key); v != nullptr) {
            effective.set(std::string(key), *v);
        }
    };

    apply_double(kInitialTimeS);
    apply_double(kTimePerLikeS);
    apply_double(kTimePerShareS);
    apply_double(kTimePerFollowS);
    apply_double(kTimePerGiftCoinS);
    apply_double(kTimePerChatS);
    apply_double(kMaxTimeS);

    apply_string(kTitleText);
    apply_string(kSubtitleText);

    apply_int(kTitleFontSize);
    apply_string(kTitleFontColor);
    apply_string(kTitleFontFamily);
    apply_bool(kTitleBold);

    apply_int(kCounterFontSize);
    apply_string(kCounterFontColor);
    apply_string(kCounterFontFamily);
    apply_bool(kCounterBold);

    apply_int(kSubtitleFontSize);
    apply_string(kSubtitleFontColor);
    apply_string(kSubtitleFontFamily);
    apply_bool(kSubtitleBold);

    apply_string(kSoundPath);
    apply_bool(kSoundRepeat);
    apply_double(kSoundVolume);
    apply_string(kVideoUrl);
    apply_string(kPopupAddColor);
    apply_string(kPopupSubtractColor);
    apply_string(kOnCompleteText);
    apply_string(kOnCompleteTextColor);
    apply_int(kOnCompleteTextSize);
    apply_string(kTickSoundPath);
    apply_double(kTickSoundVolume);
    apply_string(kAddSoundPath);
    apply_double(kAddSoundVolume);

    // Visual effects — apply BEFORE moving effective to config_
    apply_string(kTitleEffect);
    apply_string(kCounterEffect);
    apply_string(kSubtitleEffect);
    apply_bool(kTitleGlow);
    apply_bool(kCounterGlow);
    apply_bool(kSubtitleGlow);
    apply_string(kGlowColor);
    apply_int(kGlowIntensity);
    apply_double(kPulseSpeed);
    // V3: new visual fields
    apply_string(kDigitEffect);
    apply_string(kColorPreset);
    apply_string(kCounterFont);

    config_ = std::move(effective);

    state_.time_per_like = config_.get_double(kTimePerLikeS, 0.0);
    state_.time_per_share = config_.get_double(kTimePerShareS, 0.0);
    state_.time_per_follow = config_.get_double(kTimePerFollowS, 0.0);
    state_.time_per_gift_coin = config_.get_double(kTimePerGiftCoinS, 0.0);
    state_.time_per_chat = config_.get_double(kTimePerChatS, 0.0);
    state_.max_time_s = config_.get_double(kMaxTimeS, 0.0);

    double new_initial = config_.get_double(kInitialTimeS, 300.0);
    state_.initial_seconds = new_initial;
    // T2.1: only adjust remaining when the timer is actively running. When
    // completed or paused the runtime is the SSOT and apply_config must NOT
    // re-inflate remaining_seconds from the new initial value.
    if (state_.running && !state_.paused && !state_.completed) {
        double diff = new_initial - old_initial;
        state_.remaining_seconds = std::max(0.0, state_.remaining_seconds + diff);
    }
    // T2.2: clamp caliente — keep remaining within max_time_s even after live edits.
    if (state_.max_time_s > 0.0 && state_.remaining_seconds > state_.max_time_s) {
        state_.remaining_seconds = state_.max_time_s;
    }

    state_.title_text = config_.get_string(kTitleText, "\xf0\x9f\x8e\xaf Extiende el Live");
    state_.subtitle_text = config_.get_string(kSubtitleText, "\xf0\x9f\x93\x8c Cada coin suma {time_per_gift_coin}s");
    state_.on_complete_sound_path = config_.get_string(kSoundPath, "");
    state_.on_complete_repeat = config_.get_bool(kSoundRepeat, false);
    state_.on_complete_volume = config_.get_double(kSoundVolume, 1.0);
    state_.on_complete_video_url = config_.get_string(kVideoUrl, "");
    state_.popup_style.add_color = config_.get_string(kPopupAddColor, "#00AAFF");
    state_.popup_style.subtract_color = config_.get_string(kPopupSubtractColor, "#FF4444");
    state_.on_complete_text = config_.get_string(kOnCompleteText, "TIEMPO CUMPLIDO");
    state_.on_complete_text_color = config_.get_string(kOnCompleteTextColor, "#FFD700");
    state_.on_complete_text_size = static_cast<int>(config_.get_double(kOnCompleteTextSize, 48.0));
    state_.tick_sound_path = config_.get_string(kTickSoundPath, "");
    state_.tick_sound_volume = config_.get_double(kTickSoundVolume, 1.0);
    state_.add_sound_path = config_.get_string(kAddSoundPath, "");
    state_.add_sound_volume = config_.get_double(kAddSoundVolume, 1.0);

    // Validate effect names — only "none" | "glow" | "pulse"
    auto validate_effect = [&](std::string_view key, std::string fallback) {
        auto raw = config_.get_string(key, fallback);
        if (raw != "none" && raw != "glow" && raw != "pulse") {
            raw = fallback;
            config_.set(std::string(key), raw);
        }
        return raw;
    };
    state_.title_effect = validate_effect(kTitleEffect, "none");
    state_.counter_effect = validate_effect(kCounterEffect, "none");
    state_.subtitle_effect = validate_effect(kSubtitleEffect, "none");
    state_.title_glow_enabled = config_.get_bool(kTitleGlow, false);
    state_.counter_glow_enabled = config_.get_bool(kCounterGlow, false);
    state_.subtitle_glow_enabled = config_.get_bool(kSubtitleGlow, false);
    state_.glow_color = config_.get_string(kGlowColor, "#FFD700");
    state_.glow_intensity_px = static_cast<int>(config_.get_double(kGlowIntensity, 8.0));
    state_.pulse_speed_s = config_.get_double(kPulseSpeed, 1.5);
    // V3: validate digit_effect, color_preset, counter_font
    {
        auto raw = config_.get_string(kDigitEffect, "none");
        if (raw != "none" && raw != "flip" && raw != "roll" && raw != "pop" && raw != "fade") {
            raw = "none";
            config_.set(std::string(kDigitEffect), raw);
        }
        state_.digit_effect = raw;
    }
    state_.color_preset = config_.get_string(kColorPreset, "neon-green");
    state_.counter_font = config_.get_string(kCounterFont, "Space Mono");

    apply_visual_style(config_, state_.title_style,
        kTitleFontSize, kTitleFontColor, kTitleFontFamily, kTitleBold,
        48, "#FFFFFF", "Segoe UI, sans-serif", true);
    apply_visual_style(config_, state_.counter_style,
        kCounterFontSize, kCounterFontColor, kCounterFontFamily, kCounterBold,
        120, "#00FF88", "Segoe UI, monospace", true);
    apply_visual_style(config_, state_.subtitle_style,
        kSubtitleFontSize, kSubtitleFontColor, kSubtitleFontFamily, kSubtitleBold,
        32, "#AAAAAA", "Segoe UI, sans-serif", false);
}

void LiveTimerGame::on_activated() {
    completion_sound_triggered_ = false;
    state_.completed = false;
    state_.paused = false;
    state_.running = true;
    state_.remaining_seconds = state_.initial_seconds;
    state_.recent_events.clear();
    total_time_added_ = 0.0;
    // T1.3: session id is regenerated on each activation so the overlay resets
    // lastShownEventId. event_id_counter_ is intentionally NOT reset here so it
    // stays monotonic across activations, arm() and save/restore cycles.
    state_.session_id = now_wall_ms_int64();
    start_time_ = std::chrono::steady_clock::now();
    last_tick_second_ = -1;
    // N2/N9: sounds moved to overlay HTML5 audio. Backend stays silent here.
    // The overlay plays ticks/add-chime/completion via applySounds() in JS,
    // driven by JSON state fields, never via this backend stub.
}

void LiveTimerGame::arm() noexcept {
    stop_sound();
    completion_sound_triggered_ = false;
    last_tick_second_ = -1;
    state_.completed = false;
    state_.paused = false;
    state_.running = false;
    state_.remaining_seconds = state_.initial_seconds;
    state_.recent_events.clear();
    total_time_added_ = 0.0;
    // T1.3: event_id_counter_ stays monotonic across arm(); only the session id
    // is regenerated so the overlay clears its lastShownEventId cursor.
    state_.session_id = now_wall_ms_int64();
    start_time_ = std::chrono::steady_clock::now();
}

void LiveTimerGame::on_host_event(
    const events::HostEvent&,
    const host::HostSessionSnapshot&) {
}

double LiveTimerGame::remaining_seconds() const noexcept {
    // T1.1f-r2: remaining_seconds() is the single source of truth for the
    // live countdown. It computes dynamically from the SSOT pair
    // (state_.remaining_seconds, start_time_) without mutating anything.
    // tick() is only a completion-detector and does NOT update baseline values.
    if (!std::isfinite(state_.remaining_seconds)) {
        return 0.0;
    }
    if (!state_.running || state_.paused) {
        return state_.remaining_seconds;
    }
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    auto elapsed_s = std::chrono::duration<double>(elapsed).count();
    double current = state_.remaining_seconds - elapsed_s;
    if (!std::isfinite(current)) current = 0.0;
    if (current < 0.0) current = 0.0;
    return current;
}

void LiveTimerGame::tick() noexcept {
    // T1.1f-r2: tick() is a completion-detector ONLY. It does NOT mutate
    // state_.remaining_seconds or start_time_ while the timer is running.
    // The live remaining is always computed dynamically by remaining_seconds()
    // so the SSOT (state_.remaining_seconds, start_time_) stays stable and
    // is only initialized by on_activated() / resume(). This prevents the
    // micro-thrashing caused by 8+ callers competing to reset start_time_.
    if (!state_.running || state_.paused || state_.completed) return;

    double rem = remaining_seconds();  // dynamic compute, no mutation
    if (rem > 0.0) return;

    // Timer exhausted — commit completion to SSOT
    state_.remaining_seconds = 0.0;
    state_.running = false;
    state_.completed = true;
    // Re-arm the completion-sound one-shot poll so poll_completion_sound can fire.
    completion_sound_triggered_ = false;
    last_tick_second_ = -1;
}

std::string LiveTimerGame::format_time() const {
    auto rem = remaining_seconds();
    auto total_seconds = static_cast<std::int64_t>(std::floor(rem));

    if (total_seconds < 0) total_seconds = 0;

    constexpr std::int64_t kSecondsPerDay = 86400;
    constexpr std::int64_t kSecondsPerHour = 3600;
    constexpr std::int64_t kSecondsPerMinute = 60;

    auto days = total_seconds / kSecondsPerDay;
    auto remainder = total_seconds % kSecondsPerDay;
    auto hours = remainder / kSecondsPerHour;
    remainder = remainder % kSecondsPerHour;
    auto minutes = remainder / kSecondsPerMinute;
    auto seconds = remainder % kSecondsPerMinute;

    std::ostringstream oss;
    if (days > 0) {
        oss << days << (days == 1 ? " dia " : " dias ");
    }
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds;
    return oss.str();
}

void LiveTimerGame::on_game_input_event(
    const gamesdk::GameInputEvent& event,
    const host::HostSessionSnapshot& session_snapshot) {
    (void)session_snapshot;

    // T2.6: hidden_ blocks event input while preserving runtime counters.
    if (hidden_ || state_.completed || !state_.running || state_.paused) return;
    // T1.1f-r2: no tick() here — remaining_seconds() computes dynamically.
    // Events add delta directly to state_.remaining_seconds; the SSOT baseline
    // (start_time_) stays stable, preserving the countdown integrity.

    double delta = 0.0;
    std::string_view icon;
    std::string_view label;

    switch (event.kind) {
    case gamesdk::GameInputEventKind::like:
        delta = state_.time_per_like;
        icon = kIconLike;
        label = "like";
        break;
    case gamesdk::GameInputEventKind::share:
        delta = state_.time_per_share;
        icon = kIconShare;
        label = "share";
        break;
    case gamesdk::GameInputEventKind::follow:
        delta = state_.time_per_follow;
        icon = kIconFollow;
        label = "follow";
        break;
    case gamesdk::GameInputEventKind::gift: {
        double coins = 1.0;
        if (event.gift.has_value()) {
            coins = event.gift->diamond_count > 0
                ? static_cast<double>(event.gift->diamond_count)
                : static_cast<double>(event.gift->quantity);
        }
        delta = coins * state_.time_per_gift_coin;
        icon = kIconGift;
        label = "gift";
        break;
    }
    case gamesdk::GameInputEventKind::chat_message:
        delta = state_.time_per_chat;
        icon = kIconChat;
        label = "chat";
        break;
    default:
        return;
    }

    if (delta == 0.0) return;

    state_.remaining_seconds += delta;
    total_time_added_ += delta;
    // A12: defend against NaN/inf polluting the SSOT. A bad delta (e.g. NaN
    // from a corrupt upstream) or a massive time_per_* would otherwise
    // produce an inf that the JSON serializer could not emit and that the
    // overlay would render as "NaN". Clamp to a sane upper bound.
    constexpr double kMaxReasonableSeconds = 365.0 * 86400.0;  // 1 year
    if (!std::isfinite(state_.remaining_seconds)) {
        state_.remaining_seconds = 0.0;
    }
    if (state_.remaining_seconds > kMaxReasonableSeconds) {
        state_.remaining_seconds = kMaxReasonableSeconds;
    }
    const bool completed_by_event = state_.remaining_seconds < 0.0;
    if (completed_by_event) {
        state_.remaining_seconds = 0.0;
        state_.running = false;
        state_.completed = true;
    } else if (state_.max_time_s > 0.0 && state_.remaining_seconds > state_.max_time_s) {
        state_.remaining_seconds = state_.max_time_s;
    }

    // A8: skip the popup when this event is what exhausted the timer. The
    // overlay will fire confetti and the completed banner; a simultaneous
    // negative-time popup is confusing UX.
    if (completed_by_event) return;

    add_event_popup(icon, label, delta);
}

void LiveTimerGame::add_event_popup(std::string_view icon, std::string_view label, double delta) {
    prune_old_events();
    auto id = ++event_id_counter_;
    state_.recent_events.push_back({
        id,
        std::string(icon),
        std::string(label),
        delta,
        delta >= 0,
        std::chrono::steady_clock::now(),
    });
    if (state_.recent_events.size() > kMaxRecentEvents) {
        state_.recent_events.erase(state_.recent_events.begin(),
            state_.recent_events.end() - kMaxRecentEvents);
    }
}

void LiveTimerGame::prune_old_events() {
    auto now = std::chrono::steady_clock::now();
    state_.recent_events.erase(
        std::remove_if(state_.recent_events.begin(), state_.recent_events.end(),
            [&](const LiveTimerRecentEvent& e) {
                auto age = std::chrono::duration<double>(now - e.occurred_at).count();
                return age > kMaxRecentEventsAgeS;
            }),
        state_.recent_events.end());
}

bool LiveTimerGame::poll_completion_sound() noexcept {
    // T1.1: tick() commits completion to the SSOT before we observe it.
    tick();
    if (!hidden_ && state_.completed && !completion_sound_triggered_) {
        completion_sound_triggered_ = true;
        play_completion_sound();
        return true;
    }
    return false;
}

// T1.4: sound playback is intentionally delegated to the overlay HTML5 audio
// (applySounds / playAddSound in live-timer.html). The backend keeps these
// signatures as empty stubs for interface compatibility with poll loops and
// arm/stop callers. These are NOT unfinished — they are complete by design.
// The overlay owns all audio playback driven by JSON state fields
// (tick_sound_path, add_sound_path, on_complete_sound_path).
void LiveTimerGame::play_completion_sound() const {
    (void)state_;
}

void LiveTimerGame::stop_sound() const noexcept {
}

void LiveTimerGame::play_event_sound(const std::string& path, double volume) const {
    (void)path;
    (void)volume;
}

bool LiveTimerGame::poll_tick_sound() noexcept {
    // T1.1: tick() flushes elapsed time before computing tick sound cadence.
    tick();
    if (hidden_ || !state_.running || state_.paused || state_.completed) {
        last_tick_second_ = -1;
        return false;
    }
    auto rem = remaining_seconds();
    if (rem > 60.0) {
        last_tick_second_ = -1;
        return false;
    }
    int current_sec = static_cast<int>(std::floor(rem));
    if (current_sec >= 0 && current_sec != last_tick_second_) {
        last_tick_second_ = current_sec;
        play_event_sound(state_.tick_sound_path, state_.tick_sound_volume);
        return true;
    }
    return false;
}

const LiveTimerGameState& LiveTimerGame::state() const noexcept {
    return state_;
}

const gamesdk::GameConfig& LiveTimerGame::config() const noexcept {
    return config_;
}

std::vector<gamesdk::GameTelemetryItem> LiveTimerGame::telemetry() const {
    auto rem = remaining_seconds();

    return {
        {"remaining_seconds", "Tiempo restante", std::to_string(rem), "neutral"},
        {"remaining_formatted", "Formato", format_time(), "neutral"},
        {"initial_seconds", "Tiempo inicial", std::to_string(state_.initial_seconds), "neutral"},
        {"running", "Estado", state_.running ? "activo" : "detenido",
            state_.running ? "accent" : "warning"},
        {"completed", "Completado", state_.completed ? "1" : "0",
            state_.completed ? "danger" : "neutral"},
        {"total_time_added", "Tiempo agregado", std::to_string(total_time_added_), "neutral"},
        {"total_actions", "Eventos procesados", std::to_string(event_id_counter_), "neutral"},
        {"title_text", "Titulo", state_.title_text, "neutral"},
        {"subtitle_text", "Subtitulo", substitute_timer_placeholders(state_.subtitle_text, state_), "neutral"},
    };
}

void LiveTimerGame::pause() noexcept {
    if (!state_.running || state_.paused) return;
    // T1.1f-r2: capture the live remaining via the dynamic getter and write it
    // into the SSOT so reads during pause return a stable, frozen value.
    // No tick() needed — remaining_seconds() already gives the correct time.
    paused_remaining_seconds_ = remaining_seconds();
    state_.remaining_seconds = paused_remaining_seconds_;
    state_.paused = true;
    state_.running = false;
}

void LiveTimerGame::resume() noexcept {
    if (state_.running || !state_.paused) return;
    // B2: si el tiempo pausado es 0 o negativo, no tiene sentido resumir.
    if (paused_remaining_seconds_ <= 0.0) return;
    state_.paused = false;
    state_.running = true;
    state_.completed = false;
    state_.remaining_seconds = paused_remaining_seconds_;
    start_time_ = std::chrono::steady_clock::now();
    // T1.1f-r2: start_time_ is only set here and in on_activated(). It stays
    // stable across the entire run so remaining_seconds() is drift-free.
}

void LiveTimerGame::reset() noexcept {
    stop_sound();
    on_activated();
}

void LiveTimerGame::stop() noexcept {
    stop_sound();
    state_.running = false;
    state_.paused = false;
    state_.completed = true;
    state_.remaining_seconds = 0.0;
    completion_sound_triggered_ = false;
}

void LiveTimerGame::adjust_time(double delta) noexcept {
    // T2.6: blocked while hidden; runtime preserved.
    if (hidden_) return;
    if (state_.completed) return;
    if (state_.paused) return;   // B1: no ajustar mientras esta pausado
    // A12: ignore non-finite deltas so the SSOT never gets poisoned with NaN.
    if (!std::isfinite(delta)) return;
    // T1.1f-r2: no tick() here. remaining_seconds() computes dynamically from
    // the stable SSOT. We add delta directly to state_.remaining_seconds so the
    // countdown baseline stays intact.

    const double before = state_.remaining_seconds;
    state_.remaining_seconds += delta;
    total_time_added_ += delta;
    double applied_delta = state_.remaining_seconds - before;
    if (!std::isfinite(state_.remaining_seconds)) {
        state_.remaining_seconds = before;  // revert to safe value
        applied_delta = 0.0;
    } else {
        constexpr double kMaxReasonableSeconds = 365.0 * 86400.0;
        if (state_.remaining_seconds > kMaxReasonableSeconds) {
            state_.remaining_seconds = kMaxReasonableSeconds;
        }
        if (state_.remaining_seconds < 0.0) {
            state_.remaining_seconds = 0.0;
            if (state_.running) {
                state_.running = false;
                state_.completed = true;
            }
        } else if (state_.max_time_s > 0.0 && state_.remaining_seconds > state_.max_time_s) {
            // A7: surface the actually-applied delta (clamped to max_time_s) so the
            // popup and the SSOT agree. Otherwise a +100s adjust with a 10s cap
            // would display "+100s" while the timer only accepted +10s.
            applied_delta = state_.max_time_s - before;
            state_.remaining_seconds = state_.max_time_s;
        }
    }

    if (delta > 0.0) {
        play_event_sound(state_.add_sound_path, state_.add_sound_volume);
    }

    // B3: no mostrar popup si el delta efectivo fue cero (ej. NaN revertido)
    if (applied_delta != 0.0) {
        add_event_popup("\xf0\x9f\x93\x9d", "manual", applied_delta);
    }
}

void LiveTimerGame::set_enabled(bool enabled) noexcept {
    // T2.6: conservative hide semantics. Disabling pauses the timer (running
    // goes false) and stops any in-flight sound, but recent_events, total_time_
    // added_, event_id_counter_ and remaining_seconds are preserved. Restoring
    // only clears hidden_; the user starts the timer explicitly afterwards.
    if (!enabled) {
        hidden_ = true;
        stop_sound();
        // Commit the live remaining into the SSOT before stopping so the runtime
        // is preserved across hide -> restore (remaining_seconds() == R after).
        if (state_.running) {
            state_.remaining_seconds = remaining_seconds();
            state_.running = false;
        }
    } else {
        // Rehabilitar restaura runtime; el usuario arranca con start explicito.
        hidden_ = false;
    }
}

bool LiveTimerGame::is_enabled() const noexcept {
    return !hidden_;
}

bool LiveTimerGame::is_running() const noexcept {
    return state_.running;
}

// T1.3: getters used by persistence (PanelApp save/load).
std::int64_t LiveTimerGame::event_id_counter() const noexcept {
    return event_id_counter_;
}

std::int64_t LiveTimerGame::session_id() const noexcept {
    return state_.session_id;
}

double LiveTimerGame::total_time_added() const noexcept {
    return total_time_added_;
}

void LiveTimerGame::reset_config_to_defaults() noexcept {
    config_ = default_config();
    apply_config(config_);
}

// T1.3: 5-arg overload delegates to the extended one with neutral defaults
// so existing callers (tests) keep working while persistence restores extras.
void LiveTimerGame::restore_state(double remaining_seconds, bool running, bool paused,
                                    bool completed, bool enabled) noexcept {
    restore_state(remaining_seconds, running, paused, completed, enabled,
                  0, 0, 0.0);
}

void LiveTimerGame::restore_state(double remaining_seconds, bool running, bool paused,
                                    bool completed, bool enabled,
                                    std::int64_t event_id_counter,
                                    std::int64_t session_id,
                                    double total_time_added) noexcept {
    stop_sound();
    completion_sound_triggered_ = false;
    last_tick_second_ = -1;
    state_.recent_events.clear();
    // T1.3: persisted counters are restored verbatim so event ids stay monotonic
    // across save/load and the overlay's lastShownEventId stays aligned.
    event_id_counter_ = (event_id_counter > 0) ? event_id_counter : 0;
    total_time_added_ = total_time_added;
    // T2.6: enabled toggles hidden_; runtime is NOT cleared on hide and is left
    // as-is on restore so the user starts the timer explicitly.
    hidden_ = !enabled;

    state_.remaining_seconds = std::max(0.0, remaining_seconds);
    // T2.3: paused + running is interpreted as paused awaiting explicit resume
    // (state_.running is forced to false regardless of the requested running).
    if (paused) {
        running = false;
    }
    state_.running = running && !completed && remaining_seconds > 0.0;
    state_.paused = paused;
    state_.completed = completed;

    // T1.3: session id — generate fresh if none was persisted.
    state_.session_id = (session_id > 0) ? session_id : now_wall_ms_int64();

    if (state_.running) {
        start_time_ = std::chrono::steady_clock::now();
        paused_remaining_seconds_ = 0.0;
    } else {
        start_time_ = std::chrono::steady_clock::now();
        paused_remaining_seconds_ = state_.remaining_seconds;
    }

    if (state_.completed) {
        state_.remaining_seconds = 0.0;
    }
}

const gamesdk::GameManifest& LiveTimerGameFactory::manifest() const noexcept {
    return manifest_;
}

std::unique_ptr<gamesdk::IGameModule> LiveTimerGameFactory::create() const {
    return std::make_unique<LiveTimerGame>();
}

} // namespace nlp3::games
