# Changelog

All notable Panel Live changes should be recorded here.

Format follows a lightweight Keep a Changelog style. Versions use SemVer.

## 0.2.24 - 2026-07-12

### Fixed

- **Reloj local del overlay acelerado por bug en tickLocalClock**: `tickLocalClock()` no actualizaba `localLastSyncMs` después de cada frame, causando que `elapsedMs` acumulara el tiempo total desde el inicio en vez del delta entre frames. El contador descendía ~30x más rápido y cada resync (cada 3s) lo hacía "saltar hacia atrás". Corregido agregando `localLastSyncMs = timestamp` al final del tick.

### Added

- **Self-test mode (`?test=1`)**: Verifica automáticamente que el reloj local descuente exactamente 1s por cada segundo real, sin deriva. Corre 60+ frames de `requestAnimationFrame` y reporta PASS/FAIL con métricas.

## 0.2.23 - 2026-07-12

### Fixed

- **Overlay timer se congelaba durante streaming**: El mecanismo de polling del overlay (`live-timer.html`) llamaba `schedulePoll()` sincrónicamente antes de que el `fetch` completara. Si el servidor tardaba >500ms en responder, cada petición era abortada por la siguiente, creando un congelamiento permanente del contador. Corregido moviendo `schedulePoll()` al `.finally()` de la promesa.

- **Contador sin fluidez sub-segundo**: El overlay dependía 100% del polling HTTP para actualizar el display. Cualquier latencia de red se traducía en saltos visuales. Agregado reloj local con `requestAnimationFrame` que descuenta suavemente (~60fps) desacoplado de la red. El servidor solo se consulta cada ~3s para resincronizar.

### Changed

- **Poll interval relajado a 3s cuando el reloj local está activo**: Reduce carga del servidor sin afectar la fluidez visual.
- **Backoff adaptativo diferenciado**: Cuando el reloj local corre, los errores de red no penalizan el intervalo de polling (máximo 5s con factor 1.5x en vez de 2x).

### Verified

- Overlay timer fluido segundo a segundo con red desconectada (reloj local mantiene el tic).
- Resincronización correcta al recuperar conexión.
- Sonidos tick/add/completion usan tiempo local preciso.
- Estados paused/completed/disabled detienen el reloj local correctamente.

## 0.2.22 - 2026-07-12

### Fixed

- **TikTok bridge runner terminaba a los 2 s sin abrir sockets 8766/8770**: `event_dispatcher.AsyncEventDispatcher.emit_status()` no protegía `panel_ws_sink.send_json()` ni `broadcast_callback()` con `try/except`. Cuando el `panel_ws_url` apuntara a un puerto sin listener (típico al arrancar antes que el panel), cualquier `WSAECONNREFUSED` propagaba hasta `_publish_status`, matando el `run_connection` task completo. Ahora ambos pathways están envueltos con `try/except` y registran `panel_ws_send_failures_total` / `broadcast_send_failures_total`. End-to-end validado: `externalBridge.connectionState: connected` con `acceptedMessages: 1695+` sobre puerto 8765 exclusivo.

- **`tiktok_connection.close()` no atrapaba `CancelledError`**: cuando `TikTokLiveClient.disconnect()` cancelaba su `event_loop_task`, el `CancelledError` saltaba `except Exception` (que no captura CancelledError en Python 3.14) y mataba el runner con traceback antes de permitir al `panel_ws_sink` escribir el JSON del report forense. Capturado aparte.

- **`test_tiktok_connection.py` perdía el JSON report al crashear**: el probe forense escribía el report DESPUÉS de que `run_probe()` retornara, así que cualquier excepción antes del write dejaba el disco sin report. Ahora el `main()` usa `try/finally` para emitir el report siempre, y el `finally` interno usa `except BaseException` para no romper el cleanup.

- **`connection_manager.py` exponía `_stop_requested` o `CancelledError`**: múltiples paths del loop interno llamaban `await connection.close()` sin guard. Cada `close()` ahora blindado con `try/except BaseException`. El `wait_task` también se cancela explícitamente en un sub-`finally` para evitar tasks colgadas.

- **`run_tiktok_bridge.py` no reportaba por qué el runner terminó sin conectar**: añadido `log_json(warning)` explícito cuando `exit_code == 0` y `connection_state != "connected"` para que el panel pueda diagnosticar.

- **`tiktok_external_ws_server.cpp` endurece el puerto 8765 exclusivo**: `EXCLUSIVE_BRIDGE_PORT` ahora se parametriza con `kDefault/kMin/kMax` (rango 8765-8765) y configura `SO_EXCLUSIVEADDRUSE` por defecto, evitando que un zombie TCP de un cierre abrupto robe el port.

### Build & Workflow

