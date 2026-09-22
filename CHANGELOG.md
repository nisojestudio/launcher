# Changelog

All notable Panel Live changes should be recorded here.

Format follows a lightweight Keep a Changelog style. Versions use SemVer.

## 0.3.3 - 2026-09-22

### Added — Live Timer Bloque A: reglas de eventos (M1, M2, M3, M4, M5)

De `specs/live-timer-mejoras/sugerencias.md`: el timer deja de comportarse como juguete.

- **M2 · Likes por magnitud**: un lote de N likes suma N × `time_per_like_s` (antes solo sumaba una vez por lote, sin importar la magnitud). El backend ya recibía la magnitud del bridge pero la ignoraba. Configuración `like_use_magnitude` (default `true`; desactivar para el comportamiento anterior).
- **M3 · Multiplicadores por tipo de espectador**: `mult_subscriber`, `mult_follower`, `mult_moderator` (doubles, default 1.0, acotados 0..1000). Se aplican solo a deltas positivos. Si el actor tiene varios roles, gana el multiplicador **más alto** (máximo, no producto). Un actor sin roles sigue a 1.0.
- **M1 · Nombre del actor en popup y feed**: los popups del overlay y el feed del panel ahora muestran el nombre del espectador (y la etiqueta del tramo de regalo si aplica). El backend propaga `GameInputActor.display_name` y expone `actorName` + `capped` en cada evento del overlay JSON.
- **M5 · Topes, suelo y antispam**: `cap_per_event_s` (tope bruto por evento), `cap_per_user_per_minute_s` (ventana deslizante de 60 s por actor), `cap_total_per_minute_s` (ventana deslizante global) y `floor_time_s` (suelo del reloj; 0 = completable como siempre). Cuando un delta se recorta por tope, el popup se marca con `capped:true`. Las ventanas se vacían al armar/activar (son de sesión, no persisten entre directos).
- **M4 · Tramos de regalo por valor**: `gift_tiers` acepta una regla por línea (`10-99: 15`, `100+: 300`, `Rosa: 3`). Si una regla casa, sustituye **por completo** la fórmula `coins × time_per_gift_coin`. Excepciones por nombre (contiene, case-insensitive) tienen prioridad sobre los rangos. Reglas malformadas se ignoran (no rompen la sesión).

### Fixed — Interno

- **Ventana de topes ya no se borra sola**: `ContributionWindow::push` purgaba entradas más viejas que la ventana, pero la llamada errada con `window_s=0` vaciaba todo antes de acumular. La ventana real (`kCapWindowS = 60 s`) se pasa explícita.
- **build_windows_installer.ps1 robustez**: `gh` se invoca con arg escaping adecuado para rutas con espacios y con drains asíncronos de stdout/stderr (la corrida de Fase 5 se colgaba en el upload del instalador de 245 MB).
- **`scripts/release/github_release.ps1` reanudable**: si el draft ya existe (por una subida a medias), se reutiliza en vez de fallar; los assets se suben con `--clobber`.

### Verificación

- 33/33 tests C++ (`nlp3_live_timer_game_test`, `nlp3_live_timer_api_smoke_test`, …), 63/63 tests Python.
- Contrato UI↔backend: `scripts/dev/verify_visual_wiring.mjs` → CONTRATO OK.
- End-to-end de publicación validado con release de litera `v9.9.9` (borrado después).

## 0.3.2 - 2026-09-20

### Added — Live Timer Fase 5: Motor visual completo y 3 diseños

- **Motor visual completo (27 claves nuevas)**: escala relativa, marco con 6 estilos (none/card/glass/neon/ribbon/badge), border, radius, padding, brackets, grid, scanlines, text outline, time separator, show hours, warn/danger thresholds, danger effect, progress (ring/bar/none), particles con presupuesto y auto-apagado por FPS.
- **Diseños predefinidos**: 4 botones en el panel que aplican configuraciones validadas:
  - **HUD Órbita** (futurista): neón cian, brackets, grid, scanlines, glitch, chispas
  - **Cristal Líquido** (moderna): glassmorphism, anillo de progreso, radio 28px, sin partículas
  - **Pegatina Brutal** (divertida): tarjeta negra con papel claro, 4px border, confeti activo
  - **Clásico** (restaura defaults anteriores a Fase 5)
