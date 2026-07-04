#include "games/live_timer_game.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

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
constexpr std::string_view kWaveColors = "wave_colors";
constexpr std::string_view kPulseSpeed = "pulse_speed_s";
constexpr std::string_view kShakeIntensity = "shake_intensity";
constexpr std::string_view kParticlesEnabled = "particles_enabled";
constexpr std::string_view kParticleCount = "particle_count";
constexpr std::string_view kParticleColor = "particle_color";

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
        style.font_color = config.get_string(color_key, std::string(default_color));
    }
    if (const auto* v = config.find(font_key); v != nullptr) {
        style.font_family = config.get_string(font_key, std::string(default_font));
    }
    if (const auto* v = config.find(bold_key); v != nullptr) {
        style.bold = config.get_bool(bold_key, default_bold);
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
    config.set(std::string(kTimePerLikeS), 2.0);
    config.set(std::string(kTimePerShareS), 5.0);
    config.set(std::string(kTimePerFollowS), 10.0);
    config.set(std::string(kTimePerGiftCoinS), 0.5);
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
    config.set(std::string(kWaveColors), std::string("#FF6B6B,#4ECDC4,#FFE66D"));
    config.set(std::string(kPulseSpeed), 1.5);
    config.set(std::string(kShakeIntensity), std::string("normal"));
    config.set(std::string(kParticlesEnabled), false);
    config.set(std::string(kParticleCount), std::int64_t{15});
    config.set(std::string(kParticleColor), std::string("#FFD700"));

    return config;
}

