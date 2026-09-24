# TikTok Live Bridge

Bridge Python product-ready para Nisoje Studio. Soporta tres proveedores:
- **tiktools** (recomendado): wss://api.tik.tools — requiere API key
- **euler** (comunidad): wss://ws.eulerstream.com — requiere JWT API key
- **direct** (experimental): TikTokLive directo — sin API key

## Instalacion

```bash
cd tools/bridge_py
python -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

En Windows PowerShell:

```powershell
cd tools/bridge_py
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

## Runtime soportado

- `tools/bridge_py/.venv` es el runtime local recomendado del bridge.
- El panel y `start_real_session.ps1` resuelven Python en este orden:
  `LIVEPANEL_TIKTOK_PYTHON_EXE` -> `tools/bridge_py/.venv/Scripts/python.exe` -> `python` en `PATH`.
- El script runner se resuelve con:
  `LIVEPANEL_TIKTOK_RUNNER_SCRIPT` -> `tools/bridge_py/run_tiktok_bridge.py`.
- Si necesitas reutilizar otro runtime compatible para `TikTokLive`, usa `LIVEPANEL_LEGACY_BRIDGE_ROOT` o `--legacy-bridge-root`. Ya no existe fallback hardcodeado al proyecto anterior.

## Arranque

### tik.tools (modo real)

El panel solicita la API key al conectar y la pasa al proceso del bridge; no la
copies en `bridge_config.yaml` ni en logs. Para ejecutar el bridge de forma
manual, define `LIVEPANEL_BRIDGE_API_KEY` (genérico) o `LIVEPANEL_TIKTOOLS_API_KEY` (legacy) temporalmente, o usa `--api-key`:

```powershell
$env:LIVEPANEL_BRIDGE_API_KEY = "tk_..."
.\.venv\Scripts\python.exe .\run_tiktok_bridge.py --user tuusuario --provider tiktools
Remove-Item Env:LIVEPANEL_BRIDGE_API_KEY
```

### Pool de API keys y rotacion automatica

El panel guarda varias credenciales en la bóveda local y las escribe en un JSON
transitorio que se pasa al bridge con `--api-keys-file`:

```powershell
.\.venv\Scripts\python.exe .\run_tiktok_bridge.py --user tuusuario --provider tiktools --api-keys-file "C:\Users\...\AppData\Local\NisojeStudio\run\bridge-api-keys.json"
```

Formato: `{"keys": [{"label": "cuenta 1", "value": "tk_..."}, ...]}`.

Cuando tik.tools cierra la sesion por cuota o plan (`4401` evaluation period,
`4429` demo, `4555` daily demo), el bridge:

1. clasifica el cierre como `API_SESSION_ENDED` (accion `rotate_key`);
2. pone la credencial en cuarentena y sigue con la siguiente del pool, sin
   reiniciar el proceso ni gastar el presupuesto de reintentos;
3. si todas agotaron, espera hasta `retry_policy.all_keys_cooldown_max_minutes`;
   y si no hay otra key que rotar corta ya con un mensaje accionable
   (no quema `max_attempts` reintentando con la misma credencial muerta).

### Euler Stream (comunidad)

Euler requiere una API key en formato JWT (3 partes separadas por puntos). La key se obtiene desde https://eulerstream.com/.

```powershell
$env:LIVEPANEL_BRIDGE_API_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
.\.venv\Scripts\python.exe .\run_tiktok_bridge.py --user tuusuario --provider euler
Remove-Item Env:LIVEPANEL_BRIDGE_API_KEY
```

### Directo (TikTokLive experimental)

No requiere API key, pero puede ser menos estable.

```bash
python run_tiktok_bridge.py --user tuusuario --provider direct
```

Si el proveedor cierra con código de error específico (ej. `4401`/`4429` en tiktools, `4429`/`4404` en Euler), no es una caída de red: la sesión alcanzó el límite de plan/demo o de WebSockets. Con un pool de credenciales el bridge rota a la siguiente key y sigue; sin pool, corta ahí mismo para evitar un bucle y muestra el diagnóstico (cierra las sesiones duplicadas o usa una key con cuota disponible).

WS directo al panel:

```bash
python run_tiktok_bridge.py --user alice
```

JSONL:

```bash
python run_tiktok_bridge.py --user alice --output session.jsonl
```

Inbox:

```bash
python run_tiktok_bridge.py --user alice --inbox ./live_inbox --session-name live-demo
```

Replay:

```bash
python run_tiktok_bridge.py --replay ./session.jsonl --replay-speed 1.0
```

Burst sintetico:

```bash
python run_tiktok_bridge.py --simulate-burst 500
```

## Arquitectura

```text
TikTokLive / replay / burst
        |
ConnectionManager + SessionSupervisor + HeartbeatMonitor
        |
EventNormalizer -> CanonicalEvent
        |
AsyncEventDispatcher
   |         |          |            |
 panel ws   jsonl      inbox     broadcast ws
        |
    Panel C++
```

## Endpoints locales

- `GET /health`
- `GET /status`
- `GET /metrics`
- `POST /replay/start`
- `POST /replay/stop`
- `POST /shutdown`

