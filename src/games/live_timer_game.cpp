#include "games/live_timer_game.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
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

// Bloque A — reglas de tiempo
constexpr std::string_view kLikeUseMagnitude = "like_use_magnitude";
constexpr std::string_view kMultSubcriber = "mult_subscriber";
constexpr std::string_view kMultFollower = "mult_follower";
constexpr std::string_view kMultModerator = "mult_moderator";
constexpr std::string_view kCapPerEventS = "cap_per_event_s";
constexpr std::string_view kCapPerUserPerMinS = "cap_per_user_per_minute_s";
constexpr std::string_view kCapTotalPerMinS = "cap_total_per_minute_s";
constexpr std::string_view kFloorTimeS = "floor_time_s";
constexpr std::string_view kGiftTiers = "gift_tiers";
// R2 — control de popups
constexpr std::string_view kPopupsEnabled = "popups_enabled";
constexpr std::string_view kPopupShowActor = "popup_show_actor";
// R5 — posicion del bloque en el overlay
constexpr std::string_view kAnchorPosition = "anchor_position";
constexpr std::string_view kAnchorMarginPct = "anchor_margin_pct";

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

// Fase 5 — motor visual: escala (M16), marco (V1/V2/V3) y contorno (V8).
constexpr std::string_view kScaleMode = "scale_mode";
constexpr std::string_view kCanvasWidth = "canvas_width";
constexpr std::string_view kCanvasHeight = "canvas_height";
constexpr std::string_view kFrameStyle = "frame_style";
constexpr std::string_view kFrameColor = "frame_color";
constexpr std::string_view kFrameOpacity = "frame_opacity";
constexpr std::string_view kFrameBorderPx = "frame_border_px";
constexpr std::string_view kFrameRadiusPx = "frame_radius_px";
constexpr std::string_view kFramePaddingPx = "frame_padding_px";
constexpr std::string_view kFrameBrackets = "frame_brackets";
constexpr std::string_view kFrameGrid = "frame_grid";
constexpr std::string_view kFrameScanlines = "frame_scanlines";
constexpr std::string_view kTextOutlinePx = "text_outline_px";
constexpr std::string_view kTextOutlineColor = "text_outline_color";

// Fase 5 (incremento 2) — formato del tiempo (V6), estados (V13) y medidor (V5).
constexpr std::string_view kTimeSeparator = "time_separator";
constexpr std::string_view kShowHours = "show_hours";
constexpr std::string_view kWarnSeconds = "warn_seconds";
constexpr std::string_view kDangerSeconds = "danger_seconds";
constexpr std::string_view kDangerEffect = "danger_effect";
constexpr std::string_view kProgressStyle = "progress_style";
constexpr std::string_view kProgressThicknessPx = "progress_thickness_px";
constexpr std::string_view kProgressColor = "progress_color";

// Fase 5 (incremento 3) — particulas (V11).
constexpr std::string_view kParticlesEnabled = "particles_enabled";
constexpr std::string_view kParticlesStyle = "particles_style";
constexpr std::string_view kParticlesBudget = "particles_budget";
constexpr std::string_view kParticlesDensity = "particles_density";
constexpr std::string_view kParticlesForce = "particles_force";

constexpr int kMinParticlesBudget = 10;
constexpr int kMaxParticlesBudget = 300;
constexpr double kMinParticlesDensity = 0.25;
constexpr double kMaxParticlesDensity = 2.0;

constexpr int kMaxThresholdSeconds = 3600;
constexpr int kMinProgressThicknessPx = 1;
constexpr int kMaxProgressThicknessPx = 40;

// Bloque A — limites de las caps. 1 anno es el tope absoluto del reloj; aqui
// se acotan los minutos de ventana para que no desborden en configuraciones
// locas.
constexpr double kMaxCapValueS = 86400.0;
// Tokens "1500+" / "1500-" de los tramos de regalo. El "-" en un token numerico
// al final de una serie significa "sin techo".
constexpr std::string_view kTierNoCeilingMarker = "+";
// Bloque A / M5: ventana deslizante de los topes (por minuto, con nombre).
constexpr double kCapWindowS = 60.0;

// Limites del motor visual. El servidor HTTP ya valida, pero el motor no puede
// fiarse: un lienzo de 0 deja la escala en division por cero y una opacidad de
// 500 pintaria un marco opaco tapando el directo.
constexpr int kMinCanvasSide = 320;
constexpr int kMaxCanvasSide = 7680;
constexpr int kMinFrameOpacity = 0;
constexpr int kMaxFrameOpacity = 100;
constexpr int kMaxFrameBorderPx = 12;
constexpr int kMaxFrameRadiusPx = 64;
constexpr int kMaxFramePaddingPx = 120;
constexpr int kMaxTextOutlinePx = 8;

