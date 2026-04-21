# External Game Module Template

Base template to extract a legacy live game and connect it to Nisoje Studio without touching the panel core.

## Expected module shape

```text
<Game Root>\
  module_manifest.json
  config\
    live_config.json
  runtime\
    inbox\
      events.jsonl
    status.json
    host.log.jsonl
```

The panel bridge also creates:

```text
<Game Root>\runtime\panel_bridge\
  inbox\panel_events.jsonl
  control\stop.flag
  state.json
  bridge.log.jsonl
```

## Base integration checklist

1. Extract only the game runtime, assets and host pieces from the old project. Do not copy the old panel shell.
2. Make the game boot standalone from its own folder before wiring it to the panel.
3. Create `module_manifest.json` from `module_manifest.template.json` and keep the contract paths explicit.
4. Make the game consume `config/live_config.json`, `runtime/inbox/events.jsonl`, `runtime/status.json` and `runtime/host.log.jsonl`.
5. Decide the bridge launch policy:
   `bridge.launch.passModuleRootArg=true` if the executable accepts `--module-root`.
   `bridge.launch.passModuleRootArg=false` if the legacy executable breaks on unknown flags.
   `bridge.launch.workingDirectory="launch_root"` if the executable needs its bundle folder as cwd.
6. Normalize or consume these incoming kinds from the panel: `join`, `chat`, `follow`, `share`, `like`, `gift`.
7. Treat avatar refresh as a `join` event with `data.avatarUpdate=true`.
8. Publish a runtime status shaped like `runtime/status.template.json`, especially `type`, `payload.data.roundState`, `payload.data.mode` and `payload.data.canonical`.
9. Run the audit before first connection:
   `python tools/game_bridge_py/audit_external_game_contract.py --game-root "<Game Root>"`
10. Launch the bridge manually for the first smoke:
   `powershell -ExecutionPolicy Bypass -File .\tools\game_bridge_py\start_local_game_bridge.ps1 -GameRoot "<Game Root>"`

## Incoming event contract

The bridge writes normalized JSONL lines to `runtime/inbox/events.jsonl`.

- `join`
  Fields: `user.id`, `user.name`, `user.avatarUrl`, `data.message`
- `chat`
  Fields: `data.message`, `data.comment`
- `follow`
  Fields: `data.count`
- `share`
  Fields: `data.count`
- `like`
  Fields: `data.count`
- `gift`
  Fields: `data.giftName`, `data.count`, `data.diamond`, `data.coins`

See `examples/panel_events.example.jsonl`.

## Runtime status contract

The panel bridge reads `runtime/status.json` and extracts:

- `type`
- `payload.ts`
- `payload.data.roundState`
- `payload.data.mode`
- `payload.data.canonical.ranking`
- `payload.data.canonical.feed`
- `payload.data.achievements`

If your game cannot emit the full canonical shape yet, keep at least `ranking` and `feed`.

## Surgical migration notes

- Keep all game-owned files inside the game root unless there is a hard reason not to.
- Prefer explicit paths in `communication` over relying on defaults.
- If the executable lives in `build\Release`, keep the module root at the game root and let the bridge resolve the bundle.
- Audit first, then run the panel. It is much faster to fix manifest and path mistakes before a live smoke.
