# Local Python Bridge Tools

This folder now contains a modular TikTok Live bridge ready for product-style local operation:
- `run_tiktok_bridge.py` -> live bridge runner
- `setup_windows_bridge_env.ps1` -> local Windows bridge virtualenv bootstrap
- `bridge_config.yaml` -> bridge runtime config
- `requirements.txt` -> Python dependencies
- `Dockerfile` -> container packaging
- `run_bridge.sh` -> container/local Unix launcher
- `README_bridge.md` -> full bridge-specific documentation
- `sample_events.py` -> simulated event generator
- `start_real_session.ps1` -> one-command local Windows session (panel + real runner)

Supported event types:
- `chat`
- `like`
- `gift`
- `follow`
- `share`
- `viewer_join`
- `viewer_count`
- `live_start`
- `live_end`
- `moderation`
- `custom_raw`

Control/status endpoints exposed by the Python bridge:
- `GET /health`
- `GET /status`
- `GET /metrics`
- `POST /replay/start`
- `POST /replay/stop`

## Local bridge environment

On a clean Windows machine you can prepare a local bridge environment with:

`powershell -ExecutionPolicy Bypass -File .\tools\bridge_py\setup_windows_bridge_env.ps1`

After that, panel-owned runner commands and `start_real_session.ps1` will prefer:

- `tools\bridge_py\.venv\Scripts\python.exe`
- `LIVEPANEL_TIKTOK_PYTHON_EXE` if you need an explicit override

The runner script path can also be overridden with:

- `LIVEPANEL_TIKTOK_RUNNER_SCRIPT`

If `TikTokLive` is missing from the active environment, the bridge can still reuse another compatible bridge runtime only when you opt in with `LIVEPANEL_LEGACY_BRIDGE_ROOT` or `--legacy-bridge-root`.

## Flow A: simulated inbox test

1. Generate inbox files:
   `python tools/bridge_py/sample_events.py --inbox tools/bridge_py/sample_inbox --session-name demo`
2. Make sure `panel_config.json` uses `"bridge_mode": "external"`.
3. Start the panel console:
   `.\build\src\platform\NisojeStudio.exe --console`
4. Process the inbox:
   `bridge demo inbox tools/bridge_py/sample_inbox 5 1000`

## Flow B: real TikTok -> inbox -> panel

1. Make sure `panel_config.json` uses `"bridge_mode": "external"`.
2. Start the panel console:
   `.\build\src\platform\NisojeStudio.exe --console`
3. Start the adapted TikTok bridge:
   `python tools/bridge_py/run_tiktok_bridge.py --user alice --inbox tools/bridge_py/live_inbox --session-name live-demo`
4. Process the inbox from the panel:
   `bridge demo inbox tools/bridge_py/live_inbox 20 1000`

## Flow C: real TikTok -> local WS -> panel

1. Make sure `panel_config.json` uses `"bridge_mode": "external"`.
2. Start the panel:
   `.\build\src\platform\NisojeStudio.exe --console`
   Or with UI:
   `.\build\src\platform\NisojeStudio.exe --ui`
3. The panel auto-starts the local external WS on port `8765`.
4. In a second terminal, start the adapted TikTok bridge:
   `python tools/bridge_py/run_tiktok_bridge.py --user alice`
   Or explicitly:
   `python tools/bridge_py/run_tiktok_bridge.py --user alice --ws ws://127.0.0.1:8765`
   For a short operational test with automatic stop:
   `python tools/bridge_py/run_tiktok_bridge.py --user alice --max-seconds 30`
5. Inspect the panel:
   `status`
   `activity`
   `bridge ws`
   `bridge external`
   `diagnostics`
   Or let the panel own the runner directly:
   `bridge target alice`
   `bridge runner start`
   Or for a bounded run:
   `bridge runner start alice 30`
   `bridge runner status`
   `bridge runner logs`
   `bridge runner stop`
   Optional setup helpers:
   `bridge target alice`
   `bridge ws port 8765`
   `bridge attach alice`
