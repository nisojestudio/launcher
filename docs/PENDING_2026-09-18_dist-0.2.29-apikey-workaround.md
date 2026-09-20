# Resuelto: workaround API key en dist 0.2.28/0.2.29 (2026-09-20)

## Estado

**Resuelto en 0.3.0.** Este documento queda como registro del incidente.

## Qué pasó

El panel instalado (0.2.28) no conectaba el bridge TikTok: el sondeo previo
(`bridge_env_check.py`) devolvía `ok:false` con
`tiktools provider selected but no API key configured` porque el binario no le
pasaba la API key (`--api-key`) y el `bridge_config.yaml` empaquetado tenía el
campo vacío. `ExternalBridgeRunner::start()` abortaba con
`runner_start_failed` y el runner nunca arrancaba. El mensaje que veía el usuario
era "No se pudo iniciar el bridge Python. Verificá que Python y las dependencias
estén instaladas", que apuntaba al lugar equivocado.

## Solución en 0.3.0

1. La falta de credencial dejó de ser bloqueante: el sondeo la reporta como
   **aviso** (`warnings`) y el arranque continúa. `ok` sólo refleja problemas
   reales de runtime (Python, dependencias, archivos, DNS).
2. El runner ejecuta el sondeo **con** la credencial de la petición.
3. `/api/bridge/connect` devuelve `error` (código) + `message` (motivo real) +
   `runtimeSummary/Alerts/Warnings`, y la UI muestra ese motivo.
4. Las credenciales ya no dependen de archivos: viven cifradas con DPAPI y se
   entregan al runner por archivo transitorio (`--api-keys-file`), con rotación
   automática cuando el proveedor agota la cuota de una key.
5. El empaquetado del release sincroniza `tools/bridge_py/*.py` desde el repo
   (verificado en `dist/releases/0.3.0/NisojeStudio/tools/bridge_py`:
   `error_catalog.py`, `bridge_env_check.py` con `--api-key`,
   `run_tiktok_bridge.py` con `--api-keys-file`).

## Rollback

- Release anterior: `dist/releases/0.2.29/`.
- Instalación previa del equipo: `C:\Program Files\Panel Live` (0.2.28),
  reinstalable con `dist/releases/0.2.29/installer/panel-live-0.2.29-win-x64.exe`.
- Backup del código previo al release: `scripts/backup/restore_project_backup.ps1`
  con el snapshot `Panel live 3.0-code-2026-09-20_03-43-21`.
- Código: `git revert` de los commits `85dc85b`, `5845d49`, `957cc97`,
  `5332c40`, `1e489b0`, `e121035`, `13736cc`.

## Pendiente operativo

Instalar `dist/releases/0.3.0/installer/panel-live-0.3.0-win-x64.exe` en el
equipo requiere permisos de administrador (UAC).
