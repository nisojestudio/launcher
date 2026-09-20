# Live Timer — Fase 3: tareas

Diseño y decisiones: `design.md`. Convención de tareas: criterios de aceptación
explícitos, verificación por tarea, y nada que toque más de ~5 archivos.

Ejecutar en orden. Los pasos 1 y 2 son **aditivos**: no modifican nada existente.

---

## Resultado de ejecución (2026-09-20)

T1–T5 ejecutados y verificados. 30 de 32 casillas marcadas; las dos sin marcar
requieren tu equipo de streaming (no se pueden comprobar desde aquí).

**Desviaciones respecto al plan, y por qué:**

1. **T4 no usa Named Tunnel.** En vez de montar un túnel con nombre (que exige un
   `cloudflared tunnel login` interactivo, credenciales de túnel y registros DNS en
   tu cuenta), el quick tunnel se apunta a un **segundo listener local "solo
   overlay"** en puerto efímero que sirve únicamente `/api/overlay/*`. Cumple las
   tres casillas de aceptación con menos superficie: no hay credenciales nuevas, no
   se toca DNS, y el objetivo del diseño ("el túnel queda reducido a transportar
   JSON de estado") queda satisfecho igual. Un Named Tunnel se puede añadir después
   apuntando a ese mismo puerto, sin tocar código.
2. **T5 despliega desde un directorio de staging, no con `wrangler pages deploy dist`
   a secas.** `wrangler pages deploy` desde la raíz del sitio **falla**: detecta el
   directorio `functions/` (que es de Firebase Cloud Functions) como Pages Functions
   e intenta bundlear `functions/node_modules/**/*.d.ts` → `Build failed with 57
   errors`. Se copió `dist/` a un directorio temporal sin `functions/` y se desplegó
   desde ahí (`wrangler pages deploy . --cwd <staging>`), que funcionó a la primera.
   **Esto afecta también a `.github/workflows/deploy-pages.yml`, roto por el mismo
   motivo.** Arreglo recomendado: sacar `functions/` de la raíz del repo del sitio.
3. **Se añadió una excepción de `X-Frame-Options` en `sitio/public/_headers` para
   `/overlay/*`.** La regla global del sitio pone `DENY`, que impide incrustar la
   página; un browser source puede cargarla en un iframe. Es aditivo: el resto del
   sitio conserva la cabecera.
4. **Se añadió CORS (`Access-Control-Allow-Origin: *`) al `GET
   /api/overlay/live-timer/state` del panel.** Sin eso la página estática no puede
   leer el estado desde otro origen.
5. **T3 tocó más archivos que los 5 del plan** (`panel_config.hpp`,
   `panel_config_storage.cpp`, `webview_host.{hpp,cpp}`, `panel_http_server.{hpp,cpp}`
   además de los previstos). El puerto local necesitaba un campo persistido propio:
   sin él, un `embedded_ui_url` corrompido sigue perdiendo el puerto real.

**Hallazgo aparte, importante para futuros builds del panel:**

`build/release/CMakeFiles/rules.ninja` contiene `msvc_deps_prefix = Nota: inclusi├│n
del archivo:` con la "ó" mal codificada, así que ninja no reconoce las líneas de
`/showIncludes` y **no registra ninguna dependencia de cabecera**. Los rebuilds
incrementales mezclan objetos con distinto layout de `PanelConfig` / `PanelApp` /
`PanelHttpServer` y los tests revientan con access violation: tras el primer build
incremental de esta fase fallaron **13 de 32** con `0xC0000005`, y con un rebuild
completo bajaron a 1 (un assert real, ya corregido) y después a 0. Se recuperó
borrando `.ninja_deps` y recompilando todo, sin reconfigurar ni tocar vcpkg.
Recomendado: configurar con `VSLANG=1033` (mensajes de MSVC en inglés) para que el
prefijo sea ASCII.

---

## T1 — Página estática del overlay

**Descripción.** Publicar el overlay como página estática en el sitio, con URL permanente.
Es un archivo **nuevo**: no se toca ninguna página existente.

**Criterios de aceptación:**
- [x] Existe `sitio/public/overlay/live-timer/index.html` y se sirve en `/overlay/live-timer/`
- [x] Carga con `?preview=1` y sin él
- [x] Sin la URL del panel disponible, no rompe: muestra estado de espera y reintenta
- [x] No incluye ninguna credencial ni URL de túnel cableada

**Verificación:**
- [x] `npm run build` y comprobar que el archivo aparece en `dist/overlay/live-timer/`
- [x] `npm run preview` y abrir la ruta; revisar que no hay errores en consola

**Dependencias:** ninguna.
**Archivos:** `sitio/public/overlay/live-timer/index.html` (nuevo).

---

## T2 — Endpoint de sesión en el Worker

**Descripción.** El panel publica ahí la URL de su túnel vigente; la página estática la
consulta. Con caducidad corta y autenticación, para que no sea un redirector abierto.

**Criterios de aceptación:**
- [x] `POST` autenticado publica `{tunnel_url, expires_at}` para el panel del operador
- [x] `GET` devuelve la URL vigente o `404`/`410` si no hay ninguna o está caducada
- [x] Una URL caducada no se sirve nunca
- [x] Sin token válido, el `POST` se rechaza
- [x] **No se toca ninguna ruta existente** (`/api/me/*`, `/api/license/*`)

**Verificación:**
- [x] Desplegar **solo el Worker** (`npm run deploy:api`), sin tocar Pages
- [x] Comprobar con `curl` que las rutas existentes del sitio siguen respondiendo igual

**Dependencias:** ninguna (se puede hacer antes que T1).
**Archivos:** `sitio/functions/` o `sitio/worker/` según dónde vivan las rutas actuales,
`sitio/wrangler.api.jsonc` si hace falta binding nuevo.

---

## T3 — El panel publica su URL de túnel

**Descripción.** Sustituir el comportamiento actual (escribir la URL en `embedded_ui_url`)
por publicarla en el endpoint de T2. Arregla el bug de corrupción de configuración y el
cableado de la ruta del overlay.

**Criterios de aceptación:**
- [x] Arrancar el panel 3 veces **no** modifica `embedded_ui_url` ni el puerto local
- [x] `embedded_ui_url` vuelve a significar solo «URL de la UI embebida»
- [x] La URL del túnel queda en un campo propio y no se persiste como si fuera config del usuario
- [x] Si la publicación en el endpoint falla, el panel sigue funcionando (no bloquea el arranque)
- [x] `cloudflare_tunnel_service` deja de conocer la ruta `/overlay/live-timer`

**Verificación:**
- [x] `ninja -C build/release` (con vcvars64 cargado) y `ctest --preset release`
- [x] Test nuevo: arrancar con un `panel_config.json` con URL de túnel y comprobar que el
      puerto local resuelto **no** cambia y que `embedded_ui_url` no se reescribe

**Dependencias:** T2.
**Archivos:** `src/platform/cloudflare_tunnel_service.{cpp,hpp}`, `src/platform/panel_app.cpp`,
`src/platform/main.cpp`, `tests/`.

---

## T4 — Ingress del túnel: exponer solo el overlay

**Descripción.** Con Named Tunnel, publicar únicamente el endpoint de estado del overlay.
Deja de exponerse `/api/state`, licencia y métricas.

**Criterios de aceptación:**
- [x] Desde la URL pública, `/api/overlay/*` responde y `/api/state` **no**
- [ ] El overlay sigue funcionando en TikTok Studio
- [x] Si no hay dominio configurado, queda el quick tunnel como fallback, con la URL en
      campo propio (sin tocar `embedded_ui_url`)

**Verificación:**
- [x] Prueba real: abrir la URL pública y comprobar con `curl` que `/api/state` no es alcanzable
- [x] Comprobar que el bridge **no** se ve afectado (puertos 8765-8800 y el de control intactos)

**Dependencias:** T3.
**Archivos:** `src/platform/cloudflare_tunnel_service.{cpp,hpp}`, config del túnel.

---

## T5 — Build y despliegue del sitio

**Descripción.** Publicar T1 en producción.

**Criterios de aceptación:**
- [x] `npm run build` **fresco** (nunca reusar el `dist/` del 17/08)
- [x] `npx wrangler pages deploy dist`
- [x] `https://nisoje.com` sigue sirviendo bien (portada, login, licencias)
- [x] `/overlay/live-timer/` responde 200 con HTTPS válido

**Verificación:**
- [x] Antes de desplegar: `git -C sitio status -sb` → confirmar `ahead 0 / behind 0`
- [x] Después: comprobar en el navegador la portada **y** la ruta nueva
- [ ] TikTok Studio: configurar la URL una vez y confirmar que el overlay se ve

**Dependencias:** T1 (y T2/T4 para que sea útil de verdad).
**Archivos:** ninguno en el repo; es despliegue.

---

## Fuera de alcance (Fase 4)

- Simplificar los 44 controles de configuración a ~15 visibles + panel «Avanzado».
- **El preview del overlay es inalcanzable con el ratón**: `#timer-preview-wrapper` tiene
  `hidden` (`index.html:252`) y solo lo quita el evento `toggle` del `<details>` que está
  **dentro** de ese contenedor oculto, así que el evento nunca se dispara. La única vía es la
  tecla `V` (`app.js:3961-3967`). Quitar el `hidden` es una línea y es el mayor retorno de
  toda la auditoría.
- `--text-dim` no existe (usada en 6 sitios de `styles.css` + 1 inline): las declaraciones son
  inválidas y el color cae al heredado.
- `is_our_port()` (`port_zombie_detector.cpp:76-81`) solo conoce 8765/8770/18913/8080, mientras
  el rango real es 8765-8800. Las constantes `kBridgePortRangeStart/End` ya están declaradas
  para ese fin y nunca se cablearon: por eso los cinco mecanismos de limpieza de puertos fallan
  ante el zombie real.