## Backpressure

- Default queue size: `8192`
- Default overflow policy: `drop_oldest`
- Override with `LIVEPANEL_BRIDGE_BUFFER_SIZE` or `buffer.size` in `bridge_config.yaml`

## Troubleshooting

- `USER_NOT_FOUND`: revisa que `--user` sea el username exacto.
- `NOT_LIVE`: la cuenta no esta en vivo ahora mismo.
- `ACCESS_BLOCKED` o `RATE_LIMIT`: TikTok o el servicio de firmado rechazaron la sesion.
- `INVALID_API_KEY` / `INVALID_JWT`: API key invalida o JWT malformado (Euler).
- `API_SESSION_ENDED`: la key agotó su cuota o venció su plan (códigos `4401`, `4429`, `4555`). Con pool se rota a la siguiente credencial; sin pool, revisa o reemplaza la key en el panel.
- `BOOTSTRAP_FAILED`: falta dependencia `websockets` (instalar `requirements.txt`).
- Si `TikTokLive` no esta en el entorno activo, el runner puede reutilizar el bridge legado via `--legacy-bridge-root`.
- Cuando el runner se lanza desde el panel, `bridge runner stop` intenta primero un shutdown limpio por `POST /shutdown` y solo cae a terminacion forzada si el proceso no responde.

## Rate Limiting por Proveedor

El bridge limita reintentos a **10 por hora por proveedor** (configurable con `retry_policy.max_reconnect_per_hour`). Si tiktools falla 10 veces/hora, Euler puede seguir reintentando independientemente.

## Heartbeat y Silence Timeout

- `connection.heartbeat_interval_sec`: intervalo de ping WebSocket (default 15s)
- `connection.heartbeat_warning_after_sec`: alerta si no hay eventos (default 60s)
- `connection.silence_timeout_sec`: desconectar si no hay eventos por N segundos (0 = auto = warning * 2)

Aplicable a todos los proveedores (tiktools, euler, direct).

## Variables de Entorno

| Variable | Descripción |
|---|---|
| `LIVEPANEL_BRIDGE_API_KEY` | API key genérica (tiktools, euler) — **recomendada** |
| `LIVEPANEL_TIKTOOLS_API_KEY` | Legacy, solo tiktools |
| `LIVEPANEL_TIKTOK_PROVIDER` | Proveedor por defecto: `tiktools`, `euler`, `direct` |
| `LIVEPANEL_TIKTOK_USER` | Usuario TikTok por defecto |
| `LIVEPANEL_TIKTOK_ROOM_ID` | Room ID opcional |
| `LIVEPANEL_TIKTOK_CONNECT_TIMEOUT_SEC` | Timeout conexión (default 20s) |
| `LIVEPANEL_BRIDGE_HEARTBEAT_INTERVAL_SEC` | Heartbeat interval (default 15s) |
| `LIVEPANEL_BRIDGE_HEARTBEAT_WARNING_AFTER_SEC` | Warning tras Ns sin eventos (default 60s) |
| `LIVEPANEL_BRIDGE_SILENCE_TIMEOUT_SEC` | Desconectar tras Ns silencio (0=auto) |
| `LIVEPANEL_BRIDGE_SOUND_ALERTS_ENABLED` | `false` desactiva las alertas sonoras (default `true`) |
| `LIVEPANEL_BRIDGE_SOUND_ALERTS_REQUIRE_PANEL` | `false` emite las alertas aunque no haya panel (default `true`) |

## Alertas sonoras

El bridge pita en los cambios de estado de conexión (dos tonos ascendentes al
conectar, tres descendentes al desconectar, un tono al reconectar).

**Sin panel conectado no suenan.** La alerta existe para avisar al operador, y el
operador está mirando el panel: si el panel no está, el pitido no tiene
destinatario. Esto importa porque el bridge **sobrevive al panel a propósito**
(resiliencia ante reinicios, con buffer de eventos), así que antes se quedaba
pitando en cada intento de reconexión con el panel ya cerrado. Cuando se silencia,
queda contabilizado en la métrica `sound_alerts_suppressed_total`.

Comportamiento por defecto, en `bridge_config.yaml`:

```yaml
sound_alerts:
  enabled: true
  require_panel: true
```

Alternativas:

```bash
# Silencio total, pase lo que pase
python run_tiktok_bridge.py --no-sound-alerts ...

# Aviso incondicional (comportamiento anterior: pita aunque no haya panel)
python run_tiktok_bridge.py --sound-alerts-without-panel ...
```

Si lo que quieres es que el bridge **no sobreviva** al panel, la vía soportada es
`POST /shutdown` en el puerto de control — que es lo que hace el panel al cerrarse
(`ExternalBridgeRunner::stop`).

## Validación de Entorno

Ejecuta la verificación de entorno (usada por el panel al arrancar):

```bash
python bridge_env_check.py --format text
```

Incluye checks de:
- Runtime Python y dependencias
- DNS resolution para endpoints de tiktools y Euler
- Formato de API key (JWT para Euler)
- Configuración de panel_config.json
