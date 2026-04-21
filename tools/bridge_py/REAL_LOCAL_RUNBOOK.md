# Real Local Runbook

## 1. Preparar el panel

1. Verifica que `panel_config.json` tenga:
   `"bridge_mode": "external"`
2. En una maquina nueva, prepara primero el entorno Python local del bridge:
   `powershell -ExecutionPolicy Bypass -File .\tools\bridge_py\setup_windows_bridge_env.ps1`
3. Si quieres operar con UI local, arranca:
   `.\build\src\platform\NisojeStudio.exe --ui`
4. La UI queda en `http://127.0.0.1:8080` por defecto.
5. Si prefieres consola, abre:
   `.\build\src\platform\NisojeStudio.exe --console`
6. El panel arrancara automaticamente el WS local en `8765`.
7. Puedes verificarlo con:
   `bridge ws`
8. Si quieres que el panel sea dueno del runner real:
   `bridge target alice`
   `bridge runner start`
   O con corte automatico:
   `bridge runner start alice 30`
   `bridge runner status`
   `bridge runner logs`
   `bridge runner stop`

## 2. Probar con eventos simulados

1. En una segunda terminal, genera eventos:
   `python tools/bridge_py/sample_events.py --ws ws://127.0.0.1:8765`
2. En la UI o consola del panel, revisa:
   `status`
   `activity`
   `bridge ws`

## 3. Probar con TikTok real

1. Si usaras `--ws`, instala `websockets` en el entorno Python elegido.
2. La ruta mas simple para uso diario es:
   `powershell -ExecutionPolicy Bypass -File .\tools\bridge_py\start_real_session.ps1 -User alice`
3. Para una prueba corta con cierre automatico:
   `powershell -ExecutionPolicy Bypass -File .\tools\bridge_py\start_real_session.ps1 -User alice -MaxSeconds 30`
4. Si quieres operar manualmente desde la consola del panel, opcionalmente deja listo el target y el puerto:
   `bridge target alice`
   `bridge ws port 8765`
   `bridge attach alice`
5. En una segunda terminal, si no usas el launcher, arranca el bridge adaptado:
   `python tools/bridge_py/run_tiktok_bridge.py --user alice`
6. Si quieres forzar destino explicitamente:
   `python tools/bridge_py/run_tiktok_bridge.py --user alice --ws ws://127.0.0.1:8765`
7. Si quieres una prueba corta con cierre automatico:
   `python tools/bridge_py/run_tiktok_bridge.py --user alice --max-seconds 30`
8. En la UI o consola del panel, revisa:
   `status`
   `activity`
   `bridge external`
   `bridge ws`
   `diagnostics`
9. Si quieres ver salud y metricas del bridge Python en paralelo:
   `python tools/bridge_py/run_tiktok_bridge.py --user alice --status-port 19021 --no-broadcast-ws`
   Luego abre:
   `http://127.0.0.1:19021/health`
   `http://127.0.0.1:19021/status`
   `http://127.0.0.1:19021/metrics`

## 4. Senales esperadas

- `bridge demo ready` deberia terminar en `Demo ready: yes`
- `activity` deberia mostrar chat/gift/follow
- `bridge external` deberia mostrar `target_user`, `connection_state`, `current_room_id` y contadores por tipo
- `bridge external` deberia aumentar `total_external_events_submitted`
- `bridge ws` deberia aumentar `accepted_messages`
- `bridge runner status` deberia mostrar `running=yes` mientras la captura real siga activa
- La UI local deberia reflejar `Bridge`, `Runner`, `Diagnostics` y actividad reciente sin recargar la aplicacion

## 5. Nota importante del modo WS local

- En `--console`, el panel mantiene el loop local de `tick(...)` mientras espera comandos.
- En modo `external`, el WS local arranca automaticamente en `ws://127.0.0.1:8765`.
- Si `run_tiktok_bridge.py` no recibe `--output`, `--inbox` ni `--ws`, ahora usa esa ruta local por defecto.
- Para soporte o pruebas operativas cortas, `--max-seconds` deja cerrar la captura con un resumen final de sesion.
- `--max-seconds` ahora tambien corta una sesion aunque el live no emita eventos o este reconectando.
- `bridge target`, `bridge ws port` y `bridge attach` ayudan a dejar el panel listo y persistir la configuracion con `config save`.
- `start_real_session.ps1` hace ese setup por ti y es la ruta mas directa para una sesion real local.
- `bridge runner start` ya deja operar el runner real desde la propia consola del panel cuando prefieras no abrir otra terminal para Python.
- `bridge runner start [user] [max_seconds]` te deja hacer una prueba real acotada sin salir de la consola del panel.
- `bridge runner logs` muestra el tail de salida capturado del runner Python para soporte y futura UI.
- `bridge runner stop` ahora intenta primero un shutdown limpio del bridge Python por su control HTTP local y solo fuerza el proceso si ese shutdown no responde.
- `--ui` expone una superficie HTTP local real para operacion de producto sobre el mismo `PanelApp`.
- `README_bridge.md` describe la arquitectura modular del bridge, config YAML, Dockerfile y launcher Unix.