- `clear_ports.bat` y `clear_port8765.bat`: breakers que matan procesos sobre 8765+8766+8770 antes de un arranque limpio. `clear_port8765.bat` ahora delega a `clear_ports.bat`.
- Repositorio reconstruido: `cloudflare_tunnel_service.cpp/.hpp` integra watchdog/auto-restart removido y graceful shutdown; `port_zombie_detector.cpp/.hpp` agregado al CMakeLists; `bin/NisojeStudio.exe` re-enlazado con build C++ obsoleto de `EXCLUSIVE_BRIDGE_PORT` y/o `cloudflare_tunnel_service.cpp`.
- `tools/cloudflared/cloudflared.exe` (54 MB) auto-ignorado por `.gitignore`. Se descarga vía `ensure_cloudflared_downloaded()` desde GitHub Releases; se corrigió usando el binario en `build/installer_cache/`.

### Verified

- Bridge TikTok: `externalBridge.connectionState=connected`, `runnerLastExitCode=0`, `externalWs.acceptedMessages=1695` con `target=senpaii.fb`.
- Tunnel Cloudflare: URL `https://bee-editors-update-harper.trycloudflare.com/overlay/live-timer` responde HTTP 200 con `<!doctype html><html lang="es"><title>Live Timer</title>` y `/api/state` proxied vía tunnel retorna `panelName: Nisoje Studio`.
- `unittest discover -s tools/bridge_py/tests` → 38 tests OK, exit 0.

## 0.2.21 - 2026-07-11

### Fixed

- Various fixes and improvements from recent commits

## 0.2.20 - 2026-07-10

### Fixed

- **TikTok WebSocket bridge — conexión bloqueada por zombie TCP**: El commit `af19c2b` cambió `SO_REUSEADDR` a `SO_EXCLUSIVEADDRUSE` con limpieza vía `SetTcpEntry(DELETE_TCB)`, pero `SetTcpEntry` requiere admin y falla silenciosamente. Cuando el panel se cierra abruptamente, queda una entrada zombie LISTENING en puerto 8765 que bloquea el bind. **Revertido** a `SO_REUSEADDR` y eliminada la función `try_cleanup_stale_port_listeners()` y su dependencia de `iphlpapi`.

- **Cloudflare tunnel — TerminateProcess reemplazado por graceful shutdown**: `restart_tunnel()` y `stop_tunnel()` ahora cierran el pipe stdout primero, esperan 3s a que el proceso termine solo, y solo llaman a `TerminateProcess` si sigue vivo. Esto evita zombies del tunnel.

## 0.2.19 - 2026-07-09

### Fixed

- **Timer overlay — `clampVolume` límite incorrecto**: El overlay limitaba el volumen a 1.0, pero el backend acepta hasta 2.0. Valores entre 1.0 y 2.0 se silenciaban sin aviso. Ahora `clampVolume()` admite hasta 2.0, consistente con el backend.

- **Timer panel — `||` falsy reemplazaba 0 por default**: `parseFloat("0") || 2.0` evaluaba a `2.0`, haciendo imposible configurar `time_per_*` en 0 desde el panel. Añadida función `parseTimerNum()` que preserva 0 como valor válido. También cambiados `title_text` y `subtitle_text` de `||` a `??` para permitir strings vacíos.

- **Timer overlay — preset de color sobrescribía estados danger/warning**: `applyColorPreset()` seteaba `counter.style.color` como inline DESPUÉS de `setCounterState()`, anulando los colores de estado (danger/warning/completed). Eliminado el inline color — ahora se usan exclusivamente CSS classes + variables (`#counter.danger { color: var(--danger-color) }`).

- **Timer overlay — `lastDigits` no sincronizado en disabled/completed**: Al salir de disabled o completed, `renderDigits()` comparaba contra `lastDigits` stale, disparando animaciones no deseadas en todos los dígitos. Ahora `lastDigits[]` se sincroniza inmediatamente después de escribir los guiones (`-`) o ceros (`000000`).

- **Timer overlay — validación de glow color débil**: La regex `/^(#|rgb|hsl|[a-z])/` aceptaba colores inválidos como `#GGGGGG`, causando texto invisible en OBS. Ahora valida estrictamente `#RGB/#RRGGBB`, `rgb()`, `hsl()`, o nombres CSS.

- **Timer overlay — null JSON faltaban campos**: `build_live_timer_state_json` con `game == nullptr` no incluía `popupAddColor` ni `popupSubtractColor`. Añadidos con valores default.

- **Timer overlay — stale response race condition**: El polling usaba `abortController.abort()` que no protege contra callbacks de fetch ya completados. Añadido `pollGeneration` counter — respuestas stale se descartan.

- **Timer overlay — overflow en textos**: Title, subtitle, days-label y counter sin protección de desbordamiento. Añadidos `overflow: hidden; text-overflow: ellipsis; max-width: 90vw`.

