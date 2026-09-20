# Reporte — Blindaje de la conexión TikTok y release 0.3.0

- **Fecha:** 2026-09-20
- **Alcance:** Panel Live 3.0 (`Nisoje LivePanel V3`) — integración TikTok (bridge Python + panel C++)
- **Versión resultante:** 0.3.0 (tag `v0.3.0`, commit de release `13736cc`)
- **Estado:** release empaquetado y validado; pendiente la instalación en el equipo (UAC) y la publicación en producción

---

## 1. Resumen ejecutivo

La conexión con TikTok no funcionaba. La auditoría (solo lectura) determinó que **el transporte no estaba roto**: el bridge ya había funcionado durante horas con miles de eventos. Los bloqueos reales eran:

1. **El panel instalado era la build 0.2.28 (pre-fix)** y su sondeo previo abortaba el arranque del bridge por "falta de API key", sin dejar rastro en el log del bridge.
2. **Un zombie de sockets**: por herencia de handles de Winsock, el puerto 8765 (y 18913) quedaba en `LISTEN` con un PID ya inexistente, y el panel nuevo no podía volver a bindear.
3. **Errores mal clasificados** en el bridge: `4555` (límite de demo) se reportaba como "el usuario no está en vivo", y `NOT_LIVE` era no-reintentable (con `not_live_delay_sec` como código muerto), por lo que el bridge abandonaba en 6 s.

Sobre esa base se ejecutaron 4 fases de trabajo (blindaje, monitor, rotación de credenciales, puerto automático), se validó en vivo contra TikTok y se empaquetó el release **0.3.0** con todos los gates en verde.

---

## 2. Contexto y síntoma reportado

- Síntoma: "el panel live no conecta con TikTok".
- Entorno: panel instalado en `C:\Program Files\Panel Live`; bridge Python con runtime embebido (`python_runtime\python.exe`, Python 3.14.3); proveedor `tik.tools`.
- El panel se dejaba abierto durante las pruebas; las pruebas de conexión se hicieron con `musitogamer`.

---

## 3. Metodología

1. **Auditoría solo lectura** sobre repo, `dist/`, la instalación en `Program Files` y los logs de ejecución reales (`%LOCALAPPDATA%\NisojeStudio\logs\bridge.jsonl`, 939 líneas, 10/09 → 20/09).
2. **Reproducción controlada** de cada hipótesis (por ejemplo: ejecutar el sondeo de entorno instalado tal cual lo lanza el panel; experimento aislado de herencia de sockets en `%TEMP%`).
3. **Corrección por fases** con tests unitarios por cada comportamiento nuevo.
4. **Validación en vivo** con panel abierto y sesión real de TikTok.
5. **Empaquetado** siguiendo `docs/releases/RELEASE_PROTOCOL.md` (Hard Rules incluidas).

---

## 4. Causas raíz y evidencia

