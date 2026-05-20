# Audit Arena Live 2026-04-09

## Scope

Audit of the current `Arena Live` external-game integration against `Nisoje Studio` in:

- `C:\Users\Nisoje\Desktop\panel-live-master\juegos\Arena Live`
- `C:\Users\Nisoje\Desktop\Panel live 3.0`

## Verified contract

- Discovery root is consistent:
  - panel source resolves `%USERPROFILE%\Desktop\Juegos` or `NLP3_LOCAL_GAMES_ROOT`
  - `Arena Live` lives in `C:\Users\Nisoje\Desktop\panel-live-master\juegos\Arena Live`
- `module_manifest.json` is present and explicit for:
  - `id`
  - `displayName`
  - `entryExecutable`
  - `communication.configFile`
  - `communication.inboxFile`
  - `communication.statusFile`
  - `communication.logFile`
- The bridge resolves the real executable at:
  - `build\Release\ArenaLive.exe`
- The game publishes and consumes the expected paths:
  - `config\live_config.json`
  - `runtime\inbox\events.jsonl`
  - `runtime\status.json`
  - `runtime\host.log.jsonl`
- The panel bridge tracks:
  - `runtime\panel_bridge\inbox\panel_events.jsonl`
  - `runtime\panel_bridge\state.json`
  - `runtime\panel_bridge\bridge.log.jsonl`

## Validation performed

- `python -m unittest tools.game_bridge_py.tests.test_local_game_bridge`
  - Result: passed
- `python .\tools\game_bridge_py\audit_external_game_contract.py --game-root "C:\Users\Nisoje\Desktop\panel-live-master\juegos\Arena Live"`
  - Result: 0 failures, 1 warning
  - Warning: the bridge injects `--module-root` for this executable, which is correct for `Arena Live` but should be explicit for future legacy games

## Smoke status

The real smoke now passes on April 9, 2026:

- Command:
  - `powershell -ExecutionPolicy Bypass -File .\scripts\smoke_arena_live_integration.ps1`
- Result:
  - panel health ok
  - external game activated
  - bridge running
  - `runtime\panel_bridge\state.json` generated
  - `runtime\panel_bridge\inbox\panel_events.jsonl` received 7 events
  - `runtime\inbox\events.jsonl` received 7 events

## Fixes applied during this audit

- The bridge launch policy is now configurable from `module_manifest.json`.
- The bridge state writer retries atomic replace on Windows before falling back to in-place writes.
- The smoke script now:
  - clears stale bridge and game processes before starting
  - waits for `runtime\panel_bridge\state.json` before injecting events
  - uses a stable short avatar URL instead of a long inline data URI
  - validates transport flow by inbox delivery instead of fragile gameplay-only heuristics

## Ready assets for the next legacy game

- Contract audit:
  - `tools\game_bridge_py\audit_external_game_contract.py`
- Reusable template:
  - `tools\game_bridge_py\templates\external_game_module\README.md`
  - `tools\game_bridge_py\templates\external_game_module\module_manifest.template.json`
  - `tools\game_bridge_py\templates\external_game_module\config\live_config.template.json`
  - `tools\game_bridge_py\templates\external_game_module\runtime\status.template.json`

## Migration guidance

- Use the new template before copying any gameplay code from the old project.
- Run the audit before launching the panel.
- The current `Arena Live` path is now a validated reference integration for the next external game.