int clamp_int(int value, int lo, int hi) noexcept {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/// Lee una clave entera de la config venga como entero o como decimal.
///
/// Hace falta porque `GameConfig::get_double` SOLO lee variantes `double`: una
/// clave guardada como entero y leida con get_double devuelve el fallback en
/// silencio. Ese fallo silencioso ya estaba en el producto: `on_complete_text_size`
/// y `glow_intensity_px` se guardaban como enteros y se leian con get_double, asi
/// que el operador los configuraba y no pasaba nada.
int read_config_int(const gamesdk::GameConfig& config, std::string_view key, int fallback) {
    const auto* value = config.find(key);
    if (value == nullptr) {
        return fallback;
    }
    if (const auto* as_int = std::get_if<std::int64_t>(value); as_int != nullptr) {
        return static_cast<int>(*as_int);
    }
    if (const auto* as_double = std::get_if<double>(value); as_double != nullptr) {
        return static_cast<int>(*as_double);
    }
    return fallback;
}

constexpr double kMaxRecentEventsAgeS = 4.0;
constexpr std::size_t kMaxRecentEvents = 6;

constexpr std::string_view kIconLike = "\xe2\x9d\xa4";     // ❤
constexpr std::string_view kIconShare = "\xf0\x9f\x94\x84"; // 🔄
constexpr std::string_view kIconFollow = "\xe2\x9c\xa8";    // ✨
constexpr std::string_view kIconGift = "\xf0\x9f\x8e\x81"; // 🎁
constexpr std::string_view kIconChat = "\xf0\x9f\x92\xac"; // 💬

// Bloque A — helpers de formato numérico de los tramos de regalo.
std::string_view trim_view_soft(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) s.remove_suffix(1);
    return s;
}

std::optional<double> parse_seconds_token(std::string_view token) {
    token = trim_view_soft(token);
    if (token.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        const double value = std::stod(std::string(token), &consumed);
        if (consumed != token.size()) return std::nullopt;
        return std::max(0.0, value);
    } catch (...) {
        return std::nullopt;
    }
}

// '10', '10+', '10-99'. Devuelve true si el rango es valido; hi = -1 sin techo.
bool parse_coins_range(std::string_view token, int& lo, int& hi) {
    token = trim_view_soft(token);
    if (token.empty()) return false;
    lo = -1;
    hi = -1;
    const bool ends_with_plus = token.ends_with('+');
    if (ends_with_plus) token.remove_suffix(1);
    const auto dash = token.find('-');
    std::string_view first = token;
    std::string_view second;
    if (dash != std::string_view::npos) {
        first = trim_view_soft(token.substr(0, dash));
        second = trim_view_soft(token.substr(dash + 1));
    }
    auto to_int = [](std::string_view v, int& out) {
        if (v.empty()) return false;
        try {
            std::size_t consumed = 0;
            const int value = std::stoi(std::string(v), &consumed);
            if (consumed != v.size()) return false;
            out = value;
            return true;
        } catch (...) {
            return false;
        }
    };
    int parsed_lo = 0;
    int parsed_hi = 0;
    if (!to_int(first, parsed_lo)) return false;
    if (!second.empty()) {
        if (!to_int(second, parsed_hi)) return false;
        if (parsed_hi < parsed_lo) return false;
        hi = parsed_hi;
    } else if (ends_with_plus) {
        hi = -1;
    } else if (dash == std::string_view::npos) {
        hi = parsed_lo;
    } else {
        hi = -1;
    }
    lo = parsed_lo;
    return true;
}