void LiveTimerGame::apply_config(const gamesdk::GameConfig& config) {
    auto effective = config_;

    double old_initial = effective.get_double(kInitialTimeS, 300.0);

    auto apply_double = [&](std::string_view key) {
        if (const auto* v = config.find(key); v != nullptr) {
            effective.set(std::string(key), *v);
        }
    };
    auto apply_int = [&](std::string_view key) {
        if (const auto* v = config.find(key); v != nullptr) {
            effective.set(std::string(key), *v);
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
    apply_string(kWaveColors);
    apply_double(kPulseSpeed);
    apply_string(kShakeIntensity);
    apply_bool(kParticlesEnabled);
    apply_int(kParticleCount);
    apply_string(kParticleColor);

    config_ = std::move(effective);

    state_.time_per_like = config_.get_double(kTimePerLikeS, 2.0);
    state_.time_per_share = config_.get_double(kTimePerShareS, 5.0);
    state_.time_per_follow = config_.get_double(kTimePerFollowS, 10.0);
    state_.time_per_gift_coin = config_.get_double(kTimePerGiftCoinS, 0.5);
    state_.time_per_chat = config_.get_double(kTimePerChatS, 0.0);
    state_.max_time_s = config_.get_double(kMaxTimeS, 0.0);

    double new_initial = config_.get_double(kInitialTimeS, 300.0);
    double diff = new_initial - old_initial;
    state_.initial_seconds = new_initial;
    state_.remaining_seconds = std::max(0.0, state_.remaining_seconds + diff);

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

    // Validate effect names — fall back to "none" if invalid
    auto validate_effect = [&](std::string_view key, std::string fallback) {
        auto raw = config_.get_string(key, fallback);
        if (raw != "none" && raw != "glow" && raw != "pulse" && raw != "shake" && raw != "wave") {
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
    state_.wave_colors = config_.get_string(kWaveColors, "#FF6B6B,#4ECDC4,#FFE66D");
    state_.pulse_speed_s = config_.get_double(kPulseSpeed, 1.5);
    // Validate shake_intensity
    {
        auto raw = config_.get_string(kShakeIntensity, "normal");
        if (raw != "light" && raw != "normal" && raw != "heavy") {
            raw = "normal";
            config_.set(std::string(kShakeIntensity), std::string(raw));
        }
        state_.shake_intensity = raw;
    }
    state_.particles_enabled = config_.get_bool(kParticlesEnabled, false);
    state_.particle_count = static_cast<int>(config_.get_double(kParticleCount, 15.0));
    state_.particle_color = config_.get_string(kParticleColor, "#FFD700");

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
    start_time_ = std::chrono::steady_clock::now();
    last_tick_second_ = -1;
    play_event_sound(state_.tick_sound_path, state_.tick_sound_volume);
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
    event_id_counter_ = 0;
    start_time_ = std::chrono::steady_clock::now();
}

void LiveTimerGame::on_host_event(
    const events::HostEvent&,
    const host::HostSessionSnapshot&) {
}

double LiveTimerGame::remaining_seconds() const noexcept {
    if (!state_.running || state_.paused) {
        return state_.remaining_seconds;
    }

    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    auto elapsed_s = std::chrono::duration<double>(elapsed).count();
    double current = state_.remaining_seconds - elapsed_s;
    if (current <= 0.0) {
        current = 0.0;
        const_cast<LiveTimerGame*>(this)->state_.running = false;
        const_cast<LiveTimerGame*>(this)->state_.completed = true;
        const_cast<LiveTimerGame*>(this)->state_.remaining_seconds = 0.0;
    }
    return current;
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

    if (!enabled_ || state_.completed || !state_.running || state_.paused) return;

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
    if (state_.remaining_seconds < 0.0) {
        state_.remaining_seconds = 0.0;
        state_.running = false;
        state_.completed = true;
    } else if (state_.max_time_s > 0.0 && state_.remaining_seconds > state_.max_time_s) {
        state_.remaining_seconds = state_.max_time_s;
    }

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
    if (!enabled_ || completion_sound_triggered_) return false;
    if (!state_.paused) {
        remaining_seconds();
    }
    if (state_.completed && !completion_sound_triggered_) {
        completion_sound_triggered_ = true;
        play_completion_sound();
        return true;
    }
    return false;
}

void LiveTimerGame::play_completion_sound() const {
#ifdef _WIN32
    if (state_.on_complete_sound_path.empty()) {
        Beep(880, 500);
        Beep(660, 300);
        Beep(880, 800);
        return;
    }

    auto flags = SND_FILENAME | SND_ASYNC;
    if (state_.on_complete_repeat) {
        flags |= SND_LOOP;
    }
    PlaySoundA(state_.on_complete_sound_path.c_str(), nullptr, flags);
#else
    (void)state_;
#endif
}

void LiveTimerGame::stop_sound() const noexcept {
#ifdef _WIN32
    PlaySoundA(nullptr, nullptr, 0);
#endif
}

void LiveTimerGame::play_event_sound(const std::string& path, double volume) const {
#ifdef _WIN32
    if (path.empty()) {
        // No fallback beep (would be synchronous). User should configure a .wav.
        return;
    }
    auto flags = SND_FILENAME | SND_ASYNC | SND_NOSTOP;
    PlaySoundA(path.c_str(), nullptr, flags);
#else
    (void)path;
    (void)volume;
#endif
}

bool LiveTimerGame::poll_tick_sound() noexcept {
    if (!enabled_ || !state_.running || state_.paused || state_.completed) {
        last_tick_second_ = -1;
        return false;
    }
    auto rem = remaining_seconds();
    if (rem > 60.0) {
        last_tick_second_ = -1;
        return false;
    }
    int current_sec = static_cast<int>(std::floor(rem));
    if (current_sec != last_tick_second_) {
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
        {"total_actions", "Eventos procesados", std::to_string(state_.recent_events.size()), "neutral"},
        {"title_text", "Titulo", state_.title_text, "neutral"},
        {"subtitle_text", "Subtitulo", substitute_timer_placeholders(state_.subtitle_text, state_), "neutral"},
    };
}

void LiveTimerGame::pause() noexcept {
    if (!state_.running || state_.paused) return;
    paused_remaining_seconds_ = remaining_seconds();
    state_.paused = true;
    state_.running = false;
}

void LiveTimerGame::resume() noexcept {
    if (state_.running || !state_.paused) return;
    state_.paused = false;
    state_.running = true;
    state_.completed = false;
    state_.remaining_seconds = paused_remaining_seconds_;
    start_time_ = std::chrono::steady_clock::now();
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
    if (!enabled_) return;
    if (state_.completed) return;

    state_.remaining_seconds += delta;
    total_time_added_ += delta;
    if (state_.remaining_seconds < 0.0) {
        state_.remaining_seconds = 0.0;
        if (state_.running) {
            state_.running = false;
            state_.completed = true;
        }
    } else if (state_.max_time_s > 0.0 && state_.remaining_seconds > state_.max_time_s) {
        state_.remaining_seconds = state_.max_time_s;
    }

    if (delta > 0.0) {
        play_event_sound(state_.add_sound_path, state_.add_sound_volume);
    }

    add_event_popup("\xf0\x9f\x93\x9d", "manual", delta);
}

void LiveTimerGame::set_enabled(bool enabled) noexcept {
    enabled_ = enabled;
    if (!enabled) {
        stop_sound();
        state_.running = false;
        state_.completed = false;
        state_.paused = false;
        state_.remaining_seconds = 0.0;
        state_.recent_events.clear();
        total_time_added_ = 0.0;
        event_id_counter_ = 0;
    }
}

bool LiveTimerGame::is_enabled() const noexcept {
    return enabled_;
}

bool LiveTimerGame::is_running() const noexcept {
    return state_.running;
}

void LiveTimerGame::reset_config_to_defaults() noexcept {
    config_ = default_config();
    apply_config(config_);
}

void LiveTimerGame::restore_state(double remaining_seconds, bool running, bool paused,
                                   bool completed, bool enabled) noexcept {
    stop_sound();
    completion_sound_triggered_ = false;
    last_tick_second_ = -1;
    state_.recent_events.clear();
    event_id_counter_ = 0;
    total_time_added_ = 0.0;
    enabled_ = enabled;

    state_.remaining_seconds = std::max(0.0, remaining_seconds);
    state_.running = running && !completed && remaining_seconds > 0.0;
    state_.paused = paused;
    state_.completed = completed;

    if (state_.running && !state_.paused) {
        start_time_ = std::chrono::steady_clock::now();
        paused_remaining_seconds_ = 0.0;
    } else {
        start_time_ = std::chrono::steady_clock::now();
        paused_remaining_seconds_ = state_.remaining_seconds;
        state_.running = false;
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
