// Fase 5 — motor visual del Live Timer: escala relativa (M16) y marco (V1/V2/V3).
//
// Estos tests fijan DOS cosas por cada clave nueva:
//   1. El default es seguro: con la config de fabrica, el overlay se ve EXACTAMENTE
//      como antes (marco ninguno, escala automatica a 1920x1080, sin contorno).
//   2. El contrato del estado la publica: si una clave no llega al JSON que consume
//      el overlay, el operador la configura y no pasa nada.
//
// Sin el contrato en el JSON, una clave nueva es una mentira en la interfaz.

#include <cassert>
#include <string>

#include "games/live_timer_game.hpp"
#include "platform/overlay_assets.hpp"
#include "test_require.hpp"

namespace {

using nlp3::games::LiveTimerGame;

bool json_contains(const std::string& json, const std::string& needle) {
    return json.find(needle) != std::string::npos;
}

} // namespace

int main() {
    // --- 1. Defaults seguros: nada de esto puede cambiar el aspecto de hoy -----
    {
        LiveTimerGame game;
        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.scale_mode == "auto");
        NLP3_TEST_REQUIRE(s.canvas_width == 1920);
        NLP3_TEST_REQUIRE(s.canvas_height == 1080);
        NLP3_TEST_REQUIRE(s.frame_style == "none");
        NLP3_TEST_REQUIRE(s.frame_brackets == false);
        NLP3_TEST_REQUIRE(s.frame_grid == false);
        NLP3_TEST_REQUIRE(s.frame_scanlines == false);
        NLP3_TEST_REQUIRE(s.text_outline_px == 0);
    }

    // --- 2. La config de fabrica tambien los publica (ida y vuelta) ------------
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        NLP3_TEST_REQUIRE(cfg.get_string("scale_mode", "") == "auto");
        NLP3_TEST_REQUIRE(cfg.get_int("canvas_width", 0) == 1920);
        NLP3_TEST_REQUIRE(cfg.get_int("canvas_height", 0) == 1080);
        NLP3_TEST_REQUIRE(cfg.get_string("frame_style", "") == "none");
        NLP3_TEST_REQUIRE(cfg.get_bool("frame_brackets", true) == false);
        NLP3_TEST_REQUIRE(cfg.get_int("text_outline_px", -1) == 0);
    }

    // --- 3. Un diseño HUD completo se aplica y se lee igual -------------------
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("scale_mode", std::string("auto"));
        cfg.set("canvas_width", std::int64_t{2560});
        cfg.set("canvas_height", std::int64_t{1440});
        cfg.set("frame_style", std::string("neon"));
        cfg.set("frame_color", std::string("#00FFFF"));
        cfg.set("frame_opacity", std::int64_t{55});
        cfg.set("frame_border_px", std::int64_t{1});
        cfg.set("frame_radius_px", std::int64_t{4});
        cfg.set("frame_padding_px", std::int64_t{36});
        cfg.set("frame_brackets", true);
        cfg.set("frame_grid", true);
        cfg.set("frame_scanlines", true);
        cfg.set("text_outline_px", std::int64_t{2});
        cfg.set("text_outline_color", std::string("#001018"));
        game.apply_config(cfg);

        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.canvas_width == 2560);
        NLP3_TEST_REQUIRE(s.canvas_height == 1440);
        NLP3_TEST_REQUIRE(s.frame_style == "neon");
        NLP3_TEST_REQUIRE(s.frame_color == "#00FFFF");
        NLP3_TEST_REQUIRE(s.frame_opacity == 55);
        NLP3_TEST_REQUIRE(s.frame_padding_px == 36);
        NLP3_TEST_REQUIRE(s.frame_brackets);
        NLP3_TEST_REQUIRE(s.frame_grid);
        NLP3_TEST_REQUIRE(s.frame_scanlines);
        NLP3_TEST_REQUIRE(s.text_outline_px == 2);
        NLP3_TEST_REQUIRE(s.text_outline_color == "#001018");
    }

    // --- 4. Valores absurdos no llegan al overlay ----------------------------
    // Un lienzo de 0 dejaria la escala en division por cero; una opacidad de 500
    // pintaria un marco opaco sobre el directo. Se acotan, no se confia.
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("canvas_width", std::int64_t{0});
        cfg.set("canvas_height", std::int64_t{-100});
        cfg.set("frame_opacity", std::int64_t{500});
        cfg.set("frame_padding_px", std::int64_t{9999});
        cfg.set("text_outline_px", std::int64_t{-5});
        game.apply_config(cfg);

        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.canvas_width >= 320);
        NLP3_TEST_REQUIRE(s.canvas_height >= 240);
        NLP3_TEST_REQUIRE(s.frame_opacity == 100);
        NLP3_TEST_REQUIRE(s.frame_padding_px <= 120);
        NLP3_TEST_REQUIRE(s.text_outline_px == 0);
    }

    // --- 5. Un estilo de marco desconocido cae a "none", nunca a algo roto ----
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("frame_style", std::string("holograma-inventado"));
        game.apply_config(cfg);
        NLP3_TEST_REQUIRE(game.state().frame_style == "none");
    }

    // --- 6. El contrato: el overlay recibe las claves ------------------------
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("frame_style", std::string("neon"));
        cfg.set("frame_brackets", true);
        cfg.set("frame_scanlines", true);
        cfg.set("frame_grid", true);
        cfg.set("text_outline_px", std::int64_t{2});
        cfg.set("canvas_width", std::int64_t{1920});
        cfg.set("canvas_height", std::int64_t{1080});
        cfg.set("scale_mode", std::string("auto"));
        game.apply_config(cfg);

        const auto json = nlp3::platform::build_live_timer_state_json(&game);
        NLP3_TEST_REQUIRE(json_contains(json, "\"scale_mode\":\"auto\""));
        NLP3_TEST_REQUIRE(json_contains(json, "\"canvas_width\":1920"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"canvas_height\":1080"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_style\":\"neon\""));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_brackets\":true"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_scanlines\":true"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_grid\":true"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"text_outline_px\":2"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_color\":"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_padding_px\":"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_opacity\":"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_border_px\":"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_radius_px\":"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"text_outline_color\":"));
    }

    // --- 7. Sin juego (arranque) el JSON tambien es valido y trae defaults ----
    {
        const auto json = nlp3::platform::build_live_timer_state_json(nullptr);
        NLP3_TEST_REQUIRE(json_contains(json, "\"scale_mode\":\"auto\""));
        NLP3_TEST_REQUIRE(json_contains(json, "\"frame_style\":\"none\""));
        NLP3_TEST_REQUIRE(json_contains(json, "\"canvas_width\":1920"));
    }

    // --- 8. Ninguna de las 44 claves existentes se ha perdido ----------------
    {
        LiveTimerGame game;
        const auto json = nlp3::platform::build_live_timer_state_json(&game);
        for (const auto* key : {
                 "\"remainingSeconds\"", "\"format\"", "\"running\"", "\"paused\"",
                 "\"enabled\"", "\"completed\"", "\"sessionId\"", "\"title\"",
                 "\"subtitle\"", "\"titleStyle\"", "\"counterStyle\"", "\"subtitleStyle\"",
                 "\"popupAddColor\"", "\"popupSubtractColor\"", "\"completedText\"",
                 "\"title_effect\"", "\"counter_effect\"", "\"subtitle_effect\"",
                 "\"title_glow_enabled\"", "\"glow_color\"", "\"glow_intensity_px\"",
                 "\"pulse_speed_s\"", "\"digit_effect\"", "\"color_preset\"",
                 "\"tick_sound_path\"", "\"add_sound_path\"", "\"on_complete_sound_path\"",
                 "\"recentEvents\""}) {
            NLP3_TEST_REQUIRE(json_contains(json, key));
        }
    }

    // --- 9. Dos claves enteras que NUNCA llegaban al overlay -----------------
    // `get_double` solo lee variantes `double`, y estas dos se guardan como
    // enteros: el operador las configuraba y el overlay seguia con el default.
    // Se reproducen aqui antes del arreglo (patron Prove-It).
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("on_complete_text_size", std::int64_t{96});
        cfg.set("glow_intensity_px", std::int64_t{24});
        game.apply_config(cfg);

        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.on_complete_text_size == 96);
        NLP3_TEST_REQUIRE(s.glow_intensity_px == 24);

        const auto json = nlp3::platform::build_live_timer_state_json(&game);
        NLP3_TEST_REQUIRE(json_contains(json, "\"completedTextSize\":96"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"glow_intensity_px\":24"));
    }

    // --- 10. Y siguen acotadas si llega basura -------------------------------
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("on_complete_text_size", std::int64_t{0});
        cfg.set("glow_intensity_px", std::int64_t{999});
        game.apply_config(cfg);
        NLP3_TEST_REQUIRE(game.state().on_complete_text_size >= 8);
        NLP3_TEST_REQUIRE(game.state().glow_intensity_px <= 60);
    }

    // --- 11. Incremento 2: defaults neutros del formato, estados y medidor ----
    {
        LiveTimerGame game;
        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.time_separator == ":");
        NLP3_TEST_REQUIRE(s.show_hours == true);
        NLP3_TEST_REQUIRE(s.warn_seconds == 60);
        NLP3_TEST_REQUIRE(s.danger_seconds == 10);
        NLP3_TEST_REQUIRE(s.danger_effect == "pulse");
        NLP3_TEST_REQUIRE(s.progress_style == "none");
        NLP3_TEST_REQUIRE(s.progress_thickness_px == 6);
        NLP3_TEST_REQUIRE(s.progress_color.empty());
    }

    // --- 12. El HUD completo: separador, sin horas, umbrales propios, glitch, anillo
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("time_separator", std::string("."));
        cfg.set("show_hours", false);
        cfg.set("warn_seconds", std::int64_t{120});
        cfg.set("danger_seconds", std::int64_t{30});
        cfg.set("danger_effect", std::string("glitch"));
        cfg.set("progress_style", std::string("bar"));
        cfg.set("progress_thickness_px", std::int64_t{10});
        cfg.set("progress_color", std::string("#00FFFF"));
        game.apply_config(cfg);

        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.time_separator == ".");
        NLP3_TEST_REQUIRE(s.show_hours == false);
        NLP3_TEST_REQUIRE(s.warn_seconds == 120);
        NLP3_TEST_REQUIRE(s.danger_seconds == 30);
        NLP3_TEST_REQUIRE(s.danger_effect == "glitch");
        NLP3_TEST_REQUIRE(s.progress_style == "bar");
        NLP3_TEST_REQUIRE(s.progress_thickness_px == 10);
        NLP3_TEST_REQUIRE(s.progress_color == "#00FFFF");

        const auto json = nlp3::platform::build_live_timer_state_json(&game);
        NLP3_TEST_REQUIRE(json_contains(json, "\"time_separator\":\".\""));
        NLP3_TEST_REQUIRE(json_contains(json, "\"show_hours\":false"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"warn_seconds\":120"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"danger_seconds\":30"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"danger_effect\":\"glitch\""));
        NLP3_TEST_REQUIRE(json_contains(json, "\"progress_style\":\"bar\""));
        NLP3_TEST_REQUIRE(json_contains(json, "\"progress_thickness_px\":10"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"progress_color\":\"#00FFFF\""));
    }

    // --- 13. Separadores y estilos invalidos caen a un valor valido -----------
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("time_separator", std::string("XXX"));
        cfg.set("danger_effect", std::string("explosion"));
        cfg.set("progress_style", std::string("espiral"));
        game.apply_config(cfg);

        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.time_separator == ":");
        NLP3_TEST_REQUIRE(s.danger_effect == "pulse");
        NLP3_TEST_REQUIRE(s.progress_style == "none");
    }

    // --- 14. Los umbrales se acotan y no pueden cruzarse ---------------------
    // Si peligro quedase por encima de aviso, el overlay nunca pintaria el aviso.
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("warn_seconds", std::int64_t{30});
        cfg.set("danger_seconds", std::int64_t{90});
        cfg.set("progress_thickness_px", std::int64_t{9999});
        game.apply_config(cfg);
        NLP3_TEST_REQUIRE(game.state().danger_seconds <= game.state().warn_seconds);
        NLP3_TEST_REQUIRE(game.state().progress_thickness_px <= 40);

        LiveTimerGame other;
        auto cfg2 = other.default_config();
        cfg2.set("warn_seconds", std::int64_t{-5});
        cfg2.set("progress_thickness_px", std::int64_t{0});
        other.apply_config(cfg2);
        NLP3_TEST_REQUIRE(other.state().warn_seconds >= 0);
        NLP3_TEST_REQUIRE(other.state().progress_thickness_px >= 1);
    }

    // --- 15. Los efectos nuevos del incremento 2 son aceptados ---------------
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("title_effect", std::string("heartbeat"));
        cfg.set("counter_effect", std::string("flicker"));
        cfg.set("subtitle_effect", std::string("float"));
        cfg.set("digit_effect", std::string("odometer"));
        game.apply_config(cfg);
        NLP3_TEST_REQUIRE(game.state().title_effect == "heartbeat");
        NLP3_TEST_REQUIRE(game.state().counter_effect == "flicker");
        NLP3_TEST_REQUIRE(game.state().subtitle_effect == "float");
        NLP3_TEST_REQUIRE(game.state().digit_effect == "odometer");

        LiveTimerGame other;
        auto cfg2 = other.default_config();
        cfg2.set("counter_effect", std::string("teleport"));
        cfg2.set("digit_effect", std::string("explode"));
        other.apply_config(cfg2);
        NLP3_TEST_REQUIRE(other.state().counter_effect == "none");
        NLP3_TEST_REQUIRE(other.state().digit_effect == "none");
    }

    // --- 16. Incremento 3: particulas apagadas por defecto y coherentes -------
    // Son decoracion: con la config de fabrica no puede haber ni una particula,
    // porque el overlay se compone sobre video en vivo.
    {
        LiveTimerGame game;
        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.particles_enabled == false);
        NLP3_TEST_REQUIRE(s.particles_style == "none");
        NLP3_TEST_REQUIRE(s.particles_budget == 120);
        NLP3_TEST_REQUIRE(s.particles_density == 1.0);
        NLP3_TEST_REQUIRE(s.particles_force == false);
    }

    // --- 17. Un juego de particulas completo se aplica y viaja al overlay -----
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("particles_enabled", true);
        cfg.set("particles_style", std::string("sparks"));
        cfg.set("particles_budget", std::int64_t{80});
        cfg.set("particles_density", 1.5);
        cfg.set("particles_force", true);
        game.apply_config(cfg);

        const auto& s = game.state();
        NLP3_TEST_REQUIRE(s.particles_enabled);
        NLP3_TEST_REQUIRE(s.particles_style == "sparks");
        NLP3_TEST_REQUIRE(s.particles_budget == 80);
        NLP3_TEST_REQUIRE(s.particles_density == 1.5);
        NLP3_TEST_REQUIRE(s.particles_force);

        const auto json = nlp3::platform::build_live_timer_state_json(&game);
        NLP3_TEST_REQUIRE(json_contains(json, "\"particles_enabled\":true"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"particles_style\":\"sparks\""));
        NLP3_TEST_REQUIRE(json_contains(json, "\"particles_budget\":80"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"particles_density\":1.5"));
        NLP3_TEST_REQUIRE(json_contains(json, "\"particles_force\":true"));
    }

    // --- 18. Presupuesto y densidad acotados; estilo invalido -> none ---------
    {
        LiveTimerGame game;
        auto cfg = game.default_config();
        cfg.set("particles_style", std::string("fuegos-artificiales"));
        cfg.set("particles_budget", std::int64_t{99999});
        cfg.set("particles_density", 50.0);
        cfg.set("particles_enabled", true);
        game.apply_config(cfg);
        NLP3_TEST_REQUIRE(game.state().particles_style == "none");
        NLP3_TEST_REQUIRE(game.state().particles_budget == 300);
        NLP3_TEST_REQUIRE(game.state().particles_density == 2.0);
        // Sin estilo no hay nada que emitir: activado se queda en falso.
        NLP3_TEST_REQUIRE(game.state().particles_enabled == false);

        LiveTimerGame other;
        auto cfg2 = other.default_config();
        cfg2.set("particles_style", std::string("confetti"));
        cfg2.set("particles_budget", std::int64_t{1});
        cfg2.set("particles_density", 0.0);
        other.apply_config(cfg2);
        NLP3_TEST_REQUIRE(other.state().particles_budget >= 10);
        NLP3_TEST_REQUIRE(other.state().particles_density >= 0.25);
    }

    // --- 19. Popup móvil: viewport y piso físico de font ---------------------
    // Reproduce el reporte (patrón Prove-It): sin viewport el browser hace doble
    // zoom en móvil; y el font del popup (techo de lienzo) cae a ~10px físicos
    // con k≈0.2. La lane lateral (F2) se retiró en F5: el popup vive en el slot
    // del subtítulo y no se sienta a un costado del reloj.
    {
        const std::string html(nlp3::platform::panel_overlay_live_timer_html());
        // F1: sin este meta, el overlay se escalaba dos veces en pantallas táctiles.
        NLP3_TEST_REQUIRE(html.find("<meta name=\"viewport\"") != std::string::npos);
        // F5: la lane lateral derecha/izquierda ya no existe.
        NLP3_TEST_REQUIRE(html.find("function layoutEventLane()") == std::string::npos);
        // F3: piso físico (~14px reales) y techo ampliado a 96px de lienzo.
        NLP3_TEST_REQUIRE(html.find("physMin") != std::string::npos);
        NLP3_TEST_REQUIRE(html.find("Math.min(96") != std::string::npos);
        // F3: en pantallas angostas el texto del popup envuelve en 2 líneas.
        NLP3_TEST_REQUIRE(html.find("@media (max-width: 600px)") != std::string::npos);
    }

    // --- 20. El popup es el gancho de donación: nombre + tiempo, sin iconos,
    //         grande y sobresaliente (pastilla con glow y pop de entrada) ------
    {
        const std::string html(nlp3::platform::panel_overlay_live_timer_html());
        // Formato "nombre y tiempo": la composición vieja (icon + label + delta)
        // ya no existe en el JS del overlay.
        NLP3_TEST_REQUIRE(html.find("labelText") == std::string::npos);
        // El icono del evento NUNCA se concatena al texto visible.
        NLP3_TEST_REQUIRE(html.find("parts.push(icon)") == std::string::npos);
        // Grande: 60% del contador por defecto (72px con contador de 120).
        NLP3_TEST_REQUIRE(html.find("0.26 : 0.6") != std::string::npos);
        // Sobresaliente: pastilla oscura con glow del color del evento.
        NLP3_TEST_REQUIRE(html.find("background: rgba(0, 0, 0, 0.65)") != std::string::npos);
        NLP3_TEST_REQUIRE(html.find("box-shadow: 0 0 16px currentColor") != std::string::npos);
        // Entrada con pop de escala (no solo slide lateral).
        NLP3_TEST_REQUIRE(html.find("scale(0.72)") != std::string::npos);
    }

    // --- 21. F5 — el popup reemplaza la frase del subtítulo (Opción D) --------
    // Sin lane a un costado: mientras dura el evento, "Cada coin suma Xs" se
    // sustituye por "Nombre +Ns" en grande con pastilla; al terminar vuelve la
    // frase original. El polling del overlay no pisa el popup mientras está activo.
    {
        const std::string html(nlp3::platform::panel_overlay_live_timer_html());
        // El popup se aplica sobre #subtitle, no sobre una lane lateral.
        NLP3_TEST_REQUIRE(html.find("subtitle-popup") != std::string::npos);
        // Guardia de polling: la frase no se reescribe mientras el popup vive.
        NLP3_TEST_REQUIRE(html.find("subtitlePopupActive") != std::string::npos);
        // Al expirar se restaura el texto original del subtítulo.
        NLP3_TEST_REQUIRE(html.find("restoreSubtitlePopup") != std::string::npos);
        // El contenedor lateral de eventos ya no recibe popups.
        NLP3_TEST_REQUIRE(html.find("appendChild(el)") == std::string::npos ||
                          html.find("getElementById('event-container')") == std::string::npos);
        // Opción B: nombre extenso → máx. 2 líneas con "…" (el recuadro no
        // desborda hacia la barra de progreso ni se sale del marco).
        NLP3_TEST_REQUIRE(html.find("-webkit-line-clamp: 2") != std::string::npos);
        NLP3_TEST_REQUIRE(html.find("line-clamp: 2") != std::string::npos);
    }

    return 0;
}