6. Note:
   `--ws` requires the optional Python package `websockets`.
   If no destination is provided, `run_tiktok_bridge.py` now defaults to `ws://127.0.0.1:8765`.

## Flow E: one-command real local session

1. From the project root, launch:
   `powershell -ExecutionPolicy Bypass -File .\tools\bridge_py\start_real_session.ps1 -User alice`
2. The launcher will:
   - persist `bridge_mode=external`
   - persist `external_target_user`
   - persist `external_ws_port`
   - open the panel console
   - start the real TikTok runner on the same local WS port
3. For a short bounded run:
   `powershell -ExecutionPolicy Bypass -File .\tools\bridge_py\start_real_session.ps1 -User alice -MaxSeconds 30`

## Flow D: simulated WS -> panel

1. Make sure `panel_config.json` uses `"bridge_mode": "external"`.
2. Start the panel console:
   `.\build\src\platform\NisojeStudio.exe --console`
3. In a second terminal, emit sample events:
   `python tools/bridge_py/sample_events.py --ws ws://127.0.0.1:8765`
4. Inspect:
   `status`
   `activity`
   `bridge ws`

Practical note:
- In console mode, the panel now keeps ticking while it waits for commands.
- The local external WS auto-starts on `8765` when the panel is running in `bridge_mode = "external"`.
- In UI mode, the panel serves a local product surface on `http://127.0.0.1:8080` by default.

## Optional JSONL path

- Generate sample JSONL:
  `python tools/bridge_py/sample_events.py --output tools/bridge_py/sample_session.jsonl`
- Capture real TikTok to JSONL:
  `python tools/bridge_py/run_tiktok_bridge.py --user alice --output tools/bridge_py/live_session.jsonl`
- Replay in the panel:
  `bridge replay tools/bridge_py/live_session.jsonl`

Current scope:
- the real runner can emit to `jsonl`, `inbox` and local `ws`
- the modular bridge exposes health, status and metrics endpoints
- replay and synthetic burst are built into the same runtime service
- connection/reconnect/heartbeat now live in dedicated bridge modules instead of one monolithic script

Troubleshooting:
- If `run_tiktok_bridge.py` fails with `USER_NOT_FOUND`, check that `--user` is the exact real TikTok username.
- The runner now normalizes `--user` from forms like `alice`, `@alice` or `https://www.tiktok.com/@alice`.
- If the output says `NOT_LIVE`, the user is not live right now; the runner can retry automatically with backoff.
- If the output says `ACCESS_BLOCKED`, `AGE_RESTRICTED` or `RATE_LIMIT`, the runner stops and prints a hint based on the legacy bridge diagnostics.
- For a short real-world validation without leaving the runner open, use `--max-seconds 30` or `--max-events 10`.
- The panel now reflects WS session state in `bridge external`, including `target_user`, `connection_state`, `current_room_id` and per-kind counters.
- The panel can now also persist `external_target_user` and `external_ws_port` in config, and `bridge attach <user>` prints the exact runner command for the current product setup.
- The panel can now also start and stop the real Python runner itself with `bridge runner start|stop|status`, so the local product flow no longer depends on launching Python manually every time.
- `bridge runner start [user] [max_seconds]` now supports bounded runs directly from the panel console, and `bridge runner logs` shows the latest launch/runtime lines captured from Python.
- `bridge runner stop` now asks the Python bridge to shut down through its local control endpoint before falling back to a hard process kill, so operator stops stay clean and visible in logs/status.
- `start_real_session.ps1` is the quickest daily entry point if you want product-style local use instead of typing panel + runner commands manually.
- The modular runner now also supports `--replay`, `--simulate-burst`, `--status-port`, `--broadcast-ws-port` and `--config`.
- `--max-seconds` is enforced even if the stream is quiet or still reconnecting.
- To verify the panel path first, use:
  `python tools/bridge_py/sample_events.py --inbox tools/bridge_py/live_inbox --session-name demo`

Extra:
- See `REAL_LOCAL_RUNBOOK.md` for a short end-to-end checklist on a real PC.
- See `README_bridge.md` for the bridge architecture, packaging and endpoint details.