// Parser de la regla de tramos: cada linea es 'rango: segundos' o 'nombre: segundos'.
std::vector<LiveTimerGame::GiftTier> parse_gift_tiers_text(std::string_view text) {
    std::vector<LiveTimerGame::GiftTier> out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        auto trimmed = trim_view_soft(std::string_view(line));
        if (trimmed.empty() || trimmed.starts_with('#')) continue;
        const auto colon = trimmed.find(':');
        if (colon == std::string_view::npos) continue;
        const auto lhs = trim_view_soft(trimmed.substr(0, colon));
        const auto rhs = trim_view_soft(trimmed.substr(colon + 1));
        const auto seconds = parse_seconds_token(rhs);
        if (!seconds.has_value()) continue;

        LiveTimerGame::GiftTier tier;
        tier.seconds = *seconds;

        int lo = -1, hi = -1;
        const bool is_numeric = !lhs.empty()
            && (std::isdigit(static_cast<unsigned char>(lhs.front())) != 0);
        if (is_numeric && parse_coins_range(lhs, lo, hi)) {
            tier.min_coins = lo;
            tier.max_coins = hi;
            out.push_back(std::move(tier));
            continue;
        }
        std::string lower(lhs);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        tier.has_name = true;
        tier.name = std::move(lower);
        out.push_back(std::move(tier));
    }
    return out;
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
    config.set(std::string(kInitialTimeS), 0.0);
    config.set(std::string(kTimePerLikeS), 0.0);
    config.set(std::string(kTimePerShareS), 0.0);
    config.set(std::string(kTimePerFollowS), 0.0);
    config.set(std::string(kTimePerGiftCoinS), 0.0);
    config.set(std::string(kTimePerChatS), 0.0);

    // Bloque A — reglas: defaults neutros (sin cambio de comportamiento).
    config.set(std::string(kLikeUseMagnitude), true);
    config.set(std::string(kMultSubcriber), 1.0);
    config.set(std::string(kMultFollower), 1.0);
    config.set(std::string(kMultModerator), 1.0);
    config.set(std::string(kCapPerEventS), 0.0);
    config.set(std::string(kCapPerUserPerMinS), 0.0);
    config.set(std::string(kCapTotalPerMinS), 0.0);
    config.set(std::string(kFloorTimeS), 0.0);
    config.set(std::string(kGiftTiers), std::string(""));
    config.set(std::string(kPopupsEnabled), true);
    config.set(std::string(kPopupShowActor), true);
    config.set(std::string(kAnchorPosition), std::string("center"));
    config.set(std::string(kAnchorMarginPct), std::int64_t{2});

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
    // Fase 5 — motor visual: defaults neutros (el overlay se ve como antes).
    config.set(std::string(kScaleMode), std::string("auto"));
    config.set(std::string(kCanvasWidth), std::int64_t{1920});
    config.set(std::string(kCanvasHeight), std::int64_t{1080});
    config.set(std::string(kFrameStyle), std::string("none"));
    config.set(std::string(kFrameColor), std::string("#00FFFF"));
    config.set(std::string(kFrameOpacity), std::int64_t{55});
    config.set(std::string(kFrameBorderPx), std::int64_t{1});
    config.set(std::string(kFrameRadiusPx), std::int64_t{4});
    config.set(std::string(kFramePaddingPx), std::int64_t{28});
    config.set(std::string(kFrameBrackets), false);
    config.set(std::string(kFrameGrid), false);
    config.set(std::string(kFrameScanlines), false);
    config.set(std::string(kTextOutlinePx), std::int64_t{0});
    config.set(std::string(kTextOutlineColor), std::string("#000000"));
    config.set(std::string(kTimeSeparator), std::string(":"));
    config.set(std::string(kShowHours), true);
    config.set(std::string(kWarnSeconds), std::int64_t{60});
    config.set(std::string(kDangerSeconds), std::int64_t{10});
    config.set(std::string(kDangerEffect), std::string("pulse"));
    config.set(std::string(kProgressStyle), std::string("none"));
    config.set(std::string(kProgressThicknessPx), std::int64_t{6});
    config.set(std::string(kProgressColor), std::string(""));
    config.set(std::string(kParticlesEnabled), false);
    config.set(std::string(kParticlesStyle), std::string("none"));
    config.set(std::string(kParticlesBudget), std::int64_t{120});
    config.set(std::string(kParticlesDensity), 1.0);
    config.set(std::string(kParticlesForce), false);
    config.set(std::string(kTitleGlow), false);
    config.set(std::string(kCounterGlow), false);
    config.set(std::string(kSubtitleGlow), false);
    config.set(std::string(kGlowColor), std::string("#FFD700"));
    config.set(std::string(kGlowIntensity), std::int64_t{8});
    config.set(std::string(kPulseSpeed), 1.5);
    // V3: digit transition + palette
    config.set(std::string(kDigitEffect), std::string("none"));
    config.set(std::string(kColorPreset), std::string("neon-green"));

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

    // Bloque A — reglas de tiempo.
    apply_bool(kLikeUseMagnitude);
    apply_double(kMultSubcriber);
    apply_double(kMultFollower);
    apply_double(kMultModerator);
    apply_double(kCapPerEventS);
    apply_double(kCapPerUserPerMinS);
    apply_double(kCapTotalPerMinS);
    apply_double(kFloorTimeS);
    apply_string(kGiftTiers);
    apply_bool(kPopupsEnabled);
    apply_bool(kPopupShowActor);
    apply_string(kAnchorPosition);
    apply_int(kAnchorMarginPct);

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

    // Fase 5 — motor visual. Hay que copiar las claves entrantes a `effective`
    // como las demas: sin esto se leen los defaults y el operador configura en
    // balde (lo cazo el test del contrato).
    apply_string(kScaleMode);
    apply_int(kCanvasWidth);
    apply_int(kCanvasHeight);
    apply_string(kFrameStyle);
    apply_string(kFrameColor);
    apply_int(kFrameOpacity);
    apply_int(kFrameBorderPx);
    apply_int(kFrameRadiusPx);
    apply_int(kFramePaddingPx);
    apply_bool(kFrameBrackets);
    apply_bool(kFrameGrid);
    apply_bool(kFrameScanlines);
    apply_int(kTextOutlinePx);
    apply_string(kTextOutlineColor);
    apply_string(kTimeSeparator);
    apply_bool(kShowHours);
    apply_int(kWarnSeconds);
    apply_int(kDangerSeconds);
    apply_string(kDangerEffect);
    apply_string(kProgressStyle);
    apply_int(kProgressThicknessPx);
    apply_string(kProgressColor);
    apply_bool(kParticlesEnabled);
    apply_string(kParticlesStyle);
    apply_int(kParticlesBudget);
    apply_double(kParticlesDensity);
    apply_bool(kParticlesForce);

    config_ = std::move(effective);

    state_.time_per_like = config_.get_double(kTimePerLikeS, 0.0);
    state_.time_per_share = config_.get_double(kTimePerShareS, 0.0);
    state_.time_per_follow = config_.get_double(kTimePerFollowS, 0.0);
    state_.time_per_gift_coin = config_.get_double(kTimePerGiftCoinS, 0.0);
    state_.time_per_chat = config_.get_double(kTimePerChatS, 0.0);
    state_.max_time_s = config_.get_double(kMaxTimeS, 0.0);

    // Bloque A — reglas. Los mults y los topes se acotan: un multiplicador
    // negativo convertiria un like en una resta oculta (bug dificil de mirar).
    state_.like_use_magnitude = config_.get_bool(kLikeUseMagnitude, true);
    auto clamp_nonneg = [&](std::string_view key, double fallback) {
        double v = config_.get_double(key, fallback);
        if (v < 0.0) {
            v = 0.0;
            config_.set(std::string(key), 0.0);
        }
        return v;
    };
    state_.mult_subscriber = clamp_nonneg(kMultSubcriber, 1.0);
    state_.mult_follower = clamp_nonneg(kMultFollower, 1.0);
    state_.mult_moderator = clamp_nonneg(kMultModerator, 1.0);
    state_.cap_per_event_s = clamp_nonneg(kCapPerEventS, 0.0);
    state_.cap_per_user_per_minute_s = clamp_nonneg(kCapPerUserPerMinS, 0.0);
    state_.cap_total_per_minute_s = clamp_nonneg(kCapTotalPerMinS, 0.0);
    state_.floor_time_s = clamp_nonneg(kFloorTimeS, 0.0);
    // M4 — tramos de regalo. Se parsean aqui y se cachean; el texto queda como
    // config para persistencia/serializacion.
    state_.gift_tiers = config_.get_string(kGiftTiers, "");
    state_.popups_enabled = config_.get_bool(kPopupsEnabled, true);
    state_.popup_show_actor = config_.get_bool(kPopupShowActor, true);

    // R5 — anclaje: la posicion se valida con lista blanca (es un valor del
    // overlay repetido en 9 motifs distintos).
    {
        auto raw = config_.get_string(kAnchorPosition, "center");
        if (raw != "center"
            && raw != "top-left" && raw != "top-center" && raw != "top-right"
            && raw != "middle-left" && raw != "middle-right"
            && raw != "bottom-left" && raw != "bottom-center" && raw != "bottom-right") {
            raw = "center";
            config_.set(std::string(kAnchorPosition), raw);
        }
        state_.anchor_position = raw;
    }
    state_.anchor_margin_pct = clamp_int(read_config_int(config_, kAnchorMarginPct, 2), 0, 20);
    gift_tiers_ = state_.gift_tiers.empty()
        ? std::vector<GiftTier>{}
        : parse_gift_tiers_text(state_.gift_tiers);

    double new_initial = config_.get_double(kInitialTimeS, 300.0);
    state_.initial_seconds = new_initial;
    // V2 (Fase 2): apply_config solo toca el reloj en la fase de PREPARACION, y
    // solo si el tiempo inicial cambio de verdad.
    //
    // - Si el timer esta CORRIENDO, no se toca nunca: cambiar el diseno (o el
    //   propio initial_time_s) no puede mover una cuenta en vivo. Para sumar o
    //   restar en caliente estan los botones de ajuste (- / +).
    // - Si esta pausado o completado, tampoco: el runtime es el SSOT.
    // - Si esta en reposo (sin cuenta en curso) y el initial cambio, se adopta
    //   el valor nuevo: es la fase de configuracion, antes de pulsar Iniciar.
    //   La condicion "cambio de verdad" es la que protege el requisito de
    //   conservar el tiempo: tras restaurar, cambiar solo el diseno NO puede
    //   reescribir el tiempo restaurado.
    const bool idle = !state_.running && !state_.paused && !state_.completed;
    if (idle && new_initial != old_initial) {
        state_.remaining_seconds = std::max(0.0, new_initial);
    }
    // V2 (Fase 2): aqui NO se aplica el tope de max_time_s. El clamp vive en las
    // rutas que si cambian el tiempo en vivo (on_game_input_event y adjust_time),
    // que es donde tiene sentido y donde se reporta el delta real. Aplicarlo
    // tambien aqui recortaba el valor recien configurado antes de arrancar.

    state_.title_text = config_.get_string(kTitleText, "\xf0\x9f\x8e\xaf Extiende el Live");
    state_.subtitle_text = config_.get_string(kSubtitleText, "\xf0\x9f\x93\x8c Cada coin suma {time_per_gift_coin}s");
    state_.on_complete_sound_path = config_.get_string(kSoundPath, "");
    state_.on_complete_repeat = config_.get_bool(kSoundRepeat, false);
    state_.on_complete_volume = config_.get_double(kSoundVolume, 1.0);
    state_.popup_style.add_color = config_.get_string(kPopupAddColor, "#00AAFF");
    state_.popup_style.subtract_color = config_.get_string(kPopupSubtractColor, "#FF4444");
    state_.on_complete_text = config_.get_string(kOnCompleteText, "TIEMPO CUMPLIDO");
    state_.on_complete_text_color = config_.get_string(kOnCompleteTextColor, "#FFD700");
    // Bugfix Fase 5: esta clave se guarda como entero y se leia con get_double, asi
    // que el valor configurado nunca llegaba al overlay (siempre 48). Se acota al
    // mismo rango que valida el HTTP (8..400) para que un config importado a mano
    // no pueda pintar texto invisible.
    state_.on_complete_text_size = clamp_int(read_config_int(config_, kOnCompleteTextSize, 48), 8, 400);
    state_.tick_sound_path = config_.get_string(kTickSoundPath, "");
    state_.tick_sound_volume = config_.get_double(kTickSoundVolume, 1.0);
    state_.add_sound_path = config_.get_string(kAddSoundPath, "");
    state_.add_sound_volume = config_.get_double(kAddSoundVolume, 1.0);

    // Validate effect names — Fase 5 anade efectos ambientales (V9). La lista
    // blanca vive aqui porque el overlay pinta por clase CSS: un nombre que no
    // exista seria una clase muerta y un efecto que no pasa nada.
    auto is_valid_effect = [](std::string_view value) {
        return value == "none" || value == "glow" || value == "pulse"
            || value == "heartbeat" || value == "float" || value == "flicker"
            || value == "shake";
    };
    auto validate_effect = [&](std::string_view key, std::string fallback) {
        auto raw = config_.get_string(key, fallback);
        if (!is_valid_effect(raw)) {
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
    // Bugfix Fase 5: misma causa que on_complete_text_size — entero leido como
    // double. El overlay ya acotaba 1..60; ahora el valor configurado llega de verdad.
    state_.glow_intensity_px = clamp_int(read_config_int(config_, kGlowIntensity, 8), 1, 60);
    state_.pulse_speed_s = config_.get_double(kPulseSpeed, 1.5);
    // V3: validate digit_effect, color_preset
    {
        auto raw = config_.get_string(kDigitEffect, "none");
        if (raw != "none" && raw != "flip" && raw != "roll" && raw != "pop" && raw != "fade"
            && raw != "odometer" && raw != "typewriter" && raw != "blur") {
            raw = "none";
            config_.set(std::string(kDigitEffect), raw);
        }
        state_.digit_effect = raw;
    }
    {
        auto raw = config_.get_string(kColorPreset, "neon-green");
        if (raw != "neon-green" && raw != "cyber-blue" && raw != "clean-white" && raw != "rose-gold") {
            raw = "neon-green";
            config_.set(std::string(kColorPreset), raw);
        }
        state_.color_preset = raw;
    }

    // Fase 5 — motor visual. Todo con default neutro y acotado: el motor nunca
    // confia en lo que llega, ni del HTTP ni de un JSON importado a mano.
    auto apply_clamped_int = [&](std::string_view key, int fallback, int lo, int hi) {
        const auto raw = read_config_int(config_, key, fallback);
        const auto clamped = clamp_int(raw, lo, hi);
        if (clamped != raw) {
            config_.set(std::string(key), std::int64_t{clamped});
        }
        return clamped;
    };

    {
        auto raw = config_.get_string(kScaleMode, "auto");
        if (raw != "auto" && raw != "off") {
            raw = "auto";
            config_.set(std::string(kScaleMode), raw);
        }
        state_.scale_mode = raw;
    }
    state_.canvas_width = apply_clamped_int(kCanvasWidth, 1920, kMinCanvasSide, kMaxCanvasSide);
    state_.canvas_height = apply_clamped_int(kCanvasHeight, 1080, kMinCanvasSide, kMaxCanvasSide);
    {
        auto raw = config_.get_string(kFrameStyle, "none");
        if (raw != "none" && raw != "card" && raw != "glass" && raw != "neon"
            && raw != "ribbon" && raw != "badge") {
            raw = "none";
            config_.set(std::string(kFrameStyle), raw);
        }
        state_.frame_style = raw;
    }
    state_.frame_color = config_.get_string(kFrameColor, "#00FFFF");
    state_.frame_opacity = apply_clamped_int(kFrameOpacity, 55, kMinFrameOpacity, kMaxFrameOpacity);
    state_.frame_border_px = apply_clamped_int(kFrameBorderPx, 1, 0, kMaxFrameBorderPx);
    state_.frame_radius_px = apply_clamped_int(kFrameRadiusPx, 4, 0, kMaxFrameRadiusPx);
    state_.frame_padding_px = apply_clamped_int(kFramePaddingPx, 28, 0, kMaxFramePaddingPx);
    state_.frame_brackets = config_.get_bool(kFrameBrackets, false);
    state_.frame_grid = config_.get_bool(kFrameGrid, false);
    state_.frame_scanlines = config_.get_bool(kFrameScanlines, false);
    state_.text_outline_px = apply_clamped_int(kTextOutlinePx, 0, 0, kMaxTextOutlinePx);
    state_.text_outline_color = config_.get_string(kTextOutlineColor, "#000000");

    // V6 — formato del tiempo.
    {
        auto raw = config_.get_string(kTimeSeparator, ":");
        const bool allowed = raw == ":" || raw == "\xc2\xb7" /* · */ || raw == "."
            || raw == " " || raw == "-" || raw == "|";
        if (!allowed) {
            raw = ":";
            config_.set(std::string(kTimeSeparator), raw);
        }
        state_.time_separator = raw;
    }
    state_.show_hours = config_.get_bool(kShowHours, true);

    // V13 — estados. Los umbrales se acotan y se ordenan: si peligro quedara por
    // encima de aviso, el overlay nunca pintaria el estado de aviso y el operador
    // no entenderia por que.
    state_.warn_seconds = apply_clamped_int(kWarnSeconds, 60, 0, kMaxThresholdSeconds);
    state_.danger_seconds = apply_clamped_int(kDangerSeconds, 10, 0, kMaxThresholdSeconds);
    if (state_.danger_seconds > state_.warn_seconds) {
        state_.danger_seconds = state_.warn_seconds;
        config_.set(std::string(kDangerSeconds), std::int64_t{state_.danger_seconds});
    }
    {
        auto raw = config_.get_string(kDangerEffect, "pulse");
        if (raw != "none" && raw != "pulse" && raw != "glitch" && raw != "flash") {
            raw = "pulse";
            config_.set(std::string(kDangerEffect), raw);
        }
        state_.danger_effect = raw;
    }

    // V5 — medidor de progreso.
    {
        auto raw = config_.get_string(kProgressStyle, "none");
        if (raw != "none" && raw != "bar" && raw != "ring") {
            raw = "none";
            config_.set(std::string(kProgressStyle), raw);
        }
        state_.progress_style = raw;
    }
    state_.progress_thickness_px = apply_clamped_int(
        kProgressThicknessPx, 6, kMinProgressThicknessPx, kMaxProgressThicknessPx);
    state_.progress_color = config_.get_string(kProgressColor, "");

    // V11 — particulas. El estilo se valida igual que los efectos: el overlay pinta
    // por tipo, asi que un nombre que no exista seria decoracion que no ocurre.
    {
        auto raw = config_.get_string(kParticlesStyle, "none");
        if (raw != "none" && raw != "confetti" && raw != "sparks" && raw != "stars") {
            raw = "none";
            config_.set(std::string(kParticlesStyle), raw);
        }
        state_.particles_style = raw;
    }
    state_.particles_enabled = config_.get_bool(kParticlesEnabled, false);
    state_.particles_budget = apply_clamped_int(
        kParticlesBudget, 120, kMinParticlesBudget, kMaxParticlesBudget);
    {
        auto density = config_.get_double(kParticlesDensity, 1.0);
        if (!std::isfinite(density)) {
            density = 1.0;
        }
        if (density < kMinParticlesDensity) density = kMinParticlesDensity;
        if (density > kMaxParticlesDensity) density = kMaxParticlesDensity;
        state_.particles_density = density;
        config_.set(std::string(kParticlesDensity), density);
    }
    state_.particles_force = config_.get_bool(kParticlesForce, false);
    // Coherencia: con el estilo en "none" no hay nada que emitir, asi que enabled
    // se apaga. Evita el estado "activado pero sin tipo", que no hace nada y
    // confunde al operador que lo mira en la interfaz.
    if (state_.particles_style == "none") {
        state_.particles_enabled = false;
    }

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
    // V2: Iniciar CONTINUA desde el tiempo que ya haya, en vez de resetear al
    // tiempo inicial. Es lo que pide el requisito: al reiniciar el panel el
    // tiempo se conserva y arranca al pulsar Iniciar. Antes esto hacia
    // `remaining = initial_seconds`, con lo que pulsar Iniciar tiraba el
    // progreso restaurado y las coins acumuladas mientras esperaba. Solo se cae
    // al tiempo inicial cuando no hay nada que continuar (arranque en limpio).
    double start_from = state_.remaining_seconds;
    if (!(start_from > 0.0)) {        // NaN-safe: NaN y negativos caen aqui
        start_from = state_.initial_seconds;
    }
    const bool has_time = start_from > 0.0;
    state_.running = has_time;
    state_.remaining_seconds = has_time ? start_from : 0.0;
    // V2: pulsar Iniciar tambien DES-OCULTA el timer. Tras restaurar, arm()
    // deja hidden_=true (el overlay muestra "--:--:--" y se bloquea el input);
    // sin esto, Iniciar ponia running=true pero el overlay seguia mostrando
    // guiones porque `enabled` seguia en false. Solo se habilita si hay tiempo
    // que contar: sin tiempo configurado no hay nada que mostrar.
    if (has_time) {
        hidden_ = false;
    }
    state_.recent_events.clear();
    total_time_added_ = 0.0;
    // Bloque A / M5: ventanas de aporte por usuario y global son de sesión;
    // se resetean al armar/activar para que los topes no arrastren el directo anterior.
    user_contributions_.clear();
    total_contributions_.clear();
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
    user_contributions_.clear();
    total_contributions_.clear();
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
    // V2: tambien se ACUMULA cuando el timer esta armado y espera a que el
    // usuario pulse Iniciar (running=false, paused=false). Antes se
    // descartaban esos eventos, asi que al restaurar tras cerrar el panel las
    // coins que llegaron mientras estaba cerrado se perdian en silencio, y en
    // un arranque sin tiempo configurado tampoco contaba nada. Se sigue
    // bloqueando si esta pausado a proposito, completado u oculto.
    if (hidden_ || state_.completed || state_.paused) return;
    // T1.1f-r2: no tick() here — remaining_seconds() computes dynamically.
    // Events add delta directly to state_.remaining_seconds; the SSOT baseline
    // (start_time_) stays stable, preserving the countdown integrity.

    double delta = 0.0;
    std::string_view icon;
    std::string_view label;

    switch (event.kind) {
    case gamesdk::GameInputEventKind::like: {
        // Bloque A / M2: likes por magnitud. Un lote de N likes suma N veces el
        // valor configurado; con el flag apagado, comportamiento historico
        // (1 unidad por evento, no importan cuantos likes traiga el lote).
        if (state_.like_use_magnitude) {
            const auto n = event.like_count > 0 ? event.like_count : 1u;
            delta = static_cast<double>(n) * state_.time_per_like;
        } else {
            delta = state_.time_per_like;
        }
        icon = kIconLike;
        label = "like";
        break;
    }
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
        // Bloque A / M4: una regla de tramos que casa sustituye por completo al
        // multiplicador plano; sin tramo, se usa el multiplicador historico.
        const std::string_view gift_name = event.gift.has_value()
            ? std::string_view(event.gift->gift_name)
            : std::string_view{};
        const double tiered = resolve_gift_seconds(gift_name, coins);
        delta = tiered >= 0.0 ? tiered : coins * state_.time_per_gift_coin;
        icon = kIconGift;
        label = event.gift.has_value() && !event.gift->gift_name.empty()
            ? std::string_view(event.gift->gift_name)
            : std::string_view("gift");
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

    // Bloque A / M3: multiplicador por tipo de espectador aplicado al delta del
    // evento (sub/follower/moderator). Solo a deltas POSITIVOS — multiplicar una
    // resta agravia sin aportar valor de diseno.
    if (delta > 0.0) {
        delta *= actor_multiplier(event.actor);
    }

    // Bloque A / M5: topes (cap por evento, por usuario/min y global/min).
    // Solo limitan aportes POSITIVOS; las restas no se tocan.
    const auto actor_key = !event.actor.id.empty()
        ? event.actor.id
        : (!event.actor.username.empty()
               ? event.actor.username
               : (!event.actor.display_name.empty() ? event.actor.display_name
                                                   : std::string("_anon")));
    bool capped = false;
    delta = apply_caps(delta, actor_key, &capped);
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
    // Bloque A / M5 (suelo): el reloj no baja por debajo de floor_time_s. Con
    // exactamente 0 no corre, queda fijo en el suelo: el operador decide en un
    // solo control cómo termina el live.
    const bool completed_by_event = state_.remaining_seconds < std::max(0.0, state_.floor_time_s);
    if (completed_by_event) {
        if (state_.floor_time_s > 0.0) {
            state_.remaining_seconds = state_.floor_time_s;
        } else {
            state_.remaining_seconds = 0.0;
            state_.running = false;
            state_.completed = true;
        }
    } else if (state_.max_time_s > 0.0 && state_.remaining_seconds > state_.max_time_s) {
        state_.remaining_seconds = state_.max_time_s;
    }

    // A8: skip the popup when this event is what exhausted the timer. The
    // overlay will fire confetti and the completed banner; a simultaneous
    // negative-time popup is confusing UX.
    if (completed_by_event && state_.floor_time_s <= 0.0) return;

    // Bloque A / M1: el popup lleva el nombre del actor (o el best-alias).
    const auto& a = event.actor;
    std::string actor_name = !a.display_name.empty()
        ? a.display_name
        : (!a.username.empty() ? a.username : std::string{});
    add_event_popup(icon, label, delta, actor_name, capped);
}

void LiveTimerGame::ContributionWindow::push(double seconds, double window_s,
                                             const std::chrono::steady_clock::time_point& now) {
    // Purga entradas viejas (respetando la ventana real) y entonces añade el
    // aporte. Con un tope medido en minutos no vale retener mas de la ventana.
    sum_recent(window_s, now);
    entries.push_back({now, seconds});
}

double LiveTimerGame::ContributionWindow::sum_recent(double window_s, const std::chrono::steady_clock::time_point& now) {
    const auto cutoff = now - std::chrono::duration<double>(window_s);
    auto first_live = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& e) { return e.at >= cutoff; });
    entries.erase(entries.begin(), first_live);
    double total = 0.0;
    for (const auto& e : entries) total += e.seconds;
    return total;
}