- **Panel UI**: nueva sección "🖥 Diseño (motor visual)" con 36 controles visuales y 4 botones de diseño.
- **Coherencia visual automática**: sub-controles que no aplican se desactivan (ej: contorno desaparece cuando el marco está OFF).
- **Validación cliente mejorada**: ahora reciega valores de efectos, tamaños y rangos para evitar sorpresas con el servidor.
- **Tests nuevos**: 18 secciones de test en `live_timer_visual_config_test.cpp`, más cp7 HTTP round-trip real en `live_timer_api_smoke_test.cpp`.

### Fixed — Live Timer

- **Bug `on_complete_text_size`**: ya no se define como `double` y se acota correctamente 8..400.
- **Bug `glow_intensity_px`**: mismo problema, corregido a clamp 1..60.
- **Allow-list de efectos extendido**: ahora incluye `heartbeat`, `float`, `flicker`, `shake`, `odometer`, `typewriter`, `blur`.
- **Coherencia de partículas**: `particles_style = none` ⇒ `particles_enabled = false` automáticamente.

### Verificación

- 17 capturas Playwright en `reports/fase5-visual/` con medidas verificadas.
- 33/33 tests C++ pasados, 63/63 tests Python pasados.
- Contrato UI↔backend verificado con `scripts/dev/verify_visual_wiring.mjs`: **CONTRATO OK**.

## 0.3.1 - 2026-09-20

### Fixed

- **Alertas sonoras con el panel cerrado**: el bridge sobrevive al panel a propósito (resiliencia ante reinicios, con buffer de eventos), pero `SoundAlerts` estaba cableado con `enabled=True` y pitaba en cada intento de reconexión aunque el panel ya estuviera cerrado. Un bridge huérfano de la sesión de `musitogamer` estuvo ~70 minutos emitiendo la secuencia de desconexión/reconexión cada 20–45 s, además de acumular 3985 envíos fallidos al WebSocket del panel y consumir cuota de tik.tools. Ahora las alertas se silencian cuando no hay panel conectado (`sound_alerts.require_panel`, default `true`) y quedan contabilizadas en `sound_alerts_suppressed_total`. Se añaden `--no-sound-alerts` y `--sound-alerts-without-panel` (y las variables `LIVEPANEL_BRIDGE_SOUND_ALERTS_ENABLED` / `LIVEPANEL_BRIDGE_SOUND_ALERTS_REQUIRE_PANEL`) para controlarlo.
  - No se toca la supervivencia del bridge al panel: es intencional. La vía soportada para pararlo es `POST /shutdown` en el puerto de control, que es lo que ya hace el panel al cerrarse.

### Fixed — Live Timer (Fase 1)

- **El tiempo se perdía al cerrar el panel.** El estado se guardaba en `%TEMP%` (Windows lo limpia) y el autosave solo se disparaba cuando cambiaba `event_id_counter`, así que **el avance puro de la cuenta no se persistía nunca**. Ahora vive en `%LOCALAPPDATA%\NisojeStudio\timer\live-timer.json` (con migración automática del save viejo de `%TEMP%`), se guarda en cada transición, cada 10 s **mientras corre**, y siempre al cerrar el panel. Esquema de save subido a v3.
- **Al reiniciar arrancaba solo y ya descontado.** `restore_state` restauraba `running=true` y restaba el tiempo de pared transcurrido con el panel cerrado. Ahora el tiempo se **congela**: se restaura el mismo valor, el timer espera, e `Iniciar` **continúa desde ahí** en vez de volver al tiempo inicial.
- **Arrancaba con 5 minutos de regalo.** `initial_time_s` de `300.0` → `0.0` (también `initial_seconds` y el formulario). Sin tiempo configurado no cuenta: `on_activated` no arranca sobre cero.
- **Pulsar Iniciar no mostraba nada tras restaurar.** `arm()` deja el timer oculto (`--:--:--`) y `on_activated()` no lo des-ocultaba: `running` quedaba en true mientras el overlay seguía pintando guiones. Iniciar ahora también habilita el timer.
- **Las coins se descartaban si el timer no corría.** El handler bloqueaba con `!state_.running`, así que las coins que llegaron con el panel cerrado se perdían en silencio, y con el default en cero tampoco sumaba nada. Ahora se acumula mientras está armado; se sigue bloqueando si está pausado a propósito, completado u oculto.
- **Clamp de cordura al cargar.** Un save con un valor absurdo llegaba a `format_time()` y casteaba a `int64` fuera de rango (comportamiento indefinido); el clamp del HTTP no cubría esa ruta. `initial_time_s` por HTTP ahora acepta 0 (antes mínimo 1 s).
- **Overlay: el día salía en orden incorrecto.** `#days-label` estaba después del contador en el DOM y se leía al revés (`00:00:00` y debajo `2 dias`). Ahora va **sobre** el reloj como distintivo `DÍA N`.
- **Overlay: los popups cruzaban por encima del contador.** Ocupaban todo el ancho bajo el reloj y la animación los subía 40 px. Ahora viven en una **lane lateral derecha** con entrada horizontal y desvanecido en el sitio: sin recorrido vertical, nunca tapan el reloj. Tamaño acotado y escala propia en modo preview.

