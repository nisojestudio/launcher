# Goal
- Hacer que el overlay timer funcione en TikTok Live Studio via Cloudflare Tunnel automático

## Constraints & Preferences
- El panel debe ser autosuficiente — sin OBS, sin programas adicionales, sin config del usuario
- TikTok Live Studio solo acepta URLs HTTPS con dominio público (rechaza 127.0.0.1, localhost, IP LAN)
- Internet requerido aceptable (el panel ya necesita internet para TikTok)
- URL cambiante por sesión aceptable (es un contador extensible por evento)
- cloudflared.exe debe ir empaquetado en el installer para que funcione post-instalación

## v0.2.1 — Overlay via Cloudflare Tunnel
### Done
- Creado `CloudflareTunnelService` (`src/platform/cloudflare_tunnel_service.hpp` + `.cpp`): spawn cloudflared como subproceso, parsea stdout para extraer URL `https://<random>.trycloudflare.com`
- `panel_snapshot.hpp`: + `overlay_tunnel_url` en `PanelTimerStatus`
- `panel_app.hpp`: + `unique_ptr<CloudflareTunnelService> tunnel_service_`
- `panel_app.cpp:start_http_ui()`: inicia tunnel tras arrancar servidor HTTP; `stop_http_ui()`: mata tunnel; `snapshot()`: incluye `overlay_tunnel_url`
- `panel_http_json.cpp`: serializa `overlayTunnelUrl` en JSON del snapshot
- `app.js`: Copy URL usa `_timerOverlayUrl` (cacheado del snapshot) en lugar de `window.location.origin`
- `CMakeLists.txt`: + `cloudflare_tunnel_service.cpp`
- `package_windows.ps1`: descarga cloudflared.exe (desde GitHub releases), lo copia a `tools/cloudflared/` en el package
- Build 55/55 exitoso, tests 31/31 passed
- Package regenerado con cloudflared incluido + installer + manifest
- Assets v0.2.1 re-subidos a GitHub Release (portable 34MB, installer 241MB)

### In Progress
- (none)

### Blocked
- (none)

## Key Decisions
- Cloudflare Tunnel anónimo (TryCloudflare) en vez de tunnel con dominio fijo: evita configurar cuenta Cloudflare, la URL aleatoria es aceptable para el use case
- cloudflared empaquetado en `tools/cloudflared/` dentro del installer — el Inno script ya incluye `tools\*` automáticamente
- `INADDR_LOOPBACK` se mantiene (servidor solo en 127.0.0.1), cloudflared hace de puente — más seguro que abrir el servidor a la LAN
- v0.2.1 en lugar de v0.2.2 porque se rebuildearon los mismos assets con cloudflared añadido (no hay breaking changes)

## Next Steps
1. **Probar en TikTok Live Studio** — instalar/ejecutar el panel v0.2.1, iniciar overlay, copiar URL del tunnel, pegarla en TikTok Live Studio como Browser Source
2. Si funciona, mergear a master
3. Si no funciona, debug: revisar logs de cloudflared, probar SOFWERX/SAM (alternativas a cloudflared)

## Critical Context
- cloudflared.exe ~15MB, descargado una sola vez a `build/installer_cache/`, re-empaquetado en cada build
- La URL cambia cada vez que se reinicia el tunnel (nueva sesión del panel)
- El overlay_url local (localhost) se mantiene como fallback cuando el tunnel no está disponible
- El servidor HTTP sigue bindeado a INADDR_LOOPBACK (seguro), cloudflared expone solo lo necesario

## Relevant Files
- `src/platform/cloudflare_tunnel_service.hpp`: Interfaz del servicio de túnel
- `src/platform/cloudflare_tunnel_service.cpp`: Spawn + parseo de cloudflared
- `src/platform/panel_app.cpp:1346-1362`: `start_http_ui()` / `stop_http_ui()` con integración del tunnel
- `src/platform/panel_app.cpp:962-966`: Inclusión de `overlay_tunnel_url` en snapshot
- `src/platform/ui/app.js:3099`: Copy URL usa tunnel URL cacheada
- `scripts/package_windows.ps1:913-923`: Descarga y empaquetado de cloudflared.exe
