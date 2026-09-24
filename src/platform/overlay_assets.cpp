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
            "\"initial_seconds\":0,"
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
            "\"popupAddColor\":\"#00AAFF\","
            "\"popupSubtractColor\":\"#FF4444\","
            "\"completedText\":\"TIEMPO CUMPLIDO\","
            "\"completedTextColor\":\"#FFD700\","
            "\"completedTextSize\":48,"
            "\"title_effect\":\"none\","
            "\"counter_effect\":\"none\","
            "\"subtitle_effect\":\"none\","
            "\"title_glow_enabled\":false,"
            "\"counter_glow_enabled\":false,"
            "\"subtitle_glow_enabled\":false,"
            "\"glow_color\":\"#FFD700\","
            "\"glow_intensity_px\":8,"
            "\"pulse_speed_s\":1.5,"
            "\"digit_effect\":\"none\","
            "\"color_preset\":\"neon-green\","
            // Fase 5: motor visual con defaults neutros.
            "\"scale_mode\":\"auto\","
            "\"canvas_width\":1920,"
            "\"canvas_height\":1080,"
            "\"frame_style\":\"none\","
            "\"frame_color\":\"#00FFFF\","
            "\"frame_opacity\":55,"
            "\"frame_border_px\":1,"
            "\"frame_radius_px\":4,"
            "\"frame_padding_px\":28,"
            "\"frame_brackets\":false,"
            "\"frame_grid\":false,"
            "\"frame_scanlines\":false,"
            "\"text_outline_px\":0,"
            "\"text_outline_color\":\"#000000\","
            "\"time_separator\":\":\","
            "\"show_hours\":true,"
            "\"warn_seconds\":60,"
            "\"danger_seconds\":10,"
            "\"danger_effect\":\"pulse\","
            "\"progress_style\":\"none\","
            "\"progress_thickness_px\":6,"
            "\"progress_color\":\"\","
            "\"particles_enabled\":false,"
            "\"particles_style\":\"none\","
            "\"particles_budget\":120,"
            "\"particles_density\":1.0,"
            "\"particles_force\":false,"
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
        for (unsigned char c : v) {
            if (c == '\\') r += "\\\\";
            else if (c == '"') r += "\\\"";
            else if (c == '\n') r += "\\n";
            else if (c == '\r') r += "\\r";
            else if (c == '\t') r += "\\t";
            else if (c < 0x20) {
                // B2: escapar el resto de controles como \u00XX — un byte crudo
                // invalida el JSON.parse del overlay y deja el sonido sin sonar.
                static constexpr char kHex[] = "0123456789abcdef";
                r += "\\u00";
                r += kHex[(c >> 4) & 0xF];
                r += kHex[c & 0xF];
            } else {
                r += static_cast<char>(c);
            }
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
    // R2: con popups_enabled=false el overlay no pinta nada — la lista va vacia.
    static const std::vector<nlp3::games::LiveTimerRecentEvent> kEmptyEvents{};
    for (const auto& ev : (s.popups_enabled ? s.recent_events : kEmptyEvents)) {
          if (!first) events_json << ",";
        first = false;
        events_json << "{"
            << "\"id\":" << ev.id << ","
            << "\"icon\":" << json_quote(ev.icon) << ","
            << "\"label\":" << json_quote(ev.label) << ","
            << "\"delta\":" << ev.delta_seconds << ","
            << "\"isAddition\":" << (ev.is_addition ? "true" : "false") << ","
            // Bloque A / M1 + R2: nombre del actor; se omite si el operador no lo quiere.
            << "\"actorName\":" << json_quote(s.popup_show_actor ? ev.actor_name : "") << ","
            // Bloque A / M5: el delta fue recortado por un tope.
            << "\"capped\":" << (ev.capped ? "true" : "false")
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
        << "\"title_effect\":" << json_quote(s.title_effect) << ","
        << "\"counter_effect\":" << json_quote(s.counter_effect) << ","
        << "\"subtitle_effect\":" << json_quote(s.subtitle_effect) << ","
        << "\"title_glow_enabled\":" << (s.title_glow_enabled ? "true" : "false") << ","
        << "\"counter_glow_enabled\":" << (s.counter_glow_enabled ? "true" : "false") << ","
        << "\"subtitle_glow_enabled\":" << (s.subtitle_glow_enabled ? "true" : "false") << ","
        << "\"glow_color\":" << json_quote(s.glow_color) << ","
        << "\"glow_intensity_px\":" << s.glow_intensity_px << ","
        << "\"pulse_speed_s\":" << s.pulse_speed_s << ","
        << "\"digit_effect\":" << json_quote(s.digit_effect) << ","
        << "\"color_preset\":" << json_quote(s.color_preset) << ","
        // Fase 5: motor visual (escala, marco, adornos y contorno).
        << "\"scale_mode\":" << json_quote(s.scale_mode) << ","
        << "\"canvas_width\":" << s.canvas_width << ","
        << "\"canvas_height\":" << s.canvas_height << ","
        << "\"frame_style\":" << json_quote(s.frame_style) << ","
        << "\"frame_color\":" << json_quote(s.frame_color) << ","
        << "\"frame_opacity\":" << s.frame_opacity << ","
        << "\"frame_border_px\":" << s.frame_border_px << ","
        << "\"frame_radius_px\":" << s.frame_radius_px << ","
        << "\"frame_padding_px\":" << s.frame_padding_px << ","
        << "\"frame_brackets\":" << (s.frame_brackets ? "true" : "false") << ","
        << "\"frame_grid\":" << (s.frame_grid ? "true" : "false") << ","
        << "\"frame_scanlines\":" << (s.frame_scanlines ? "true" : "false") << ","
        << "\"text_outline_px\":" << s.text_outline_px << ","
        << "\"text_outline_color\":" << json_quote(s.text_outline_color) << ","
        // Fase 5 (incremento 2): formato del tiempo, estados y medidor.
        << "\"time_separator\":" << json_quote(s.time_separator) << ","
        << "\"show_hours\":" << (s.show_hours ? "true" : "false") << ","
        << "\"warn_seconds\":" << s.warn_seconds << ","
        << "\"danger_seconds\":" << s.danger_seconds << ","
        << "\"danger_effect\":" << json_quote(s.danger_effect) << ","
        << "\"progress_style\":" << json_quote(s.progress_style) << ","
        << "\"progress_thickness_px\":" << s.progress_thickness_px << ","
        << "\"progress_color\":" << json_quote(s.progress_color) << ","
        // Fase 5 (incremento 3): particulas con presupuesto y auto-apagado.
        << "\"particles_enabled\":" << (s.particles_enabled ? "true" : "false") << ","
        << "\"particles_style\":" << json_quote(s.particles_style) << ","
        << "\"particles_budget\":" << s.particles_budget << ","
        << "\"particles_density\":" << s.particles_density << ","
        << "\"particles_force\":" << (s.particles_force ? "true" : "false") << ","
        // R5 — posicion del bloque en el overlay.
        << "\"anchor_position\":" << json_quote(s.anchor_position) << ","
        << "\"anchor_margin_pct\":" << s.anchor_margin_pct << ","
        // R6 — aviso/resumen para el panel (el overlay no usa esto, pero lo lee la UI).
        << "\"popup_show_actor\":" << (s.popup_show_actor ? "true" : "false") << ","
        << "\"popups_enabled\":" << (s.popups_enabled ? "true" : "false") << ","
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