- **Timer overlay — accesibilidad**: Faltaban `aria-live="polite"` en `#days-label` y `#subtitle`. Añadidos.

### Removed

- **Timer C++ — `resolve_actor_name` eliminado**: Función definida pero nunca llamada dentro del módulo timer. Eliminada.

- **Timer test — `test_v3_counter_font_validation` eliminado**: El campo `counter_font` fue fusionado a `counter_style.font_family`. El test referenciaba `state().counter_font` que ya no existe. Eliminado junto con referencias en `test_v3_fields_round_trip`.

## 0.2.18 - 2026-07-09

### Fixed

- **Timer overlay — CSP bloqueaba scripts inline**: `Content-Security-Policy` no incluía `'unsafe-inline'` para `script-src`. Añadido.

## 0.2.17 - 2026-07-09

### Fixed

- **Timer V3 — digital effects, color presets, and fonts were non-functional**: The three V3 UI controls (digit effect, color palette, counter font) appeared in the panel HTML but their JavaScript DOM bindings were never created. The `els` object was missing `timerDigitEffect`, `timerColorPreset`, and `timerCounterFont`. Any user selection was silently lost and defaults were always sent. **Fixed** by adding the missing bindings, wiring them into `hotControls` for auto-save, including them in `sendTimerConfigHot()`, and adding crash guards to the config import function.

- **Timer — ghost features removed**: Wave, Shake, and Particles controls existed in the panel UI but had zero backend or overlay implementation. Completely removed from HTML and JavaScript (~50 lines). The "Examinar" file browser for sound paths was also broken (browsers only return filename, not full path) — replaced with a manual text input with improved placeholder.

- **Timer — `timerAllowNegatives` checkbox was misleading**: The checkbox only controlled the HTML `min` attribute locally. The backend always accepted negative values (±3600s). Removed the checkbox; all time-per-* inputs now have `min="-10"` permanently, matching the backend's actual behavior.

- **Timer — presets used invalid effects**: The "Energy" preset sent `shake` and "Rainbow" preset sent `wave` — effects that don't exist in the backend validation whitelist. Fixed to use `pulse` instead.

- **Timer — `counter_font_family` default mismatch**: The C++ default was `"Segoe UI, monospace"` but the HTML select default was `"Segoe UI, sans-serif"`. First config apply would silently change the counter font. Fixed.

- **Timer — `onPayloadUpdate` dead code removed**: The function was assigned but never called anywhere. Removed.

- **Timer — `bgColor` removed from overlay JSON**: The field was always `"transparent"` and never consumed by the overlay. Removed from both null and real state JSON to save bandwidth.

- **Timer — `on_complete_video_url` purged**: The field was stored in C++ state and exported via HTTP but had no UI, no overlay playback, and was never serialized to the overlay JSON. Completely removed from header, implementation, and HTTP server.

### Added

- **Timer — V3 field server-side validation**: `color_preset` and `counter_font` now have the same whitelist validation as `digit_effect` in `apply_config()`. Invalid values are normalized to safe defaults with the correction recorded in config warnings.

- **Timer — CSP header on overlay**: Added `Content-Security-Policy` meta tag to the overlay HTML, restricting resources to self and Google Fonts origins.

- **Timer — string length limits**: `title_text` (256 chars), `subtitle_text` (512 chars), and `on_complete_text` (128 chars) now have max length enforcement at the HTTP layer.

- **Timer — V3 contract documentation**: Section 10 added to `timer_module_contract.md` documenting `digit_effect`, `color_preset`, and `counter_font` with allowed values, defaults, and behavior.

- **Timer — V3 unit tests**: 4 new test functions (16 assertions) covering digit effect validation, color preset validation, counter font validation, and full V3 round-trip through JSON serialization.

### Security

- **Timer overlay CSP**: `default-src 'self' https://fonts.googleapis.com https://fonts.gstatic.com; style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; media-src 'self' blob:;`
- **Input length limits**: Prevents unbounded string storage for title (256), subtitle (512), and completion text (128).

## 0.2.16 - 2026-07-09

### Fixed

