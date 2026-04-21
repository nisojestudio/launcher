# Nisoje Studio

Host local para juegos live con bridge TikTok `stub` y `external`, consola operativa, runner Python integrado y una UI local servida por el propio panel.

## Estado actual

El proyecto ya soporta:
- host C++ local con `PanelApp`
- bridge `external` por WebSocket local
- runner Python real o simulado
- backend TTS real de Windows SAPI con catalogo de voces, filtros de chat, templates editables y prueba de voz
- bridge Python modular con reconnect, replay, burst, health, status y metrics
- replay, inbox y JSONL para pruebas
- juego `event-counter`
- UI HTTP local para operacion y monitoreo

## Arranque rapido

### Consola

```powershell
Set-Location <repo-root>
.\build\src\platform\NisojeStudio.exe --console
```

### Ventana embebida

```powershell
Set-Location <repo-root>
.\build\src\platform\NisojeStudio.exe
```

Por defecto, si `embedded_ui_enabled=true`, la app abre su propia ventana nativa `Nisoje Studio` con WebView2.

### Paquete portable Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1
```

Eso genera:

- `dist\NisojeStudio\`
- `dist\NisojeStudio-portable.zip`

### Instalador Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_windows_installer.ps1
```

Eso genera:

- `dist\installer\PanelLive-3.0-Windows-x64-Setup.exe`
- `dist\SHA256SUMS.txt`

Ruta de entrega preparada actualmente:

- `dist\installer\PanelLive-3.0-Windows-x64-Setup.exe`
- `dist\NisojeStudio-portable.zip`

El packaging portable usa un host C++ `Release` en `build\release` y falla si detecta dependencias a CRTs Debug como `MSVCP140D.dll` o `ucrtbased.dll`.

Prerrequisitos oficiales del portable Windows:

- `Windows x64`
- `Microsoft Visual C++ Redistributable x64` instalado en la maquina destino
- `Microsoft Edge WebView2 Runtime` para la ventana embebida

El package final ya incluye el runtime Python del bridge en `tools\bridge_py\python_runtime`, por lo que no necesita Python del sistema. El portable deja ademas un archivo `PORTABLE_REQUIREMENTS.txt` en la raiz con estos prerrequisitos y el launcher `Launch Nisoje Studio.bat` avisa si falta el runtime VC++.

### Launcher de escritorio para usuario final

Para dejar un acceso directo de escritorio que abra el panel sin mostrar consola:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\create_panel_desktop_shortcut.ps1
```

Eso crea `Panel Live 3.0.lnk` en el escritorio del usuario actual. El acceso directo usa `scripts/start_panel_live_hidden.ps1`, que:

- valida `tools\bridge_py\.venv`
- usa el Python del virtualenv del bridge
- detecta automaticamente el launcher Python del panel en `tools\bridge_py`
- arranca `NisojeStudio.exe` con `NLP3_LOCAL_GAMES_ROOT=%USERPROFILE%\Desktop\Juegos`
- deja un log minimo en `%TEMP%\NisojeStudio\desktop_launcher.log`

Si quieres ver el flujo con consola para diagnostico manual, puedes usar:

```powershell
.\Launch Panel Live 3.0.bat
```

### UI local / modo browser

```powershell
Set-Location <repo-root>
.\build\src\platform\NisojeStudio.exe --ui
```

Ese mismo comando sigue arrancando la UI local, pero ahora prioriza la ventana embebida. Si WebView2 falla y el fallback esta habilitado, abre el navegador del sistema sobre la misma URL local.

La URL local por defecto queda en:

`http://127.0.0.1:18913/`

Tambien puedes elegir puerto:

```powershell
.\build\src\platform\NisojeStudio.exe --ui --ui-port 18910
```

### TikTok real

Con `panel_config.json` en modo `external`, la ruta mas directa es:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\bridge_py\start_real_session.ps1 -User cocadevidrio80
```

Si prefieres lanzar solo el runner:

```powershell
python tools/bridge_py/run_tiktok_bridge.py --user cocadevidrio80
```

### Resolucion del runtime del bridge

El runner externo del bridge ya no depende de rutas hardcodeadas al proyecto anterior.

Resolucion del ejecutable Python:

1. `LIVEPANEL_TIKTOK_PYTHON_EXE`
2. `tools/bridge_py/python_runtime/python.exe`
3. `tools/bridge_py/.venv/Scripts/python.exe`
4. `python` en `PATH`

Resolucion del script runner:

1. `LIVEPANEL_TIKTOK_RUNNER_SCRIPT`
2. `tools/bridge_py/run_tiktok_bridge.py` relativo al root del repo o del package portable

`tools/bridge_py/start_real_session.ps1` mantiene ademas el override opcional `-PythonExe`, pero por defecto usa la misma resolucion anterior.

El package portable de Windows copia `tools/bridge_py` y genera `tools/bridge_py/python_runtime` con el Python embebido oficial para Windows mas los `site-packages` del bridge. La `.venv` local sigue siendo un entorno soportado para desarrollo y como fuente de build del runtime portable, pero ya no se distribuye dentro del package final. Logs, inboxes, tests y caches del bridge quedan fuera del package.

### Resolucion de panel_config.json

Cuando `NisojeStudio.exe` arranca sin un path de config explicito, primero busca `panel_config.json` junto al ejecutable. Si no existe ahi, cae al `cwd` actual. Esto permite que el portable funcione incluso al lanzar el `.exe` directo desde otra carpeta, sin romper el flujo de desarrollo desde el root del repo.

### Arena Live como juego externo

`Arena Live` se detecta desde la carpeta local de juegos, no desde el repo del panel:

- root por defecto: `%USERPROFILE%\Desktop\Juegos`
- modulo esperado: `%USERPROFILE%\Desktop\Juegos\Arena Live`
- override opcional: `NLP3_LOCAL_GAMES_ROOT`

El panel descubre `module_manifest.json`, activa el juego externo desde la UI con un click y usa `tools/game_bridge_py/run_local_game_bridge.py` para:

- lanzar el juego real desde su carpeta externa
- enviar `like`, `avatar` y `gift` al inbox del juego
- leer estado/logs internos y devolverlos al panel

Launcher manual del bridge del juego:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\game_bridge_py\start_local_game_bridge.ps1 -GameRoot "$env:USERPROFILE\Desktop\Juegos\Arena Live"
```