### Fixed — Live Timer (Fase 2)

- **Cambiar el diseño movía el tiempo.** `apply_config` sumaba la diferencia de `initial_time_s` a `remaining_seconds` cuando el timer estaba corriendo, así que tocar cualquier control de aspecto podía reajustar la cuenta en vivo. Ahora `apply_config` **nunca** toca el reloj si la cuenta está en marcha, pausada o completada; solo adopta un tiempo inicial nuevo cuando el timer está **en reposo** y ese valor **cambió de verdad** (fase de configuración, antes de pulsar Iniciar). Esa condición de "cambió de verdad" es la que protege el requisito de conservar el tiempo: tras restaurar, cambiar solo el diseño no puede reescribir el valor restaurado.
- **El formulario no se cargaba desde el servidor.** Al abrir el panel mostraba los valores de fábrica del HTML, y como el hot path reenvía las claves visuales **del formulario**, tocar cualquier control revertía el diseño guardado del operador a esos defaults. El bloque de poblado vivía inline dentro del handler de Importar; se extrajo a `populateTimerFormFromConfig(config)` y ahora se llama una vez al arrancar tras `GET /api/timer/config`. No se aplica nada de vuelta: el servidor es la fuente de verdad, solo se pinta.
- **El tope `max_time_s` ya no se aplica en `apply_config`.** Recortaba el tiempo recién configurado antes de arrancar. El clamp vive donde sí cambia el tiempo en vivo (`on_game_input_event` y `adjust_time`), que es donde además se reporta el delta real en el popup.

### Tests — Live Timer
- Regresión del requisito central (`nlp3_live_timer_api_smoke_test` cp6): configurar 600 s, arrancar, guardar, reabrir → el tiempo se conserva, **no** arranca solo, e Iniciar continúa desde el valor restaurado.
- Tests actualizados a la nueva verdad (default en cero). Tres dependían implícitamente de los 5 minutos de regalo (`test_on_activated_starts_timer`, `test_negative_config_removes_time`, `test_stop`) y ahora configuran su propio tiempo base, que es lo que realmente querían probar.
- El test suite aísla su estado con `NLP3_TIMER_STATE_DIR` para no leer ni pisar el estado real del usuario.

### Fixed — Live Timer (Fase 3): el panel dejaba de recordar su puerto

- **La URL del quick tunnel corrompía la configuración sola.** El callback de `start_tunnel` escribía la URL efímera en `embedded_ui_url` **y guardaba la config**, y en el arranque siguiente se derivaba de ese campo el puerto local; como una URL de `trycloudflare.com` no es loopback, el panel caía siempre a 18913 y **olvidaba su puerto real**. Ahora el puerto local vive en un campo propio persistido (`embedded_ui_port`), la URL pública del túnel queda solo en memoria (`PanelApp::overlay_public_base_url_`) y no se persiste como configuración, y al cargar se repara el bloque `embedded_ui`: una `embedded_ui_url` no-loopback se reescribe a su loopback real en vez de decidir el puerto.
- **El túnel exponía el panel entero.** cloudflared apuntaba al mismo puerto que la UI del panel, así que `/api/state`, licencia, métricas y la UI quedaban alcanzables desde internet sin autenticación (los `GET` no pasan por `request_requires_access`, que solo cubre `POST`). Ahora `PanelHttpServer` tiene un modo **solo overlay** y el túnel apunta a un segundo listener loopback en puerto efímero que sirve únicamente `/api/overlay/*`; todo lo demás responde 404 desde la URL pública. Si ese listener no arranca, no se abre túnel. Ese segundo listener se bombea también en `PanelApp::tick()`: sin eso el socket aceptaba la conexión pero nunca respondía y Cloudflare devolvía **524** al overlay.
- **La ruta del overlay estaba cableada dentro del servicio de túnel.** `cloudflare_tunnel_service` hacía `url + "/overlay/live-timer"`, así que un servicio genérico conocía la ruta de un juego e impedía que un segundo módulo tuviera su propia URL. Ahora `public_base_url()` devuelve solo la base y la ruta la compone quien sí sabe de overlays.
- **CORS del endpoint de estado.** Lo consume la página estática pública desde otro origen: `GET /api/overlay/live-timer/state` responde con `Access-Control-Allow-Origin: *` (antes era imposible leerlo desde un navegador a través del túnel).

