#include "platform/overlay_assets.hpp"

#include <sstream>

namespace {

static constexpr const char* kLiveTimerOverlayHtml =
#include "overlay_live_timer_html.inc"
;

} // namespace

namespace nlp3::platform {

std::string_view panel_overlay_live_timer_html() noexcept {
    return kLiveTimerOverlayHtml;
}

std::string build_live_timer_state_json(const games::LiveTimerGame* game) {
    // T1.1f-r2: drive completion to SSOT before serializing. tick() is now a
    // pure completion detector — it only mutates state on completion, never
    // while running. The const_cast is still needed because tick() is non-const
    // (it CAN set completed=true), but it no longer thrashes the SSOT baseline.
    if (game != nullptr) {
        const_cast<games::LiveTimerGame*>(game)->tick();
    }
    if (game == nullptr) {
        return "{"
            "\"remainingSeconds\":0,"
            "\"initial_seconds\":300,"
            "\"max_time_s\":0,"
            "\"format\":\"00:00:00\","
            "\"running\":false,"
            "\"paused\":false,"
            "\"enabled\":true,"
            "\"completed\":false,"
            "\"sessionId\":0,"
            "\"title\":\"\","
            "\"subtitle\":\"\","
            "\"titleStyle\":{},"
            "\"counterStyle\":{},"
            "\"subtitleStyle\":{},"
            "\"bgColor\":\"transparent\","
            "\"completedText\":\"TIEMPO CUMPLIDO\","
            "\"completedTextColor\":\"#FFD700\","
            "\"completedTextSize\":48,"
            "\"tick_sound_path\":\"\","
            "\"tick_sound_volume\":1.0,"
            "\"add_sound_path\":\"\","
            "\"add_sound_volume\":1.0,"
            "\"on_complete_sound_path\":\"\","
            "\"on_complete_volume\":1.0,"
            "\"on_complete_repeat\":false,"
            "\"recentEvents\":[]"
            "}";
    }

    const auto& s = game->state();
    auto rem = game->remaining_seconds();

    auto json_quote = [](std::string_view v) -> std::string {
        std::string r;
        r.reserve(v.size() + 8);
        for (auto c : v) {
            if (c == '\\') r += "\\\\";
            else if (c == '"') r += "\\\"";
            else if (c == '\n') r += "\\n";
            else if (c == '\r') r += "\\r";
            else if (c == '\t') r += "\\t";
            else r += c;
        }
        return "\"" + r + "\"";
    };

    auto style_json = [&](const games::LiveTimerVisualStyle& st) -> std::string {
        std::ostringstream oss;
        oss << "{"
            << "\"font_size_px\":" << st.font_size_px << ","
            << "\"font_color\":" << json_quote(st.font_color) << ","
            << "\"font_family\":" << json_quote(st.font_family) << ","
            << "\"bold\":" << (st.bold ? "true" : "false")
            << "}";
        return oss.str();
    };

    auto substitute_placeholders = [&](std::string_view tmpl) -> std::string {
        return nlp3::games::substitute_timer_placeholders(tmpl, s);
    };

    std::ostringstream events_json;
    events_json << "[";
    bool first = true;
    for (const auto& ev : s.recent_events) {
        if (!first) events_json << ",";
        first = false;
        events_json << "{"
            << "\"id\":" << ev.id << ","
            << "\"icon\":" << json_quote(ev.icon) << ","
            << "\"label\":" << json_quote(ev.label) << ","
            << "\"delta\":" << ev.delta_seconds << ","
            << "\"isAddition\":" << (ev.is_addition ? "true" : "false")
            << "}";
    }
    events_json << "]";

    std::ostringstream oss;
    oss << "{"
        << "\"remainingSeconds\":" << rem << ","
        << "\"initial_seconds\":" << s.initial_seconds << ","
        << "\"max_time_s\":" << s.max_time_s << ","
        << "\"format\":" << json_quote(game->format_time()) << ","
        << "\"running\":" << (s.running ? "true" : "false") << ","
        << "\"paused\":" << (s.paused ? "true" : "false") << ","
        << "\"enabled\":" << (game->is_enabled() ? "true" : "false") << ","
        << "\"completed\":" << (s.completed ? "true" : "false") << ","
        << "\"sessionId\":" << s.session_id << ","
        << "\"title\":" << json_quote(s.title_text) << ","
        << "\"subtitle\":" << json_quote(substitute_placeholders(s.subtitle_text)) << ","
        << "\"titleStyle\":" << style_json(s.title_style) << ","
        << "\"counterStyle\":" << style_json(s.counter_style) << ","
        << "\"subtitleStyle\":" << style_json(s.subtitle_style) << ","
        << "\"popupAddColor\":" << json_quote(s.popup_style.add_color) << ","
        << "\"popupSubtractColor\":" << json_quote(s.popup_style.subtract_color) << ","
        << "\"completedText\":" << json_quote(s.on_complete_text) << ","
        << "\"completedTextColor\":" << json_quote(s.on_complete_text_color) << ","
        << "\"completedTextSize\":" << s.on_complete_text_size << ","
        << "\"bgColor\":\"transparent\","
        << "\"title_effect\":" << json_quote(s.title_effect) << ","
        << "\"counter_effect\":" << json_quote(s.counter_effect) << ","
        << "\"subtitle_effect\":" << json_quote(s.subtitle_effect) << ","
        << "\"title_glow_enabled\":" << (s.title_glow_enabled ? "true" : "false") << ","
        << "\"counter_glow_enabled\":" << (s.counter_glow_enabled ? "true" : "false") << ","
        << "\"subtitle_glow_enabled\":" << (s.subtitle_glow_enabled ? "true" : "false") << ","
        << "\"glow_color\":" << json_quote(s.glow_color) << ","
        << "\"glow_intensity_px\":" << s.glow_intensity_px << ","
        << "\"wave_colors\":" << json_quote(s.wave_colors) << ","
        << "\"pulse_speed_s\":" << s.pulse_speed_s << ","
        << "\"shake_intensity\":" << json_quote(s.shake_intensity) << ","
        << "\"particles_enabled\":" << (s.particles_enabled ? "true" : "false") << ","
        << "\"particle_count\":" << s.particle_count << ","
        << "\"particle_color\":" << json_quote(s.particle_color) << ","
        // T1.4: overlay HTML5 audio reads these fields and plays via new Audio().
        // Empty path = total silence. Backend never plays sounds itself.
        << "\"tick_sound_path\":" << json_quote(s.tick_sound_path) << ","
        << "\"tick_sound_volume\":" << s.tick_sound_volume << ","
        << "\"add_sound_path\":" << json_quote(s.add_sound_path) << ","
        << "\"add_sound_volume\":" << s.add_sound_volume << ","
        << "\"on_complete_sound_path\":" << json_quote(s.on_complete_sound_path) << ","
        << "\"on_complete_volume\":" << s.on_complete_volume << ","
        << "\"on_complete_repeat\":" << (s.on_complete_repeat ? "true" : "false") << ","
        << "\"recentEvents\":" << events_json.str()
        << "}";
    return oss.str();
}

} // namespace nlp3::platform