- **Timer overlay — IDs duplicados**: El refactor visual V3 agregó nuevos elementos HTML sin eliminar los antiguos. El overlay mostraba dos contadores superpuestos. Eliminados elementos duplicados y unificada la estructura de digitos.
- **Timer overlay — `textContent` destruia estructura de digitos**: Al deshabilitar el timer, `counterEl.textContent = '--:--:--'` eliminaba los `<span class="digit">`. Al reactivar, `renderDigits` no encontraba elementos y el contador quedaba congelado. Ahora escribe `-` en cada digito individual.
- **Timer overlay — conflicto `counter_font` vs `counterStyle.font_family`**: `applyCounterFont` ahora se ejecuta siempre despues de `applyStyles` para que la fuente del preset tenga prioridad.
- **Timer overlay — `renderDigits` no soportaba formato con dias**: Strings mas largas que los 6 digitos disponibles (ej. tiempos >24h) ahora usan slice para tomar solo la parte HHMMSS.
- **Timer overlay — `effectConfigChanged` falsos positivos**: Campos ausentes en el JSON (undefined) se comparaban con valores default causando re-render innecesario. Agregados defaults con `||`.
- **Timer overlay — `glowColor` invalido causaba texto invisible en OBS**: Agregada validacion basica de color CSS antes de aplicar `drop-shadow`. Si el color es invalido, se mantiene el text-shadow default.
- **Timer overlay — `triggerCounterBump` rompia transiciones CSS**: El bump guarda y restaura `el.style.transition` para no pisar permanentemente otras transiciones.
- **Timer C++ — `apply_visual_style` con `get_string` inseguro**: Cambiado a `std::get_if<std::string>` para evitar `bad_variant_access` si el valor no es string.
- **Timer — codigo muerto eliminado**: Funciones de particulas, confetti, wave y shake (~100 lineas) eliminadas del overlay. CSS de efectos obsoletos limpiado.

### Changed

- **Timer overlay — polling robusto**: `AbortController` para cancelar requests previos, backoff adaptativo con recuperacion gradual, `AbortError` ignorado.
- **Timer overlay — presets de color funcionales**: `applyColorPreset` ahora aplica color inline al contador y `setCounterState` usa colores del preset para danger/warning/completed.
- **Timer overlay — `styleEqual` y `applyStyles` genericos**: Usan `Object.keys` para no romperse si el struct `LiveTimerVisualStyle` crece.

## 0.2.15 - 2026-07-08

### Added

- **Timer — refactor visual V3**: Efectos de digito individual (flip/roll/pop/fade), 4 paletas de color predefinidas, selectores de tipografia mono (Space Mono, JetBrains Mono, Share Tech Mono).
- **Timer — `live_timer_save.json` en `%TEMP%/NisojeStudio/`**: La persistencia ya no se cuela en el directorio de releases.

### Fixed

- **Firebase**: `FIREBASE_API_KEY` agregado a variables del worker.
- **Licencias**: `activation_email_sent_at` rastreado en licencia + `email_sent_count` en dashboard.

## 0.2.14 - 2026-07-08

### Fixed

- **STATUS_STACK_BUFFER_OVERRUN (0xC0000409) en Release**: `/Ob2` (inline agresivo) combinado con `/GS` causaba falso positivo del stack guard en `PanelApp::initialize()` y `CloudflareTunnelService`. Cambio a `/Ob1` en `CMAKE_CXX_FLAGS_RELEASE`. Mantiene `/O2` para velocidad.
- **Túneles cloudflared zombies**: Limpieza de procesos `cloudflared.exe` residuales en los launchers `.bat` y `.ps1`.

### Added

- **Script externo de túnel**: `scripts/start_cloudflared_tunnel.ps1` como alternativa para gestionar el túnel cloudflared como proceso independiente.

## 0.2.13 - 2026-07-06

### Fixed

- **T1.1f — Timer SSOT actualizado en cada tick()**: `tick()` ahora actualiza `state_.remaining_seconds` Y resetea `start_time_` en CADA llamado, no solo al expirar. Antes `tick()` era no-op cuando `current > 0.0` y `remaining_seconds()` computaba todo dinámicamente — mismo resultado matemático pero convergencia asegurada entre SSOT y getter dinámico.
- **Missing `#include <mutex>`**: `cloudflare_tunnel_service.hpp` usaba `std::mutex` sin incluir `<mutex>`, causando error de compilación C2039.

### Added

- **Watchdog de Cloudflare Tunnel**: Hilo watchdog que monitorea el proceso `cloudflared` cada 12 segundos + health check HTTP al endpoint `/health`. Reinicio automático si el proceso muere o el health check falla.

## 0.2.11 - 2026-07-05

### Added

- **Auto-descarga de cloudflared.exe**: El `CloudflareTunnelService` ahora descarga automáticamente `cloudflared.exe` desde GitHub Releases si no está presente en `tools/cloudflared/`. Esto evita que el túnel de Cloudflare se pierda después de operaciones de limpieza (`git clean -fdx`) o reinstalaciones. Usa WinInet sin dependencias externas. El instalador ya lo incluye en `tools/cloudflared/`.

### Fixed

- **CRITICAL — Efectos visuales nunca se aplicaban**: Bug en `LiveTimerGame::apply_config()` donde las 14 propiedades de efectos visuales (`title_effect`, `counter_effect`, `subtitle_effect`, glow, wave, pulse, shake, partículas) se escribían en `effective` DESPUÉS de `config_ = std::move(effective)`, perdiendo todos los valores. Como resultado, `state_.*_effect` siempre quedaba en `"none"` y `state_.*_glow_enabled` / `particles_enabled` siempre en `false`. Ahora los efectos se aplican antes del move y el estado los recibe correctamente.

