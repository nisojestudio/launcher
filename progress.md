Original prompt: perfecto ahora necesito que en el juego subas al espacio que esta vacio junto al engranaje, el record de la semana el recor del dia y el top 3, crea tarjetas que se vean bien se muestren grande y claro los numeros, el avatar y el nombre, el nombre se debe recortar si no cae completo. en el area del juego solo debe quedar el temporizador y en el tts una instruccion, cuando lea el chat no debe leer emoticones, iconos caracteres raros solo texto.

- 2026-04-08: revisados `README.md`, `docs/WORKING_CONTRACT.md` y `docs/ARCHITECTURE_START.md` antes de tocar codigo.
- 2026-04-08: localizado HUD real de Arena Live en `%USERPROFILE%\Desktop\Juegos\Arena Live\build\Release\web\local\arena_live\js\core\render\arena_renderer.js`.
- 2026-04-08: confirmado que `getArenaRect()` deja una franja superior libre fuera de la arena; el HUD actual podia mover records/top3 al header superior.
- 2026-04-08: `arena_renderer.js` ajustado para mover record semanal, record del dia y top 3 al header superior libre junto al engranaje; el temporizador queda como unico HUD dentro de la arena.
- 2026-04-08: cards del HUD ampliadas con avatar visible, tipografia mas grande y truncado real de nombre en top 3 y records.
- 2026-04-08: validacion real del HUD hecha con `node --check` y capturas Playwright en `output/web-game-arena-hud/shot-0.png`, `output/web-game-arena-hud/full-page.png` y `output/web-game-arena-hud/full-page-longnames.png`.
- 2026-04-08: localizado filtro TTS actual en `src/tts/tts_scheduler.cpp`; se reemplazo la limpieza basica por sanitizacion UTF-8 que elimina emoticones, iconos y caracteres raros, preservando texto legible con acentos y `ñ`.
- 2026-04-08: agregado test dedicado en `tests/tts_scheduler_test.cpp` para probar actor, texto sanitizado y rechazo de mensajes solo con emoji.
- 2026-04-08: validacion real de TTS hecha con `ctest --preset release -R "nlp3_tts_scheduler_test|nlp3_tts_template_formatter_test" --output-on-failure`.
- 2026-04-08: `src/platform/ui/index.html` y `tests/panel_http_ui_test.cpp` alineados con la instruccion visual de TTS y con el tema oscuro real del host.
- 2026-04-08: detectado que los `.inc` del panel UI estaban regenerados pero `NisojeStudio.exe` seguia sirviendo una version vieja hasta relinkear `nlp3_app`; se forzo recompilacion limpia del bloque embebido.
- 2026-04-08: validacion real del host UI hecha con smoke HTTP directo contra `NisojeStudio.exe` en puerto 19100 y con `ctest --preset release -R "nlp3_panel_http_ui_test|nlp3_panel_app_smoke_test|nlp3_tts_scheduler_test|nlp3_tts_template_formatter_test" --output-on-failure`.
- 2026-04-23: agregadas portadas reales de `Arena Live`, `Super Chat` y `Conquista` al panel embebido mediante `src/platform/ui/game-previews.js` servido desde el host HTTP.
- 2026-04-23: tarjetas de juegos rediseñadas con poster vertical 3:4 y descripciones no tecnicas para que la imagen se vea completa y el panel sea mas claro a simple vista.