Smoke operativo real:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke_arena_live_integration.ps1
```

Documentacion tecnica completa:

- `docs/ARENA_LIVE_PANEL_INTEGRATION.md`
- `docs/AUDIT_ARENA_LIVE_2026-04-09.md`
- `tools/game_bridge_py/templates/external_game_module/README.md`
- `tools/game_bridge_py/audit_external_game_contract.py`

## Preparacion inicial

### Windows / PowerShell
1. Abre esta carpeta en VS Code.
2. Ejecuta:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\preflight_windows.ps1`
3. Revisa el reporte.
4. Luego ejecuta:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap_windows.ps1`

### Linux / macOS
1. Abre esta carpeta en VS Code.
2. Ejecuta:
   - `bash ./scripts/preflight_unix.sh`
3. Revisa el reporte.
4. Luego ejecuta:
   - `bash ./scripts/bootstrap_unix.sh`

## Archivos clave

- `AGENTS.md` -> contrato principal para el agente
- `agents/definitions/AGENT_MAP.md` -> mapa operativo de agentes y responsabilidades
- `agents/routing/ROUTING_POLICY.md` -> reglas de asignacion y escalamiento
- `skills/SKILL_CATALOG.md` -> catalogo de skills compartidas por flujo y stack
- `CHANGELOG.md` -> historial de cambios versionable
- `ROADMAP.md` -> fases operativas y de producto
- `CONTRIBUTING.md` -> reglas de ramas, commits, validacion y backups
- `docs/releases/RELEASE_POLICY.md` -> politica de versionado, artefactos e instaladores
- `docs/runbooks/BACKUP_POLICY.md` -> politica de respaldos del proyecto
- `docs/WORKING_CONTRACT.md` -> reglas operativas detalladas
- `docs/TOOLS_AND_DEPENDENCIES.md` -> herramientas requeridas
- `docs/HOST_ARCHITECTURE_BASELINE.md` -> estado tecnico del host
- `README_panel_ui.md` -> arquitectura, endpoints y uso de la UI embebida
- `README_tts.md` -> backend TTS real, voces, templates, API y despliegue Windows
- `README_embedded_ui.md` -> ventana nativa WebView2, dependencias y fallback
- `prompts/KICKOFF_PROMPT.md` -> prompt de arranque para el agente
- `.github/copilot-instructions.md` -> instrucciones de trabajo dentro del repo
- `tools/bridge_py/REAL_LOCAL_RUNBOOK.md` -> pasos concretos para operar TikTok real o eventos simulados
- `tools/bridge_py/README_bridge.md` -> documentacion completa del bridge TikTok modular

## Operacion con agentes, releases y respaldos

El repo ahora tiene una primera capa operativa no invasiva:

- 7 roles principales para orientar el trabajo: Orchestrator, C++ Engineer, Python Engineer, Frontend Web Engineer, Node.js Engineer, QA Engineer y Release Manager.
- catalogo de skills compartidas para intake, Git, testing, changelog, releases, backups e incidentes
- politica SemVer y manifiesto JSON por release
- runbook de backup y restore test
- scripts base:
  - `scripts/backup/create_project_backup.ps1`
  - `scripts/release/new_release_manifest.ps1`

Respaldo diario de codigo:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\backup\create_project_backup.ps1 -Mode code
```

Respaldo operativo completo:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\backup\create_project_backup.ps1 -Mode full
```

## Producto local actual

- `--console` mantiene el loop local del panel y autoarranca el WS `external` si corresponde.
- `NisojeStudio.exe` ya abre una ventana nativa embebida con WebView2 cuando la UI embebida esta habilitada.
- `--ui` sigue usando la misma UI HTTP local, ahora hosteada dentro de la ventana nativa o con fallback browser si WebView2 no esta disponible.
- La UI embebida ya ofrece feed realtime, control de juego, control de host AI, selector de juegos y metricas con sparklines.
- La UI consulta estado, diagnosticos, actividad, runner, bridge, TTS y juego del panel real, y ya puede guardar voces, filtros y templates TTS.
- El bridge Python puede empujar eventos live reales o simulados al mismo pipeline local.
- `tools/bridge_py/run_tiktok_bridge.py` ya puede correr en modo live, replay o burst sintetico, con config YAML, logs JSON y endpoints `/health`, `/status` y `/metrics`.
- El TTS real ya usa el backend Windows SAPI del host actual, con voces ingles instaladas por defecto y soporte documentado para instalar voces de espanol.
- `tools/bridge_py/setup_windows_bridge_env.ps1` crea una `.venv` local del bridge para desarrollo y para construir el `python_runtime` redistribuible del package Windows.
