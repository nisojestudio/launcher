# Panel Live 3.0 TikTok Clean-Install Audit

Date: 2026-04-20

## Objective

Cerrar la brecha entre "instala" y "queda operativo" en Windows limpio, con foco en la integracion TikTok.

## Evidence Reviewed

- `README.md`
- `docs/WORKING_CONTRACT.md`
- `docs/ARCHITECTURE_START.md`
- `docs/TOOLS_AND_DEPENDENCIES.md`
- `tools/bridge_py/*.py`
- `scripts/package_windows.ps1`
- `scripts/build_windows_installer.ps1`
- `installer/panel_live.iss`
- `tools/bridge_py/logs/*.jsonl`
- `reports/panel-live-clean-install-vm-protocol.md`

## Primary Finding

La causa raiz mas probable para una maquina limpia con instalacion en `C:\Program Files\Panel Live` no es la ausencia de Python global. El instalador ya copia `tools\bridge_py\python_runtime` y el runtime empaquetado importa `TikTokLive`, `websockets`, `yaml`, `ssl` y `certifi`.

La dependencia oculta mas probable es de permisos de escritura:

- el bridge TikTok registraba por defecto en `tools/bridge_py/logs/bridge.jsonl`
- esa ruta es relativa al `cwd`
- al arrancar desde `Program Files`, un usuario estandar no puede crear `tools\bridge_py\logs`
- el instalador valida el runtime como admin, por lo que esa condicion no quedaba expuesta durante la instalacion

Resultado esperado del fallo:

- la app instala
- el host abre
- la validacion de install-time pasa
- el runner TikTok falla al arrancar o deja un error poco claro al primer uso real

## Secondary Findings

- la validacion post-instalacion actual verifica importacion del runtime, pero no una prueba TikTok real ni una prueba forense con evidencia util
- el soporte actual exporta logs, pero no priorizaba el nuevo destino adecuado para logs por usuario
- los logs historicos muestran mas fallos de sesion (`STREAM_DISCONNECTED`) y usuario invalido que fallos por ausencia de dependencias Python, lo que refuerza que el problema principal ya no es "instalar Python global"

## Corrective Actions Applied

- `tools/bridge_py/structured_logging.py`
  - fallback automatico de logs a `%LOCALAPPDATA%\NisojeStudio\logs`
  - descripcion explicita de ruta pedida, ruta efectiva y si hubo fallback
- `tools/bridge_py/bridge_env_check.py`
  - verifica OpenSSL, bundle de certificados, DLLs criticas del runtime y ruta efectiva de logs
- `src/platform/support_bundle_exporter.cpp`
  - debe captar el log del bridge desde la ruta por usuario
- `tools/bridge_py/test_tiktok_connection.py`
  - probe forense de conexion TikTok con reporte JSON y codigos de salida utiles
- `tools/bridge_py/validate_post_install.ps1`
  - validacion post-instalacion con evidencia
- `tools/bridge_py/healthcheck_panel_live.ps1`
  - healthcheck operativo del panel y del bridge
- `scripts/install_windows_prereqs.ps1`
  - instalacion/reparacion silenciosa de VC++ y WebView2
- `scripts/bootstrap_windows.ps1`
  - bootstrap real de workstation para build/release
- `scripts/start_panel_live.ps1`
  - arranque correcto con `LIVEPANEL_BRIDGE_LOG_PATH` en ruta por usuario

## Validation Executed

- runtime empaquetado `dist\NisojeStudio\tools\bridge_py\python_runtime\python.exe`
  - `bridge_env_check.py --format json` -> `ok=true`
  - version Python -> `3.14.3`
  - OpenSSL -> `3.0.18`
- prueba local de permisos
  - crear carpeta en `C:\Program Files\...` como usuario actual -> `Access denied`
- prueba local del runtime empaquetado
  - `run_tiktok_bridge.py --simulate-burst 3 --max-seconds 5 --no-broadcast-ws` -> `success`

## Residual Risk

La certificacion final sigue abierta hasta ejecutar el protocolo en Windows 10 y Windows 11 limpios con un usuario TikTok real en vivo.