| # | Causa raíz | Evidencia recolectada | Corrección |
|---|---|---|---|
| 1 | Build instalada sin el fix del sondeo (`--api-key`) | `NisojeStudio.exe` instalado = SHA256 `E2F6D5FB…D76ABA9`, **idéntico** al de `dist/releases/0.2.28`; el fix es el commit `8e60581` (posterior). El sondeo instalado (18.285 B) no acepta `--api-key`; el del repo (18.728 B) sí. | Sondeo no bloqueante por credencial + release 0.3.0 con el bridge sincronizado |
| 2 | El sondeo bloqueaba el arranque | Ejecutado el sondeo instalado: `{"ok": false, "summary": "tiktools provider selected but no API key configured"}` → `ExternalBridgeRunner::start()` aborta (`runner_start_failed`) | La credencial faltante pasa a **aviso**; el bloqueo queda sólo para fallos reales de runtime |
| 3 | Zombie de puertos por herencia de sockets | En producción: **8765 y 18913 en `LISTEN` con PID 5104 inexistente**, único superviviente `cloudflared.exe` (1568). Al terminarlo, ambos puertos quedaron libres. Experimento aislado: puerto 18766 en `LISTEN` con PID padre muerto; se liberó **sólo al terminar el hijo** que heredó el handle | Listeners de WS/HTTP/overlay marcados como **no heredables** |
| 4 | Un `bind` exitoso no garantiza ser dueño del puerto | Con 8765 ocupado, el panel igual quedó escuchando 8765 **a la vez** que el ocupante | Detección de ocupación por **tabla TCP**, no por `bind` |
| 5 | La limpieza forzada mataba procesos ajenos | El ocupante de las pruebas era terminado por el propio panel | La limpieza sólo toca restos del panel (python del bridge, cloudflared, Nisoje) |
| 6 | `4555` (Daily Demo Limit) mal clasificado | `_NOT_LIVE_CLOSE_CODES` incluía 4555; el log lo reportaba como "el usuario no está en vivo" | Catálogo de códigos de cierre; 4555 = cuota agotada (dispara rotación) |
| 7 | `NOT_LIVE` no reintentaba | `NOT_LIVE` estaba en la lista de no-reintentables: `compute_retry_delay` retornaba `None` antes de la rama `not_live_delay_sec` → código muerto | `wait_for_live`: espera con `not_live_delay_sec` y presupuesto configurable |
| 8 | "Conectado" antes del handshake | Log: "session connected" + sonido y 6 s después `4404` | La sesión espera la confirmación de sala; si llega tarde, se declara conectada |
| 9 | Estados descartados por el codec C++ | `reconnecting`, `stopped` e `idle` no existían en el enum → el panel descartaba el `session_status` completo | Enum y parser ampliados; el parser además acepta decimales |
| 10 | Cambio de cuenta sin efecto | `/api/bridge/connect` sólo arrancaba el runner si no estaba corriendo → devolvía `ok` manteniendo al usuario anterior | Reinicia el runner si cambia usuario o proveedor |
| 11 | Aviso falso en cada sesión | "TikTok connection ended without reaching connected state" en **43 de 43** sesiones, incluidas dos de ~2 h con 5.022 eventos | Flag histórico `ever_connected` |
| 12 | Broadcast del bridge chocando con el puerto del panel | Con el panel en 8766, el runner salía con código 1 (el bridge intentaba 8766 para su broadcast) | Broadcast derivado del puerto efectivo (bound + 1) |

---

## 5. Trabajo realizado por fase

### Fase 1 — Desbloquear el arranque
- `tools/bridge_py/bridge_env_check.py`: la credencial faltante se reporta en `warnings` (no bloquea); se agrega `blocking` por chequeo.
- `src/platform/external_bridge_runner.{hpp,cpp}`: el sondeo propaga `warnings`.
- `src/platform/panel_http_server.cpp`: `/api/bridge/connect` devuelve `error` (código) + `message` (motivo real) + `runtimeSummary/Alerts/Warnings`.
- `src/platform/ui/app.js`: la UI muestra el motivo real.

### Fase 2 — Blindaje del bridge
- **Nuevo** `tools/bridge_py/error_catalog.py`: código → mensaje / severidad / acción (`retry`, `wait_for_live`, `rotate_key`, `fix_user`, `check_key`, `wait_provider`).
- `tiktools_connection.py`: clasificación por catálogo; handshake real antes de `CONNECTED` (piso de 45 s por latencia medida del relay).
- `connection_manager.py`: `NOT_LIVE` transitorio con `waiting_for_live_max_minutes`; rotación de credencial; fase real para el latido.
- `session_manager.py`: el latido no pisa la fase ni el mensaje útil.
- `run_tiktok_bridge.py`: fin del falso "never reached connected state".

### Fase 5 — Monitor del live
- `session_status` ampliado: `phase`, `severity`, `alert_code`, `retry_in_sec`, `provider`, `key_label` (bridge + codec C++ + snapshot + JSON).
- UI: franja de estado (`Iniciando / Conectando / Esperando el vivo / Conectado / Error`) con cronómetro y **banner de alertas** con severidad, contador y limpieza.
- **Ubicación**: en el panel **Conexión TikTok**, debajo del botón Conectar (antes estaba en "Actividad del live"). El test de UI verifica el orden `conexión < estado < alertas < actividad`.