## 0.2.12 - 2026-07-06

### Added

- **A11 — Save atómico con backup**: `panel_app.cpp` ahora escribe el estado del timer a un archivo temporal y lo renombra atómicamente. Si el archivo principal se corrompe, `load_timer_state` hace fallback al `.bak`. Esto previene pérdida de estado por crash mid-write.
- **A12 — NaN/inf guard en SSOT**: `remaining_seconds()`, `adjust_time()` y `apply_config()` rechazan silenciosamente valores NaN o infinito, evitando que el estado interno se corrompa.
- **Validación cliente en Apply**: El formulario de configuración del timer valúa tipos y rangos en el cliente antes de enviar al backend. El botón Apply se deshabilita durante el request para evitar doble envío.
- **Import config auto-aplica**: Al importar una configuración JSON, el formulario se llena y se aplica automáticamente al backend.
- **4 tests de regresión**: delta clamp (A7), skip popup en timer exhausto (A8), sanitize NaN/inf (A12), round-trip JSON save/load.

### Fixed

- **A1 — CSS !important eliminado**: Las clases `.warning`, `.danger` y `.completed` ya no usan `!important` en el color, permitiendo que el color personalizado del counter se respete siempre.
- **A4 — Cleanup de particleTimeouts**: Los `setTimeout` del burst inicial de partículas se trackean en un array y se limpian en `beforeunload`, evitando fugas de memoria y errores tras recarga del overlay.
- **A7 — adjust_time reporta delta real**: El popup ahora muestra el delta realmente aplicado (clampeado por `max_time_s`), no el delta raw solicitado.
- **A8 — Skip popup en timer exhausto**: Cuando un evento de game input agota el timer (lo lleva a 0 o negativo), se suprime el popup para evitar que el overlay muestre "-Xs" junto con el confetti de completion.
- **A10 — pollTimerEvents recursivo**: El polling de eventos del timer cambió de `setInterval` a un chain recursivo con `setTimeout` y cleanup en `beforeunload`, eliminando fugas de intervalos huérfanos.
- **A13 — Label popup consistente**: El label del evento (like, share, follow, gift) ahora se muestra siempre en el popup, no solo cuando `|delta| == 1`.
- **A6 — int64_t namespaced**: Unificado `int64_t` → `std::int64_t` en `live_timer_game.hpp`.

### Changed

- **Accesibilidad del overlay**: El counter ahora tiene `aria-live="polite"` y `role="region"` para lectores de pantalla. Los popups de eventos tienen `aria-label`. El banner de completed usa `role="status"` con `aria-live="assertive"`. Los contenedores decorativos (partículas, confetti) tienen `aria-hidden="true"`.
- **Refactor Apply handler**: Extraído `readTimerConfigFromForm()` como helper reutilizable desde Apply e Import.

## 0.2.9 - 2026-07-04

### Added

- **Efectos visuales en caliente**: Todos los cambios de efectos, colores y fuentes se envían automáticamente al servidor con debounce de 350ms, sin necesidad de presionar "Aplicar config". La preview del overlay se refresca automáticamente después de cada cambio. *(Nota: el frontend y el overlay estaban correctos, pero el backend C++ perdía los valores — corregido en 0.2.10)*
- **Presets "Temas rápidos" ahora aplican inmediatamente**: Al hacer clic en Elegante, Energía, Arcoíris o Minimal, los cambios se envían al servidor y se reflejan en la preview al instante. *(Requiere fix de 0.2.10)*

### Fixed

- **Keyboard shortcuts no funcionaban con selects/campos numéricos enfocados**: `isEditableFocused()` ahora solo bloquea shortcuts cuando se está escribiendo texto real, no cuando el foco está en dropdowns de efectos, color pickers o inputs numéricos. Las teclas R, +, -, Space y V funcionan correctamente desde cualquier control del timer.
- **Tecla + requiere Shift**: Se agregó `=` como alias de `+` para teclados donde `+` requiere Shift.
- **Preview del overlay no se actualizaba al cambiar efectos**: Ahora la preview se refresca automáticamente 200ms después de cada cambio visual, sin necesidad de abrir/cerrar el panel de preview.

## 0.2.8 - 2026-07-04

### Added

- **Config layout redesigned**: 5 collapsible sections (Tiempos, Visual, Efectos, Sonidos, Avisos) with clean card-per-element layout. Each card groups Tamaño/Color/Fuente/Negrita + Efecto/+Resplandor in compact inline rows.
- **Preview background**: Changed from pure black to dark navy (`#1a1a2e`) for a more pleasant preview look.
- **Preview scales down**: When `?preview=1` is detected, overlay uses smaller fonts (counter 52px, title 22px) to fit the 240px panel iframe.

### Fixed