### Added — Live Timer (Fase 3): URL permanente del overlay

- **`https://nisoje.com/overlay/live-timer`** es ahora la URL que se configura **una sola vez** en TikTok LIVE Studio: es una página estática de URL fija que resuelve en runtime cuál es el panel vigente (pregunta a `GET /api/overlay/session` y luego pollea el túnel). Si el panel está apagado no se rompe: muestra un aviso discreto de espera y reintenta, y recoge una URL de túnel nueva sin recargar la página. El campo `overlayTunnelUrl` del panel pasa a contener esa URL permanente; `overlayUrl` sigue siendo la URL local directa (fallback para OBS en la misma máquina).
- **`POST`/`GET /api/overlay/session` en el Worker.** El panel publica ahí la URL de su túnel, autenticado con `Authorization: Bearer <license key>` contra la tabla `licenses`, con caducidad corta (900 s), rate limit de publicación (5 s) y validación estricta de que la URL sea `https` pública. El `GET` es público (lo consume el overlay) y **nunca sirve una URL caducada**: la borra y responde `410`. Son rutas nuevas y aditivas: `/api/me/*` y `/api/license/*` no se tocan.
- El overlay estático solo reproduce sonidos con URL absoluta `http(s)`: el panel entrega rutas locales (`C:\...`, `/sounds/...`) que en un navegador remoto no existen. Se trata como silencio, nunca como beep.
- `/overlay/*` del sitio exime `X-Frame-Options` (la regla global del sitio lo pone en `DENY`) para que un browser source pueda incrustar la página.

### Tests — Live Timer (Fase 3)

- `nlp3_panel_embedded_ui_port_test`: un `panel_config.json` con la URL del túnel escrita en `embedded_ui_url` ya no cambia el puerto local resuelto, la URL se repara a loopback, el túnel no se persiste, y tres arranques seguidos dejan la configuración intacta. Cubre también la migración de configs antiguas sin `embedded_ui_port` y que un override loopback explícito sigue mandando.

### Verificado en producción (Fase 3)

- Panel real (`NisojeStudio.exe --console --ui`) con un `panel_config.json` que llevaba la URL de un quick tunnel escrita en `embedded_ui_url` y `embedded_ui_port = 19403`: **tres arranques seguidos** resolvieron el puerto **19403** y dejaron el fichero **byte a byte idéntico** (antes caía siempre a 18913 y la config se corrompía sola).
- Con el panel en marcha, cloudflared queda apuntando al **listener solo overlay** (puerto efímero), no al puerto de la UI: comprobado en la línea de comandos del proceso hijo.
- Sobre una URL pública real de quick tunnel: `/api/overlay/live-timer/state` → **200** con `Access-Control-Allow-Origin: *`; `/api/state`, `/api/metrics`, `/api/events`, `/`, `/app.js`, `/status` y `/api/bridge/keys` → **404**.
- `https://nisoje.com/overlay/live-timer/` cargada en un navegador real: resuelve el túnel, lee el estado del panel y pinta título, subtítulo y contador verdaderos, con **cero errores de consola**, cero errores de página y cero peticiones fallidas.
- Rutas existentes del sitio y del Worker sin cambios: `/api/status`, `/api/version`, `/api/version/latest`, `/api/time`, `/api/me/licenses` (401), `/api/license-check` (400) y el fallback de rutas desconocidas responden igual que antes del despliegue.

### Build — aviso importante (regresión de build detectada en esta fase)