void LiveTimerGame::ContributionWindow::clear() noexcept {
    entries.clear();
}

double LiveTimerGame::actor_multiplier(const gamesdk::GameInputActor& actor) const noexcept {
    // Bloque A / M3: un actor SIN roles devuelve el multiplicador neutro 1.0.
    // Con roles, se toma el MAS ALTO de los roles que el actor tiene (`max`):
    // un moderador que tambien es suscriptor se beneficia del suscriptor, y un
    // moderador puro con mult 0.0 no aporta nada (configuracion de moderacion).
    // NOTA: sin este diseno max, configurar "moderador = 0" nunca surtiria
    // efecto (el 1.0 neutro 'ganaba' siempre).
    double mult = 1.0;
    bool has_role = false;
    if (actor.is_subscriber) { mult = has_role ? std::max(mult, state_.mult_subscriber) : state_.mult_subscriber; has_role = true; }
    if (actor.is_follower)   { mult = has_role ? std::max(mult, state_.mult_follower)   : state_.mult_follower;   has_role = true; }
    if (actor.is_moderator)  { mult = has_role ? std::max(mult, state_.mult_moderator)  : state_.mult_moderator;  has_role = true; }
    return mult;
}

double LiveTimerGame::resolve_gift_seconds(std::string_view gift_name, double coins) const noexcept {
    // Bloque A / M4: tramos de regalo. Primero se mira excepción por NOMBRE
    // (la regla más específica), luego el rango de coins. Si ninguna regla
    // casa, se devuelve -1.0 y el llamador recurre al multiplicador historico.
    if (gift_tiers_.empty()) return -1.0;

    if (!gift_name.empty()) {
        std::string lower(gift_name);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        for (const auto& tier : gift_tiers_) {
            if (tier.has_name && !tier.name.empty() && lower.find(tier.name) != std::string::npos) {
                return tier.seconds;
            }
        }
    }

    for (const auto& tier : gift_tiers_) {
        if (tier.has_name) continue;
        const int c = static_cast<int>(std::lround(coins));
        if (c >= tier.min_coins && (tier.max_coins < 0 || c <= tier.max_coins)) {
            return tier.seconds;
        }
    }
    return -1.0;
}

