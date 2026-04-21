# TikTok Live Bridge

Bridge Python product-ready para Nisoje Studio.

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
- Si `TikTokLive` no esta en el entorno activo, el runner puede reutilizar el bridge legado via `--legacy-bridge-root`.
- Cuando el runner se lanza desde el panel, `bridge runner stop` intenta primero un shutdown limpio por `POST /shutdown` y solo cae a terminacion forzada si el proceso no responde.
