# Remote Games Worker

Artefactos operativos para publicar el catalogo remoto licenciado de juegos en Cloudflare Workers + R2.

## Archivos

- `worker.js`: Worker canonico para `/api/me/licenses`, `/api/me/games/catalog`, `/api/me/games/download` y el dashboard admin de usuarios/licencias
- `wrangler.toml.example`: plantilla base de despliegue
- `generate_r2_catalog.ps1`: genera `latest.json` a partir de los paquetes ZIP ya construidos

## Requisitos

- Cloudflare Worker con binding `DB` a D1
- Bucket R2 privado con binding `GAMES_CATALOG_BUCKET`
- secreto `DOWNLOAD_TOKEN_SECRET`
- variable `FIREBASE_API_KEY`
- variable `ADMIN_EMAIL`

Opcionales:

- `GAMES_CATALOG_OBJECT` para cambiar `catalog/latest.json`
- `GAME_DOWNLOAD_TTL_SECONDS` para ajustar la vida util de la URL firmada
- `INSTALLER_URL` para el enlace que se entrega en el panel web y en los correos
- secretos `RESEND_API_KEY` y `RESEND_FROM_EMAIL` para habilitar envio real de correos de activacion

## Flujo recomendado

1. Generar los paquetes de juegos en `C:\Users\Nisoje\Desktop\panel-live-master\juegos\_packages`
2. Ejecutar:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\remote_games_worker\generate_r2_catalog.ps1 `
  -PackagesRoot "C:\Users\Nisoje\Desktop\panel-live-master\juegos\_packages" `
  -OutputPath "C:\Users\Nisoje\Desktop\latest.json"
```

3. Subir a R2:
   - `catalog/latest.json`
   - `catalog/games/<game_id>/<version>/<package>.zip`
   - `catalog/games/<game_id>/<version>/<package>.sha256.txt`
4. Copiar `wrangler.toml.example` a `wrangler.toml`
5. Configurar bindings reales
6. Cargar el secreto:

```powershell
wrangler secret put DOWNLOAD_TOKEN_SECRET
wrangler secret put RESEND_API_KEY
wrangler secret put RESEND_FROM_EMAIL
```

7. Desplegar:

```powershell
wrangler deploy
```

## Smoke recomendado

1. `GET /api/me/licenses` con `Authorization: Bearer <idToken>`
2. `GET /api/me/games/catalog` con el mismo `Bearer`
3. `GET /api/admin/dashboard/users` con la cuenta admin autenticada
4. Crear una licencia desde `/api/admin/licenses/create`
5. Descargar desde el panel con una sesion autenticada

## Nota de seguridad

La descarga firmada ahora exige dos cosas a la vez:

- URL firmada vigente
- `Authorization: Bearer <idToken>` del mismo usuario que recibio esa URL

Eso reduce el riesgo de compartir una URL temporal fuera de la cuenta autenticada.