- **Overlay preview showing black screen**: Query string (`?preview=1&t=...`) was not stripped from the HTTP request path, causing all overlay routes to return 404. The C++ HTTP parser now correctly strips query params before route matching.
- **Timer preview iframe height**: Increased from 200px to 240px (180px on small screens) for better preview visibility.

## 0.2.7 - 2026-07-04

### Added

- **Overlay preview in panel**: New collapsible iframe showing the overlay live. Opens with `V` key or clicking "Vista previa del overlay". Auto-refreshes when config is applied.
- **Confetti celebration**: When timer reaches 0, colorful confetti rains down. 60 particles in 9 colors, auto-cleanup after 6s.
- **Keyboard shortcuts**: `Space` (pause/resume), `R` (reset), `+`/`-` (adjust time), `V` (preview toggle). Only work when no text field is focused — safe to type normally.
- **Effect parameter presets**: Wave palette picker (Arcoíris, Neón, Fuego, etc.), pulse speed (Lento/Normal/Rápido), glow intensity (Sutil/Medio/Fuerte/Intenso), particle count (Pocas/Normales/Muchas/Lluvia). No more raw hex codes or technical units.
- **"Temas rápidos" preset buttons**: Elegante (pulse+brillo), Energía (temblor+partículas), Arcoíris (wave), Minimal (sin efectos).
- **Effect key validation in C++**: Invalid effect names fall back to "none" instead of being silently stored.

### Fixed

- **Wave animation flickering**: `updateEffects()` now compares effect state before re-applying. The CSS wave animation no longer restarts every 500ms.
- **Particle system stutter**: Particles now spawn continuously from bottom and self-clean instead of mass-respawning every 8s with `innerHTML = ''`.
- **Wave + Danger color conflict**: When timer enters danger/warning, wave gradient is suspended and the danger color is shown instead.
- **Effect classes overwritten by state**: `className = 'danger'` no longer removes `effect-wave`/`effect-pulse` classes. Uses `classList.add/remove`.

## 0.2.6 - 2026-07-04

### Added

- **Config export/import (B7)**: New Export/Import buttons in timer config panel. Export copies JSON config to clipboard, Import pastes and fills all fields.
- **Overlay subtitle preview (A9)**: Overlay now shows a "PREVIEW" badge and dimmed subtitle when timer is idle, so the operator can see how it looks in OBS while configuring.
- **Event sounds (A4)**: New "Sonidos" section in timer config. Configure `.wav` for start/tick sounds (plays every second in last 60s) and add-time sounds (plays when timer receives extra time from events).

### Fixed

- **Config keys missing in server**: `popup_add_color`, `popup_subtract_color`, `on_complete_text/color/size` were not being parsed in POST `/api/timer/configure` nor returned in GET `/api/timer/config`. Now properly handled.

## 0.2.5 - 2026-07-04

### Added

- **Timer presets**: 4 quick-config buttons (Rápido 60s, Maratón 1h, Punitivo, Solo regalo) in timer config panel
- **mm:ss input**: Timer initial time now accepts "mm:ss" or "hh:mm:ss" format beside plain seconds
- **Subtitle preview**: Live preview of subtitle text with resolved placeholders in config panel
- **Completion banner customization**: Text, color, and font size of the "TIEMPO CUMPLIDO" banner are now configurable
- **Flexible time format**: Overlay shows "1 dia 01:30:00" (plural-aware) instead of hardcoded "Dia 1"
- **Events feed**: Timer panel now shows a compact live feed of recent events with deltas

### Fixed

- **Timer auto-start**: Timer now starts armed (stopped) instead of auto-starting when panel opens

## 0.2.4 - 2026-06-24

### Fixed

- **Cloudflare Tunnel**: Restored `_timerOverlayUrl` variable, assignment, and copy-button preference in `app.js` that were accidentally removed in v0.2.3. The "Copiar URL" button now correctly copies the tunnel URL when available.

## 0.2.3 - 2026-06-24

### Fixed

- **app.js**: Added 12 missing `els` cases in Live Timer type column switch.
- **Live Timer UI**: Popup overlay z-index fixed to appear above panel header.
- **CHANGELOG**: Added missing 0.2.2 entry.

## 0.2.2 - 2026-06-24

### Added

- **Cloudflare Tunnel Service**: New `cloudflare_tunnel_service.cpp/hpp` — manages a Child Process running `cloudflared tunnel` for each game session, with lifecycle tracked in `TunnelInfo` (token, URL, pid).
- **Platform**: `cloudflare_tunnel_service` wired into `PanelApp` — tunnel starts on session start, stops on session stop.
- **HTTP JSON**: New `overlayTunnelUrl` field in panel responses.
- **Snapshot**: `overlay_tunnel_url` header added to `PanelSnapshot` / `GameSnapshot`.
- **Package**: `cloudflared.exe` auto-download bundled in `package_windows.ps1`.