- **Los rebuilds incrementales de `build/release` no recompilan las cabeceras.** `build/release/CMakeFiles/rules.ninja` contiene `msvc_deps_prefix = Nota: inclusi├│n del archivo:` con la "ó" mal codificada, así que ninja no reconoce las líneas de `/showIncludes` y **no registra ninguna dependencia de cabecera**. Un cambio en `panel_config.hpp` / `panel_app.hpp` / `panel_http_server.hpp` deja el árbol con objetos mezclados de distinto layout (`PanelConfig`, `PanelApp`, `PanelHttpServer`) y los tests revientan con access violation: tras el primer build incremental de esta fase fallaron 13 de 32 con `0xC0000005`, y con un rebuild completo bajaron a 1 (un assert real, ya corregido). Se recuperó borrando `.ninja_deps` y recompilando todo, sin reconfigurar ni tocar vcpkg.
  - Arreglo recomendado: configurar con `VSLANG=1033` (mensajes de MSVC en inglés) para que el prefijo sea ASCII, o corregir la codificación con la que CMake escribe `rules.ninja`.

## 0.3.0 - 2026-09-20

### Added

- **Rotación de cuentas / API keys de tik.tools**: el bridge recibe un pool de credenciales (`--api-keys-file`) y, cuando el proveedor agota la cuota de la key en uso (`4429` / `4555 "Daily Demo Limit"`), la pone en cuarentena y reconecta con la siguiente **sin reiniciar el proceso**. Rotar no consume `max_attempts` ni el límite de reconexiones por hora. Si todas están en cuarentena, el panel informa cuánto falta.
- **Bóveda de credenciales cifrada (DPAPI)**: las API keys se guardan cifradas con `CryptProtectData` en `%LOCALAPPDATA%\NisojeStudio\credentials.dat` (solo el usuario que las guardó puede leerlas). Se **migra y borra** la key en texto plano que quedaba en `panel_config.json`, y el pool viaja al runner por archivo transitorio, nunca en la línea de comandos.
- **Gestión de cuentas en el panel**: bloque "Cuentas y API keys" en el panel de conexión, con etiqueta, huella (`tk_73c3…9a9a`), estado (en uso / disponible / cuota agotada) y alta/baja. Endpoints `GET /api/bridge/keys`, `POST /api/bridge/keys/add`, `POST /api/bridge/keys/remove`.
- **Monitor del live con estados y alertas**: franja de estado (`Iniciando` / `Conectando` / `Esperando el vivo` / `Conectado` / `Error`) con cronómetro y banner de alertas con severidad, contador y limpieza manual, ubicados en el panel **Conexión TikTok** debajo del botón Conectar.
- `error_catalog.py`: catálogo único de errores del bridge (código, mensaje para el usuario, severidad y acción: `wait_for_live`, `rotate_key`, `fix_user`, `check_key`, `wait_provider`).
- **Puerto automático del bridge**: se acabó el 8765 fijo. El panel usa el configurado si está libre, si no el primer libre de 8765–8795 y como último recurso uno efímero asignado por Windows. El puerto efectivo se propaga al runner, al puerto de control y a la UI.

### Fixed

- **El bridge ya no arranca bloqueado por falta de API key**: el sondeo de entorno trata la credencial faltante como aviso (no como error), y `/api/bridge/connect` devuelve el motivo real (`error` + `message` + `runtimeSummary/Warnings/Alerts`) en vez de un `runner_start_failed` genérico. Esto resolvía el incidente documentado en `docs/PENDING_2026-09-18_dist-0.2.29-apikey-workaround.md`.
- **Zombie de puertos (causa raíz)**: Winsock crea sockets heredables y el panel lanzaba sus hijos (python del bridge, cloudflared) con `bInheritHandles=TRUE`; al morir el panel, el hijo mantenía 8765 y 18913 en `LISTEN` con un PID ya inexistente y el panel nuevo no podía volver a bindear. Los listeners del WS, HTTP y overlay ahora se marcan como **no heredables** (verificado: al cerrar el panel los puertos se liberan al instante aunque los hijos sigan vivos).
- **Detección real de puerto ocupado**: en Windows un `bind` puede tener éxito aunque otro proceso escuche el mismo puerto, y entonces las conexiones nuevas llegan al socket ajeno (caso típico de "no conecta"). La ocupación ahora se consulta en la tabla TCP (`PortZombieDetector::is_port_listening`).
- **La limpieza forzada de puertos ya no mata procesos ajenos**: solo restos del panel (python del bridge, cloudflared, Nisoje). Antes podía terminar un proceso de terceros que escuchara en 8765.
- **`4555` (Daily Demo Limit) ya no se reporta como "el usuario no está en vivo"**: se clasifica como cuota agotada y dispara rotación de credencial. Se agregaron además `4556` (relay), `1012` (service restart) y `502`/handshake.
- **`NOT_LIVE` vuelve a ser transitorio**: `not_live_delay_sec` era código muerto porque `NOT_LIVE` salía por la lista de errores no reintentables; el bridge abandonaba en 6 s. Ahora espera a que la cuenta empiece el vivo, con presupuesto configurable (`waiting_for_live_max_minutes`).
- **"Conectado" ya no se declara antes del handshake**: la sesión espera la confirmación de sala (`roomInfo`); si el relay tarda más que el timeout y confirma después, la sesión se declara conectada igual (antes el panel quedaba en "Conectando" para siempre aunque los eventos ya llegaran).
- **Cambiar de cuenta o proveedor con el runner activo ahora reinicia el runner**: antes devolvía `ok` dejando la sesión del usuario anterior, lo que rompía la rotación de cuentas.
- **Se eliminó el falso aviso "never reached connected state"** que aparecía en 43 de 43 sesiones, incluidas sesiones de 2 horas con 5022 eventos.
- **El codec C++ acepta los estados `reconnecting`, `stopped` e `idle`** que el bridge ya emitía: antes descartaba el mensaje `session_status` completo, por lo que los reintentos y sus alertas nunca llegaban al panel. El parser también acepta números decimales (`retry_in_sec: 4.72`).
- **El broadcast interno del bridge ya no choca con el puerto del panel**: se deriva del puerto efectivo (bound + 1) en lugar del 8766 fijo.