### Fase 3 — Cuentas y API keys
- `bridge_config.py` + `run_tiktok_bridge.py`: pool de credenciales (`--api-keys-file`, JSON `{keys:[{label,value}]}`) y cuarentena configurable.
- `connection_manager.py`: rotación automática al agotarse la cuota, sin reiniciar el proceso y sin consumir `max_attempts` ni el límite de reconexiones por hora.
- **Nuevo** `src/platform/bridge_key_vault.{hpp,cpp}`: bóveda cifrada con DPAPI (`%LOCALAPPDATA%\NisojeStudio\credentials.dat`), huella corta para la UI.
- `panel_app.cpp`: carga de la bóveda y **migración** de la key en texto plano, borrándola de `panel_config.json`; el pool se entrega en archivo transitorio (se borra al detener el runner).
- Endpoints `GET /api/bridge/keys`, `POST /api/bridge/keys/add`, `POST /api/bridge/keys/remove` + UI de gestión.

### Fase 4 — Puerto automático
- `tiktok_external_ws_server.cpp`: rango 8765–8800 y soporte de puerto efímero (con `getsockname`).
- `panel_app.{hpp,cpp}`: `resolve_external_ws_bind_port` (configurado → primer libre del rango → efímero) y `start_external_ws_auto`; el puerto efectivo se propaga al runner, al control y a la UI.
- `port_zombie_detector.{hpp,cpp}`: `is_port_listening` (tabla TCP, cualquier PID) y limpieza forzada restringida a procesos del panel.

---

## 6. Validación ejecutada

### Tests automatizados
| Suite | Resultado |
|---|---|
| `ctest --test-dir build/release-0.1.1` (completa, tras Fases 3 y 4) | **31/31 passed** |
| `ctest` (subconjunto tras el último cambio) | **7/7 passed** |
| `pytest tools/bridge_py/tests` | **52 passed** |
| Gates del release (`release-manifest-0.3.0.json`) | `build/tests/installer/backup = passed` |

Tests nuevos que fijan el comportamiento: credencial faltante no bloqueante (tiktools y Euler), códigos de cierre (`4555`/`4429`/`4404`/`4556`/`1012`), `NOT_LIVE` que espera en vez de morir, handshake tardío declarado conectado, rotación de credencial por cuota, campos de estado/alerta en el snapshot, orden del monitor en el HTML.

### Pruebas en vivo (panel abierto, TikTok real)
| Escenario | Resultado |
|---|---|
| `musitogamer` (en vivo) | `connected`, sala `7687463955770116885`, eventos creciendo (330 → 382 → 423) |
| `rojogamer_` (sin live) | Queda en `waiting` con aviso visible; **no muere** |
| 8765 ocupado por un proceso ajeno | El panel tomó **8766**; runner `--ws ws://127.0.0.1:8766`; sesión `connected` con 69 eventos; **el ocupante quedó intacto** |
| Cierre del panel | 8765 y 18913 se liberan al instante, con los procesos hijos aún vivos (sin zombie) |
| Credenciales | `panel_config.json` sin key; `credentials.dat` cifrado (sin `tk_` en claro); argv del runner sin secretos |

---

## 7. Release 0.3.0

| Artefacto | Tamaño | SHA256 |
|---|---|---|
| `dist/releases/0.3.0/installer/panel-live-0.3.0-win-x64.exe` | 234,1 MB | `1219B6A50A5A8AA75BA35278B6EF15727834F6B3FE5C7603A82AEB3911038896` |
| `dist/releases/0.3.0/panel-live-0.3.0-win-x64-portable.zip` | 39,7 MB | `7BFCE5CC35D0FDDF2B2E33DA4868EBA0E7B14146B673E11DECC9D5DEDB61AA7B` |
| `SHA256SUMS.txt` | — | 3 hashes (incluye el exe empaquetado) |