double LiveTimerGame::apply_caps(double delta, const std::string& actor_key, bool* capped) {
    if (capped != nullptr) *capped = false;
    if (delta <= 0.0) return delta;  // sólo topa aportes positivos
    bool was_capped = false;

    // M5 — cap por evento.
    if (state_.cap_per_event_s > 0.0 && delta > state_.cap_per_event_s) {
        delta = state_.cap_per_event_s;
        was_capped = true;
    }

    const auto now = std::chrono::steady_clock::now();

    // M5 — cap por usuario durante la ventana deslizante.
    if (state_.cap_per_user_per_minute_s > 0.0 && !actor_key.empty() && actor_key != "_anon") {
        auto& win = user_contributions_[actor_key];
        const double already = win.sum_recent(state_.cap_per_user_per_minute_s, now);
        const double remaining = state_.cap_per_user_per_minute_s - already;
        if (remaining <= 0.0) {
            delta = 0.0;
        } else if (delta > remaining) {
            delta = remaining;
            was_capped = true;
        }
    }

    // M5 — cap global (todos los eventos) durante la ventana.
    if (state_.cap_total_per_minute_s > 0.0) {
        const double already = total_contributions_.sum_recent(state_.cap_total_per_minute_s, now);
        const double remaining = state_.cap_total_per_minute_s - already;
        if (remaining <= 0.0) {
            delta = 0.0;
        } else if (delta > remaining) {
            delta = remaining;
            was_capped = true;
        }
    }

    if (capped != nullptr) *capped = was_capped;

    // Registrar la contribución (sólo positivos): el tope es "por minuto".
    if (delta > 0.0) {
        if (state_.cap_per_user_per_minute_s > 0.0 && !actor_key.empty() && actor_key != "_anon") {
            user_contributions_[actor_key].push(delta, kCapWindowS, now);
        }
        if (state_.cap_total_per_minute_s > 0.0) {
            total_contributions_.push(delta, kCapWindowS, now);
        }
    }
    return delta;
}

void LiveTimerGame::add_event_popup(std::string_view icon, std::string_view label, double delta,
                                     std::string_view actor_name, bool capped) {
    prune_old_events();
    auto id = ++event_id_counter_;
    state_.recent_events.push_back({
        id,
        std::string(icon),
        std::string(label),
        delta,
        delta >= 0,
        std::chrono::steady_clock::now(),
        std::string(actor_name),
        capped,
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