### Changed

- El latido de sesión dejó de pisar el estado visible: reutiliza la fase real del `connection_manager` y envía mensajes legibles ("Escuchando el live de TikTok") en vez de `heartbeat`.

### Notes

- El paquete incluye la sincronización de `tools/bridge_py/*.py` hacia el instalador, que era el desfase que provocó el incidente de la API key en 0.2.28/0.2.29.

## 0.2.29 - 2026-09-18

### Fixed

- **Updater no podía descargar el instalador**: `win_http_client.cpp` ahora sigue redirects HTTP (301/302/303/307/308) manualmente con límite de 5 saltos. GitHub devuelve 302 hacia `release-assets.githubusercontent.com` al descargar cualquier release, lo que hacía fallar `trigger_update()` silenciosamente. Verificado end-to-end: descarga completa de 245 MB del instalador v0.2.28.
- Nota: instalaciones en versión ≤0.2.28 tienen el updater roto y deben instalar 0.2.29 manualmente una vez. Desde 0.2.29 en adelante la auto-actualización funciona.

## 0.2.27 - 2026-09-17

### Fixed

- **Backend C++ no aceptaba Euler como provider**: `panel_http_server.cpp` ahora valida `euler` junto con `tiktools` y `direct`. Antes rechazaba con `invalid_tiktok_provider`.
- **Config storage reseteaba Euler a tiktools**: `panel_config_storage.cpp` ahora preserva `euler` al recargar la config guardada.
- **Python bridge rechazaba `--provider euler`**: `argparse.choices` en `run_tiktok_bridge.py` ahora incluye `("tiktools", "direct", "euler")`. Antes el bridge Python no arrancaba al recibir `--provider euler`.
- **JS no mostraba errores de conexión**: `app.js` ahora muestra mensajes claros para `invalid_tiktok_provider`, `ws_start_failed`, `runner_start_failed` en vez de tragar los errores silenciosamente.
- **Dist desincronizado**: `run_tiktok_bridge.py`, `bridge_config.yaml`, `structured_logging.py` en `dist/` estaban desactualizados y no tenían los argumentos `--provider`/`--api-key`.
- **API key no se guardaba para Euler**: El backend ahora guarda la API key tanto para `tiktools` como para `euler`.

## 0.2.26 - 2026-09-17

### Added

- **Proveedor Euler Stream**: Nuevo proveedor WebSocket `euler` para conectar a `wss://ws.eulerstream.com` con autenticación JWT. Selector de 3 proveedores en UI (tiktools / euler / directo).
- **Alertas sonoras de conexión/desconexión**: Módulo `sound_alerts.py` que reproduce tonos via `winsound.Beep()` en Windows: dos tonos ascendentes al conectar, tres tonos descendentes al desconectar, tono único al reconectar.
- **Detección de silencio automática**: `HeartbeatMonitor` declara estado `DISCONNECTED` cuando no llegan eventos por más de `silence_timeout_sec` (default: 2× warning_after_sec). El panel ahora muestra mensajes claros en vez de quedarse en "conectado" cuando la conexión cae.
- **Rate limiter de reconexiones**: `ReconnectRateLimiter` limita a N reconexiones por hora (default: 10) para no agotar tokens del proveedor.
- **Script de prueba de carga**: `test_load.py` con 5 niveles de carga para validar el bridge bajo presión.

