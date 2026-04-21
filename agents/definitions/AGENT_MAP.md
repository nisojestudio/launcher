# Panel Live Agent Map

## Purpose

This map defines the first operating model for agent-assisted development in Panel Live. It keeps the team small and focused: one accountable lead per task, with specialists used only when they add real value.

## Primary Agents

### Orchestrator / Tech Lead

Owns task intake, scope, routing, risk, and final integration.

Responsibilities:
- Read the required repo docs before code changes.
- Split work into small, auditable blocks.
- Assign the primary specialist role.
- Require validation evidence before calling work validated.
- Keep architecture decisions traceable.

Typical files:
- `AGENTS.md`
- `docs/`
- `specs/`
- cross-cutting changes across `src`, `tools`, `scripts`

### C++ Engineer

Owns host runtime, core, native bridge, memory, concurrency, CMake, and tests.

Typical files:
- `src/core`
- `src/live`
- `src/platform`
- `src/bridge`
- `src/tts`
- `tests/*.cpp`
- `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`

Required validation:
- Relevant C++ build.
- Relevant `ctest` target or full preset when risk is broad.

### Python Engineer

Owns bridge tooling, automation, diagnostics, validators, and helper scripts.

Typical files:
- `tools/bridge_py`
- `tools/game_bridge_py`
- Python scripts under `scripts`

Required validation:
- `python -m unittest discover` for affected Python package.
- Script parse or smoke run for PowerShell/Python utilities.

### Frontend Web Engineer

Owns embedded web UI, dashboard behavior, accessibility, and UI state.

Typical files:
- `src/platform/ui`
- UI asset generation paths
- UI-related docs

Required validation:
- Build or asset generation when embedded assets change.
- Browser or screenshot validation when layout/interaction changes materially.

### Node.js Engineer

Owns Node services, remote workers, JS tooling, and web/backend integration where Node is the runtime.

Typical files:
- `tools/remote_games_worker`
- future Node bridge/API services
- JS tests or worker config

Required validation:
- Node package tests or worker-specific smoke checks.
- API contract verification when endpoints change.

### QA Engineer

Owns test strategy, regression risk, release gates, and evidence quality.

Responsibilities:
- Define minimum validation for each change.
- Maintain smoke and regression checklists.
- Identify missing tests.
- Block claims of validation without executed evidence.

Typical files:
- `tests`
- `.github/workflows`
- `docs/runbooks`
- release validation reports

### Release Manager

Owns versioning, changelog, release manifests, installers, checksums, rollback, and backups.

Typical files:
- `CHANGELOG.md`
- `docs/releases`
- `scripts/release`
- `scripts/backup`
- `scripts/package_windows.ps1`
- `scripts/build_windows_installer.ps1`
- `installer`
- `dist` outputs

Required validation:
- Build/package commands listed in release policy.
- Checksum and manifest generation.
- Backup or rollback path identified.

## Secondary Agents

Use these after the primary workflow is stable:
- DevOps / Build Engineer: CI, toolchains, runners, caches.
- Documentation Engineer: user docs, runbooks, release notes.
- Security Reviewer: auth, secrets, downloads, WebSocket boundaries.
- Installer Packager: installer UX, signing, prerequisites, clean VM validation.

## Routing Matrix

| Task | Primary | Supporting |
| --- | --- | --- |
| Live event protocol change | Python Engineer | C++ Engineer, QA |
| Host runtime change | C++ Engineer | QA |
| Embedded UI change | Frontend Web Engineer | QA |
| Remote game catalog/worker | Node.js Engineer | Release Manager, QA |
| Installer/release artifact | Release Manager | QA, C++ Engineer |
| Backup/restore process | Release Manager | QA |
| Architecture decision | Orchestrator | Relevant specialist |
| Security-sensitive change | Security Reviewer | Orchestrator, QA |

## Completion Contract

Every completed task should report:
- Objective.
- Files changed.
- Decision made.
- Validation executed.
- Validation not executed and why.
- Risks.
- Suggested next block.
