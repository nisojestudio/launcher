# Live Timer — Fase 3: publicación del overlay

Estado: **ejecutada y desplegada** (2026-09-20). Fases 1 y 2 ya estaban hechas,
verificadas y desplegadas (ver `CHANGELOG.md`, sección `Unreleased`).

Resultado de la Fase 3: `https://nisoje.com/overlay/live-timer` está en producción
como URL permanente del overlay, `POST`/`GET /api/overlay/session` está desplegado en
el Worker, el panel publica su túnel en un campo propio sin tocar `embedded_ui_url`, y
el túnel solo expone `/api/overlay/*` (verificado con curl sobre una URL pública real:
`/api/overlay/live-timer/state` → 200 y `/api/state` → 404). Detalles, desviaciones y
lo que falta por comprobar en tu equipo: `tasks.md`, sección «Resultado de ejecución».
El paso 4 se resolvió con un listener local «solo overlay» en vez de un Named Tunnel.

## Problema

TikTok LIVE Studio exige una URL **HTTPS pública** para su browser source: no acepta
`http://127.0.0.1` ni certificados locales. De ahí el túnel de Cloudflare.

El quick tunnel actual arrastra tres defectos medidos en la auditoría:

1. **La URL cambia en cada arranque.** Se escribe en `embedded_ui_url`
   (`panel_app.cpp`, callback de `start_tunnel`), y en el siguiente arranque eso decide el
   puerto local (`main.cpp:418-423` parsea `embedded_ui_url` y cae a 18913 porque la URL del
   túnel no es loopback). Resultado: el panel deja de recordar su puerto real y la
   configuración se corrompe sola.
2. **Expone el panel entero.** El túnel se abre sobre el mismo puerto que el panel HTTP
   (`panel_app.cpp:1855`), así que `/api/state`, licencia y métricas quedan accesibles sin
   autenticación (los GET no pasan por `request_requires_access`, que solo cubre POST).
3. **La ruta del overlay está cableada al túnel.** `tunnel_url_ = url + "/overlay/live-timer"`
   (`cloudflare_tunnel_service.cpp:315`): un servicio genérico conoce la ruta de un juego, así
   que un segundo módulo no puede tener su propia URL.

## Decisión arquitectónica

**Separar la URL que TikTok Studio carga (permanente) de la URL del túnel (efímera).**

```
TikTok Studio
   │  URL FIJA, configurada una sola vez
   ▼
https://nisoje.com/overlay/live-timer        ← página estática en Cloudflare Pages
   │  ¿cuál es mi panel ahora?
   ▼
https://<worker>/api/overlay/session         ← Worker: guarda la URL de túnel vigente
   ▲
   │  el panel publica su URL al arrancar (autenticado, con caducidad)
   │
Panel local ──quick tunnel──► /api/overlay/state   ← SOLO este path se publica
```

### Por qué esta y no otra

| Opción | Veredicto |
|---|---|
| Quick tunnel arreglado (URL estable en un campo propio) | Mejora el bug 1 y 3, **no** resuelve el 2 ni la reconfiguración manual |
| Named Tunnel + dominio propio | Resuelve 1, 2 (por reglas de ingress) y 3. Es la base de esta propuesta |
| **Named/Quick + página estática + Worker de sesión** | **Elegida**: la URL de TikTok Studio nunca cambia, así que se configura **una sola vez**. El túnel queda reducido a transportar JSON de estado |
| Terceros (ngrok reservado, Tailscale Funnel) | Válidos pero añaden dependencia de un SaaS; el dominio propio ya existe (`nisoje.com`) |
| Sin túnel | Descartado: TikTok Studio no acepta origen local |

Referencias del patrón «HTML estático + datos por canal aparte»:
- https://github.com/detekoi/static-browser-overlays
- https://github.com/filiphanes/web-overlays
- Ingress de túneles: https://developers.cloudflare.com/tunnel/features/locally-managed-tunnels/configuration-file/

## Hechos verificados (no repetir el trabajo)

- **`sitio` está en sync**: `main...origin/main` → ahead 0 / behind 0.
- El único cambio sin commitear que importaba, `catalog/latest.json` (diff de 2 líneas),
  **no se publica**: `catalog/` no está en `public/` ni aparece en `dist/`.
- `dist/` local es del **17/08**, anterior al último commit (18/09) → **construir fresco**,
  nunca reusar ese `dist/`.
- `vite.config.js` sin `publicDir`/`build` personalizados → Pages publica `src/` + `public/`.
- Despliegue: **Cloudflare Pages**, proyecto `nisojestudio`, `pages_build_output_dir: dist`.
  API aparte: `npm run deploy:api` → `wrangler deploy --config wrangler.api.jsonc`.
- Se publica en `https://nisoje.com`, que es además el `nisoje_api_base` de la autenticación
  y las licencias del panel: **un deploy republica el sitio entero**, no es aditivo.
- **No hay credenciales en `.env`**: solo variables `VITE_*` públicas. La autorización de
  despliegue es la sesión guardada de wrangler (`%APPDATA%\.wrangler`) + `CLOUDFLARE_ACCOUNT_ID`.
- `sitio/scripts/service-account-key.json` **no está expuesta**: gitignored en ambos repos
  (`.gitignore:14` en `sitio`, `sitio/` entero en el externo) y **nunca commiteada en ninguna
  rama**. No hay nada que arreglar y **no se debe rotar** — solo rompería
  `sync-firebase-users.mjs` y `batch-trial-licenses.mjs`.

## Riesgos

| Riesgo | Mitigación |
|---|---|
| `pages deploy` sobrescribe producción | Repo en sync (verificado). Construir fresco. Desplegar el Worker por separado, sin tocar Pages, en el primer paso |
| La página estática no conoce la URL del túnel | Worker de sesión (paso 2), con caducidad y autenticación |
| El endpoint de sesión filtra el túnel a cualquiera | Token de un solo uso firmado por el panel; caducidad corta; rate limit |
| Romper la autenticación/licencias del panel | El Worker es un añadido: no tocar rutas existentes (`/api/me/*`, `/api/license/*`) |
| El overlay depende de dos saltos de red | Mantener el modo local directo (`/overlay/live-timer` del panel) como fallback para OBS en la misma máquina |