### Changed

- **Build**: `cloudflare_tunnel_service.cpp` added to `src/platform/CMakeLists.txt`.

## 0.2.1 - 2026-06-23

### Fixed

- **Live Timer UI**: Manual adjust simplified — removed preset buttons (+30s/+60s/+5min/-30s/-60s), replaced with `−` / `+` sign buttons + numeric input.
- **Live Timer UI**: Font color inputs changed from text field to native `<input type="color">` picker.
- **Live Timer UI**: Font family inputs changed from text field to `<select>` dropdown with 10 web-safe options (Segoe UI, Arial, Helvetica, Verdana, Trebuchet MS, Courier New, Consolas, Georgia, Impact, Times New Roman).
- **Live Timer UI**: Background color removed entirely — overlay stays transparent (hardcoded `"bgColor":"transparent"`).
- **Live Timer UI**: Config details panel overflow fixed (`max-height: 380px; overflow-y: auto`).
- **C++**: Removed `background_color` from `LiveTimerGameState`, config key `kBackgroundColor`, and all parser/serializer code.
- **Tests**: Removed `test_default_config_background_color`.

## 0.2.0 - 2026-06-23

### Added

- **Live Timer: max_time_s**: New config field caps the total accumulated time.
  When set > 0, any addition that would exceed the cap is clamped.
- **Live Timer: manual time adjust**: `POST /api/timer/adjust` endpoint with
  delta support. UI includes quick buttons (+30s/+60s/+5min/-30s/-60s) and
  a custom input field. Negative deltas supported.
- **Live Timer: reset config to defaults**: `POST /api/timer/reset-config`
  restores all timer settings to factory defaults.
- **Live Timer: visual style fields in UI**: Font size, color, family, and
  bold for title, counter, and subtitle are now editable in the config form
  and sent to `POST /api/timer/configure`.
- **Live Timer: background color**: `background_color` now configurable via
  UI and accepted by the configure endpoint.
- **Live Timer: overlay connection error banner**: Red banner appears when
  the overlay loses connection to the panel.
- **Live Timer: event dedup in overlay**: Events carry a monotonic `id`;
  overlay skips already-shown popups on reconnect.
- **Live Timer: confirm on restart**: If the timer is running and the user
  clicks Start, a confirmation dialog prevents accidental reset.
- **AGENTS.md Section 5.5**: Build without re-installation rule — agent must
  check existing build before re-running cmake/vcpkg.

### Changed

- **Live Timer: overlay_host default**: Changed from `"127.0.0.1"` to
  `"localhost"` in `PanelConfig`, matching the documented behavior and
  fixing overlay URL generation in the snapshot.
- **Live Timer: substitute_placeholders extracted**: Now a shared non-anonymous
  function `substitute_timer_placeholders()` in the `nlp3::games` namespace,
  used by both the game code and overlay_assets.cpp (was duplicated).
- **Live Timer: kLiveTimerGameId constant**: All hardcoded `"live-timer"`
  strings replaced with the named constant.

### Fixed

- **Live Timer: remaining_seconds() now triggers completion**: When the
  countdown reaches 0, `remaining_seconds()` automatically sets
  `running=false, completed=true`. Previously `poll_completion_sound()` was
  the only path to detect expiry, causing the overlay to show stale values.
- **Live Timer: format_time() uses floor(), not ceil()**: Display was
  rounding up (e.g. 1.1s → 2s shown). Now truncates correctly.
- **Live Timer: pause() captures remaining before pausing**: The paused
  snapshot was stale because `remaining_seconds()` was called after setting
  `paused=true` (which stopped the clock). Now called before the state change.
- **Live Timer: set_enabled() resets full state**: Previously only toggled
  flags; now also resets `remaining_seconds`, clears `recent_events`, and
  zeroes `total_time_added`.
- **Live Timer: clamp all numeric config values**: timeouts, volumes, and
  font sizes are now clamped to sane ranges on the server side.
- **Live Timer: overlay style comparison**: Fixed object reference comparison
  that never detected style changes after the first render. Now uses
  `styleEqual()` deep comparison function.

## 0.1.10 - 2026-06-13

### Fixed

- **Brand logo**: Replaced corrupt base64 PNG with real Nisoje Studio logo (resized 32×32 from logo package), properly embedded as inline data URI.
- **TTS per-notice toggle**: Each notice now has an ON/OFF toggle. Disabled notices are skipped during sync to legacy bridge and excluded from the voice payload sent to the server.
- **Latency meter not updating**: `renderSystemStatus()` was defined but never called. Added the call in `renderAll()` so `pipelineLatencyMs` reaches `status-latency` on every poll cycle.

## 0.1.9 - 2026-06-12

### Fixed