**Sincronización repo → paquete verificada** en `dist/releases/0.3.0/NisojeStudio/tools/bridge_py`: `error_catalog.py` presente, `bridge_env_check.py` con `--api-key`, `run_tiktok_bridge.py` con `--api-keys-file`, `bridge_config.yaml` con `waiting_for_live_max_minutes`. Este era el desfase que originó el incidente de 0.2.28/0.2.29.

**Commits:** `85dc85b`, `5845d49`, `957cc97`, `5332c40`, `1e489b0`, `e121035`, `13736cc` (release), `0dd442a` (docs).

---

## 8. Riesgos, limitaciones y desvíos

1. **Desvío documentado — backup `full`:** `-BackupMode full` falla en este equipo (robocopy exit 8 por archivos bloqueados: 8 elementos de 56.100). Se usó `-BackupMode code`, que pasó. Anotado en `AGENTS.md`.
2. **Desvío documentado — entorno MSVC:** `prepare_release.ps1` ejecuta `cmake --build --preset release`, que no hereda el entorno de Visual Studio por sí solo y falla con `fatal error C1083: 'cstddef'` (Rule 2 del protocolo). El pipeline se ejecutó dentro de `vcvars64.bat`; el comando exacto quedó documentado.
3. **Latencia del proveedor:** el relay de tik.tools puede tardar ~45 s en confirmar la sala; el sistema ahora lo tolera (no corta la sesión) y lo comunica.
4. **Cuota del plan:** la key en uso es de plan Community/demo (50 sesiones WS/24 h). La rotación mitiga el problema, no lo elimina: si todas las keys están en cuarentena, el panel informa la espera.
5. **Alcance de la validación:** no publiqué el release en producción ni instalé 0.3.0 en `C:\Program Files` (requiere UAC). Las pruebas en vivo se hicieron con el binario del repo (`build/release-0.1.1`) y un `panel_config.json` de prueba en `build/` (ignorado por git).

---

## 9. Pendientes y próximos pasos

1. **Instalar 0.3.0** (requiere UAC):
   `dist\releases\0.3.0\installer\panel-live-0.3.0-win-x64.exe`
2. **Publicar en producción** (GitHub release + URLs del sitio + Worker), con `gh` ya autenticado:
   `scripts\release\github_release.ps1 -Version 0.3.0 -Changelog "..."`.
3. Validar en la instalación real el flujo completo: conectar, ver estados/alertas, cargar 2+ cuentas y forzar una rotación.

---

## 10. Rollback

- Código: `git revert` de los commits listados en §7, o volver al snapshot previo al release (`Panel live 3.0-code-2026-09-20_03-43-21`) con `scripts/backup/restore_project_backup.ps1`.
- Instalación: reinstalar `dist/releases/0.2.29/installer/panel-live-0.2.29-win-x64.exe`.
- Tag: `v0.3.0` (local; el release de GitHub no se publicó).

---

## 11. Anexo — comandos de reproducción

```powershell
# Build y tests del panel (MSVC + ninja)
cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" >nul && ninja -C build\release-0.1.1"
ctest --test-dir build\release-0.1.1 --output-on-failure

# Tests del bridge
tools\bridge_py\.venv\Scripts\python.exe -m pytest tools\bridge_py\tests -q

# Sondeo de entorno (sin credencial -> aviso, no bloqueo)
tools\bridge_py\.venv\Scripts\python.exe tools\bridge_py\bridge_env_check.py --format json

# Sesión real del bridge (aislada)
$env:LIVEPANEL_BRIDGE_API_KEY = 'tk_...'
tools\bridge_py\.venv\Scripts\python.exe tools\bridge_py\run_tiktok_bridge.py --user musitogamer --provider tiktools --max-seconds 45
Remove-Item Env:LIVEPANEL_BRIDGE_API_KEY

# Pipeline de release (Hard Rules: ejecutar el script, con entorno MSVC cargado)
cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" >nul && powershell -ExecutionPolicy Bypass -File .\scripts\release\prepare_release.ps1 -Version 0.3.0 -BackupMode code"
```