### Changed

- **Máximo 5 intentos de reconexión**: `max_attempts` cambiado de 0 (ilimitado) a 5 por defecto.
- **Jitter en backoff exponencial**: `jitter_sec` (default 1.0s) evita thundering herd en reconexiones simultáneas.
- **I/O de archivo asíncrono**: `append_jsonl_async()` y `write_json_async()` usan `asyncio.to_thread()` para no bloquear el event loop.
- **PanelWsSink con cooldown exponencial**: Fallos consecutivos aumentan el cooldown de reconexión al panel (0.5s → 32s).
- **Mensajes de estado en español**: "DESCONECTADO", "RECONECTANDO", "Advertencia: sin eventos durante Xs".
- **UI del panel**: Campo API Key ahora sirve para tiktools y euler. Label genérico "API Key" en vez de "API Key de tik.tools".

### Fixed

- **Panel mostraba "conectado" tras caída de conexión**: El heartbeat ahora detecta silencio y cambia el estado a DISCONNECTED con mensaje claro.
- **I/O síncrono bloqueaba el event loop**: Escritura a JSONL e inbox ahora es completamente asíncrona.

### Build & Workflow

- Tests Python: 44 tests pasan.
- UI embebida en `NisojeStudio.exe` via `.inc` generados por CMake.

## 0.2.25 - 2026-09-10

### Added

- **Proveedor `tiktools` para bridge TikTok**: Nueva alternativa al modo `direct` (TikTokLive). Conecta vía WebSocket a `api.tik.tools` usando API key, sin depender de `TikTokLive` local. Incluye manejo de errores específico (API key inválida, límites de sesión, usuario no en vivo) y mapeo completo de eventos (chat, gift, like, follow, share, viewer count, live start/end).
- **Campo API key de tik.tools en UI**: Nuevo selector de proveedor (`tiktools` / `direct`) y campo de API key en la sección "Live / TikTok" del panel. La clave se guarda encriptada en config y persiste al alternar proveedores.
- **Recuperación de contraseña (Forgot Password)**: Botón "¿Olvidaste la contraseña?" en la pantalla de auth que usa Firebase Auth `sendOobCode` para enviar correo de restablecimiento.
- **CLI extendido del bridge Python**: Nuevos argumentos `--api-key` y `--provider` (`tiktools` | `direct`) en `run_tiktok_bridge.py`. Variable de entorno `LIVEPANEL_TIKTOOLS_API_KEY` soportada.

### Changed

- **Proveedor por defecto a `tiktools`**: `bridge_config.yaml` y `BridgeConfig` ahora usan `tiktools` como `connection_mode` predeterminado.
- **Flujo de conexión bridge unificado**: `/api/bridge/connect` ahora acepta `provider` y `api_key`, guarda configuración siempre, y si el panel no está en modo `external` lo fuerza y pide reinicio (error `bridge_not_external_mode_saved`).
- **API key persistente**: La clave de tik.tools se guarda en `panel_config.json` (`tiktools_api_key`) para no tener que reingresarla al cambiar de proveedor.
- **Endpoint `/api/bridge/status`**: Expone `api_key_configured` y `provider` para que la UI refleje el estado real.

### Fixed

- **HTTP 403 en bridge WebSocket**: `tiktok_external_ws_server.cpp` usa cierre graceful (`shutdown(SD_SEND)` + `SO_LINGER` off) tras enviar respuesta 403, evitando que un RST descarte la respuesta antes de que el cliente la lea.
- **Validación de usuario TikTok**: Eliminada validación estricta `is_valid_tiktok_user()` que rechazaba formatos válidos; ahora `normalize_tiktok_user()` acepta `usuario`, `@usuario` y URLs.

### Build & Workflow

- `clear_ports.bat` y `clear_port8765.bat` actualizados para limpieza robusta de puertos 8765/8766/8770 antes de arranque.
- Tests Python: 38 tests pasan (`unittest discover -s tools/bridge_py/tests`).

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