- **Infinite update loop**: The Worker returns `latest_version` with a `v` prefix ("v0.1.8") but the panel's internal
  version has no prefix ("0.1.8"). The comparison always failed, so the update button never hid after updating.
  Fixed by normalizing: the panel now strips the leading `v` when parsing the Worker response.

## 0.1.8 - 2026-06-12

### Fixed

- **Update button now works correctly**: Replaced `std::system()` with `ShellExecuteExW` (no CMD window, UAC via `runas`).
  After installing, the panel automatically relaunches itself and shuts down the old instance (`PostQuitMessage`).

- **Visual feedback when updating**: The update button now shows "Descargando..." while downloading, and "Error" if
  something goes wrong (with auto-reset after 4 seconds).

- **Fixed version mismatch**: `NLP3_PANEL_VERSION` in `CMakeLists.txt` now correctly reads `0.1.8` (was stuck at `0.1.6`
  even though the release metadata said `0.1.7`). The panel now reports its internal version correctly.

## 0.1.7 - 2026-06-11

### Added

- Device activation on login: Panel Live 3.0 now calls `/api/license/activate` after successful license validation.
  Each PC running the launcher registers itself as a device tied to the license, preventing multi-account abuse.
- New config field `license_activate_path` (default `/api/license/activate`) in panel config.
- Device activation errors are surfaced to the UI as warnings (limit reached, device already registered on another account).

### Fixed

- Removed `SECURITY_FLAG_IGNORE_REVOCATION` usage in `win_http_client.cpp` — constant was removed in Windows 11 24H2 SDK (10.0.26100.0).
  WinHTTP now uses default certificate revocation checking, which is the correct security posture.
- Fixed `PanelUpdaterService` shutdown hang: the worker thread used `sleep_for(6h)` between update checks, blocking the
  destructor's `join()` for up to 6 hours. Replaced with `wait_for()` + `condition_variable` so `stop()` wakes the
  thread immediately and shutdown is instant. This fixes the `panel_app_smoke_test` hang.

## 0.1.11 - 2026-06-23

### Added

- **Live Timer**: New standalone countdown timer module independent of the game system.
  Always visible in UI (center column), always running regardless of active game.
  - `LiveTimerGame` class with real-time countdown (`steady_clock`), configurable time,
    time-per-event extensions (like/share/follow/gift/chat), completion sound polling.
  - Panel UI section between "Actividad del live" and "Métricas" with collapsible config.
  - Overlay HTML (`/overlay/live-timer`) for TikTok Live Studio browser source:
    transparent background, color thresholds, animated popups on event, completion banner.
  - REST endpoints: `GET /api/timer/config`, `POST /api/timer/configure`,
    `GET /api/overlay/live-timer/state`.
  - `PanelTimerStatus` in snapshot with `has_timer`, `remaining_seconds`, `running`, etc.
  - 14 unit tests + 1 API smoke test.

### Fixed

- **UI asset embedding**: `embed_text_asset.cmake` now generates `.inc` files for new
  overlay text assets automatically.
- **Live Timer**: `overlay_host` in `panel_config` replaces hardcoded `127.0.0.1` in overlay URL,
  allowing configurable bind address for TikTok Live Studio browser source access.
- **Live Timer**: `SND_LOOP` sound now stops properly on reset/disable/stop via `PlaySound(nullptr,0,0)`.
- **Live Timer**: Added `POST /api/timer/stop` endpoint; `total_time_added` telemetry now accumulates real deltas.
- **Live Timer**: `on_complete_video_url` and all visual style fields (font size/color/family/bold for
  title/counter/subtitle) now accepted by `POST /api/timer/configure`.
- **Live Timer**: Completion sound beep fallback restored when no sound file is configured.

## 0.1.2 - 2026-04-29

### Fixed

- Refined the embedded game catalog UI actions and download states for the latest Panel Live 3.0 build.
- Tightened catalog card styling so controls remain readable in the release installer.

## 0.1.1 - 2026-04-24

### Fixed

- Kept the TTS apply action visible in the automatic messages header.
- Tightened panel layout constraints so form controls and notice actions stay inside their cards.
- Made the release validation script use the bridge Python runtime with the required TikTok dependencies.

### Added

- Added project-level agent map and routing policy.
- Added shared skill catalog for agent and human workflows.
- Added release policy and release manifest schema.
- Added backup and restore runbooks.
- Added backup and release manifest helper scripts.
- Added versioned release preparation flow for Windows installer, portable ZIP, checksums, and manifest outputs under `dist/releases/<version>`.
- Added contributing guide and roadmap.

### Fixed

- Corrected TikTok timestamp normalization so live events are no longer clamped to `1000000`.
- Expanded TikTok chat ingestion to cover alternate chat event classes.
- Hid timestamps from the activity monitor UI.

### Operational

- Initialized Git history for the project baseline.
- Added a complete operational backup on Desktop after the TikTok monitor fix.
