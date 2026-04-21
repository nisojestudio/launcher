# Contributing

## Working Contract

Before changing code, read:
- `README.md`
- `docs/WORKING_CONTRACT.md`
- `docs/ARCHITECTURE_START.md`
- directly related files

Follow:
- `AGENTS.md`
- `agents/definitions/AGENT_MAP.md`
- `agents/routing/ROUTING_POLICY.md`
- `skills/SKILL_CATALOG.md`

## Branches

Recommended:

```text
main
develop
feature/<name>
fix/<name>
release/<version>
hotfix/<version>
```

Current local branch may still be `master`. Do not rename branches without an explicit Git migration task.

## Commits

Use scoped conventional messages:

```text
feat: add live event replay view
fix: correct bridge timestamp handling
docs: add backup runbook
test: cover tts queue policy
build: update windows package script
release: prepare v0.2.0
```

## Validation

Pick validation by affected surface:

- C++: `cmake --build --preset release`, relevant `ctest`.
- Python bridge: `python -m unittest discover -s tools/bridge_py/tests -t tools/bridge_py -v`.
- Game bridge: `python -m unittest discover -s tools/game_bridge_py/tests -t tools/game_bridge_py -v`.
- UI assets: build preset or package flow that regenerates embedded assets.
- Installer: `scripts/build_windows_installer.ps1`, checksum verification, clean VM when release-bound.

Never say "validated" unless the command was actually run.

## Docs

Update docs when:
- a workflow changes
- a public behavior changes
- release/backup behavior changes
- a decision affects multiple modules

Use ADRs for decisions that are hard to reverse.

## Backups

Before risky work or release:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\backup\create_project_backup.ps1 -Mode full
```

For daily code backup:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\backup\create_project_backup.ps1 -Mode code
```
