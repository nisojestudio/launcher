# Panel Live 3.0 Audit + Installer Report

Date: 2026-04-11

## Objective

Completar el trabajo en tres pasos:

1. Auditar el proyecto y el backend desplegado.
2. Generar un nuevo instalador Windows con los cambios remotos integrados.
3. Entregar un informe completo con artefactos, validaciones y riesgos.

## Audit Summary

### Initial findings

- El Worker desplegado respondia en `https://nisoje-api.nisojestudio.workers.dev`.
- El endpoint `/api/db-test` devolvia `500` por falta de binding `DB` en runtime.
- El paquete Windows generado por `scripts/package_windows.ps1` no escribia `me_games_catalog_path` dentro del `panel_config.json` distribuido.
- El backend remoto ya tenia R2 activo y el endpoint `/api/admin/games/catalog` devolvia juegos, pero el catalogo publicado en R2 no estaba actualizado con los `manifest` enriquecidos generados localmente.

### Corrective actions applied

- Se agrego `me_games_catalog_path = "/api/me/games/catalog"` al `panel_config.json` generado por `scripts/package_windows.ps1`.
- Se agrego el binding D1 real a `tools/remote_games_worker/wrangler.toml`:
  - `binding = "DB"`
  - `database_name = "nisoje-db"`
  - `database_id = "dc6e836d-ab30-46af-b029-402a6f4f819e"`
- Se cargo un nuevo secreto `DOWNLOAD_TOKEN_SECRET` al Worker con `wrangler secret put`.
- Se subio el `catalog/latest.json` enriquecido al bucket R2 remoto `games-catalog`.
- Se redeployo el Worker con `wrangler deploy`.

## Files Changed

- `scripts/package_windows.ps1`
- `tools/remote_games_worker/wrangler.toml`
- `reports/panel-live-3.0-audit-installer-report-2026-04-11.md`

## Deployed Worker Validation

### Public checks executed

- `GET /api/status` -> `200`
- `GET /api/version` -> `200` con `v3-remote-games`
- `GET /api/db-test` -> corregido a `200` con `{"database":"connected","users_total":1}`
- `GET /api/me/licenses` sin identidad -> `400`
- `GET /api/me/licenses?firebase_uid=audit-temp-uid` -> `200` con `valid=false`
- `GET /api/me/games/catalog` con `Bearer invalid-token` -> `401`
- `GET /api/me/games/download` sin firma -> `400`
- `GET /api/admin/games/catalog?t=...` -> `200` con catalogo enriquecido y `manifest` para `arena_live` y `conquista`

### End-to-end backend smoke executed

Se creo una cuenta temporal de auditoria en Firebase, se registro en la API, se creo una licencia activa y se valido todo el flujo remoto:

- `me_licenses_valid=True`
- `me_licenses_count=1`
- `catalog_games=2`
- `downloaded_game=arena_live`
- `downloaded_bytes=49564819`

## Local Package / Host Validation

### Build + tests

Validaciones ejecutadas realmente:

- `cmake --build --preset release`
- `ctest --preset release --output-on-failure`

Resultado:

- `27/27` tests aprobados

### Packaged host smoke

Se levanto el panel empaquetado desde `dist/NisojeStudio/NisojeStudio.exe`, se uso una cuenta/licencia temporal nueva y se valido:

- `POST /api/auth/login` local -> `ok=true`
- `POST /api/game/download` local -> `ok=true`
- instalacion registrada en `%LOCALAPPDATA%\NisojeStudio\state\remote_game_installs.json`
- juego instalado en `%LOCALAPPDATA%\NisojeStudio\games\arena_live\20260411-030455`

## Installer Output

Artefactos generados:

- `dist/installer/PanelLive-3.0-Windows-x64-Setup.exe`
- `dist/NisojeStudio-portable.zip`
- `dist/SHA256SUMS.txt`

### Final distributed config

El `panel_config.json` final dentro de `dist/NisojeStudio` quedo con:

- `auth.required = true`
- `nisoje_api_base = "https://nisoje-api.nisojestudio.workers.dev"`
- `me_licenses_path = "/api/me/licenses"`
- `me_games_catalog_path = "/api/me/games/catalog"`

## Operational Notes

- El Worker ya quedo desplegado con:
  - D1 `nisoje-db`
  - R2 `games-catalog`
  - `DOWNLOAD_TOKEN_SECRET`
- El catalogo remoto actualizado ya esta en el bucket remoto.
- El endpoint admin sin query string mostro una respuesta vieja por cache en una prueba previa; con query string de cache-busting devolvio el catalogo actualizado. El flujo autenticado del panel no depende de esa ruta admin.

## Temporary Audit Data Created

Durante la validacion se crearon usuarios/licencias temporales de auditoria para no depender de credenciales de produccion.

No se elimino esa data automaticamente.

## Residual Risks

- No se valido login con una cuenta/licencia real de produccion del cliente final.
- No se ejecuto instalacion limpia del `Setup.exe` dentro de una VM nueva de Windows 10 y otra de Windows 11.
- Los juegos `external-webview-game` siguen exponiendo HTML/JS instalado en el cliente; el flujo reduce exposicion, pero no oculta el codigo al 100%.

## Recommended Next Step

Ejecutar un smoke final en una VM Windows limpia con tu cuenta/licencia real de produccion para cerrar validacion comercial, no solo tecnica.
