# Arena Live ↔ Nisoje Studio

## Ruta de descubrimiento local
- El panel busca juegos externos en `%USERPROFILE%\Desktop\Juegos`.
- `Arena Live` debe vivir en `%USERPROFILE%\Desktop\Juegos\Arena Live`.
- Override soportado para desarrollo o staging:
  - `NLP3_LOCAL_GAMES_ROOT`

La deteccion sale de esa carpeta externa, no del repo del panel.

## Resolucion de arranque real
- `module_root` funcional del juego sigue siendo `%USERPROFILE%\Desktop\Juegos\Arena Live`.
- Si el ejecutable real vive dentro de un bundle compilado, por ejemplo `build\Release\ArenaLive.exe`, el bridge lo resuelve y lo lanza desde ese bundle.
- Los archivos de integracion siguen anclados a la raiz del juego:
  - `config/live_config.json`
  - `runtime/inbox/events.jsonl`
  - `runtime/status.json`
  - `runtime/host.log.jsonl`
- Esto permite usar el juego desde la ruta externa dada sin moverlo al repo del panel.

## Contrato del modulo
- Archivo requerido: `module_manifest.json`
- Entry actual de `Arena Live`: `ArenaLive.exe`
- Archivos de comunicacion del juego:
  - `config/live_config.json`
  - `runtime/inbox/events.jsonl`
  - `runtime/status.json`
  - `runtime/host.log.jsonl`

## Flujo de integracion
1. Nisoje Studio descubre `Arena Live` leyendo `module_manifest.json`.
2. El panel activa el juego con `/api/game/start`.
3. `tools/game_bridge_py/run_local_game_bridge.py` resuelve el ejecutable real del juego y, si hace falta, lanza el bundle desde `build\Release` manteniendo `config`, `inbox`, `status` y `log` en la raiz de `%USERPROFILE%\Desktop\Juegos\Arena Live`.
4. El panel escribe eventos del live en `runtime/panel_bridge/inbox/panel_events.jsonl`.
5. El bridge los normaliza y los reenvia al inbox real del juego.
6. El juego publica estado interno en `runtime/status.json`.
7. El bridge consolida estado, logs y paths en `runtime/panel_bridge/state.json` para que el panel los lea de vuelta.

## Eventos soportados desde el panel
- `like`
  - salida al juego: `kind=like`
  - datos: `count`
- `avatar`
  - salida al juego: `kind=join`
  - datos: `avatarUpdate=true`, avatar del usuario y mensaje de actualizacion
- `gift`
  - salida al juego: `kind=gift`
  - datos: `giftName`, `count`, `diamond`, `coins`

## Eventos internos que vuelven al panel
- `score` y ranking: leidos desde `runtime/status.json` y expuestos en `snapshot.externalGame.ranking`
- `log`: leidos desde `runtime/host.log.jsonl` y expuestos en `snapshot.externalGame.recentLogs`
- `achievements`: leidos desde el payload canonico del juego y expuestos en `snapshot.externalGame.achievements`

## Launcher manual
- Script: `tools/game_bridge_py/start_local_game_bridge.ps1`
- Wrapper: `tools/game_bridge_py/start_local_game_bridge.bat`

Ejemplo:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\game_bridge_py\start_local_game_bridge.ps1 -GameRoot "$env:USERPROFILE\Desktop\Juegos\Arena Live"
```

Valida:
- `Microsoft Visual C++ Redistributable x64`
- `Microsoft Edge WebView2 Runtime` si el modulo declara `webview`
- runtime Python del bridge

## Smoke operativo
- Script: `scripts/smoke_arena_live_integration.ps1`

Ejemplo:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke_arena_live_integration.ps1
```

Ese smoke:
- arranca el panel
- activa `Arena Live`
- envia `like`, `avatar` y `gift`
- verifica `/health`
- verifica `/api/state`
- espera `runtime/panel_bridge/state.json`

## Logs minimos
- Panel UI / host:
  - `%TEMP%\NisojeStudio\embedded_ui.log`
- Bridge del juego:
  - `%USERPROFILE%\Desktop\Juegos\Arena Live\runtime\panel_bridge\bridge.log.jsonl`
- Estado consolidado:
  - `%USERPROFILE%\Desktop\Juegos\Arena Live\runtime\panel_bridge\state.json`
- Logs del juego:
  - `%USERPROFILE%\Desktop\Juegos\Arena Live\runtime\host.log.jsonl`

## Agregar nuevos eventos
1. Añadir el nuevo `kind` en `PanelApp::forward_host_event_to_external_game`.
2. Normalizarlo en `tools/game_bridge_py/run_local_game_bridge.py`.
3. Consumirlo del lado del juego en el inbox real.
4. Añadir smoke o test automatizado para ese evento.

## Plantilla para nuevos juegos legacy
- Plantilla base:
  - `tools/game_bridge_py/templates/external_game_module/README.md`
  - `tools/game_bridge_py/templates/external_game_module/module_manifest.template.json`
  - `tools/game_bridge_py/templates/external_game_module/config/live_config.template.json`
  - `tools/game_bridge_py/templates/external_game_module/runtime/status.template.json`
- Auditoria previa:
  - `python tools/game_bridge_py/audit_external_game_contract.py --game-root "<Game Root>"`

### Politica de lanzamiento del bridge
- `bridge.launch.passModuleRootArg`
  - `true`: el bridge agrega `--module-root <launch_root>` al comando del juego.
  - `false`: usar cuando el ejecutable legacy no tolera flags extra.
- `bridge.launch.workingDirectory`
  - `module_root`: cwd por defecto, anclado a la raiz del modulo.
  - `launch_root`: usar cuando el ejecutable necesita correr desde su bundle real, por ejemplo `build\Release`.

## Preparacion para fuente remota futura
- El panel no depende del repo del juego.
- El modulo ya se resuelve por `module_manifest.json` y `module_root`.
- Para pasar a servidor remoto, la pieza que cambia es el resolvedor de origen del modulo:
  - descarga/sincronizacion del juego
  - cache local del modulo
  - actualizacion del `module_root`

El contrato panel ↔ bridge ↔ juego puede mantenerse igual.
