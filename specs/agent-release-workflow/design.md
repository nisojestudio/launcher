# Technical Design

## Overview

This design adds an operating layer around the existing Panel Live codebase. It does not move product code. The layer documents who does what, which skills apply, how releases are versioned, and how backups are created and restored.

## Architecture

```text
Repo root
  AGENTS.md                    Existing master contract
  CHANGELOG.md                 Human-readable change history
  ROADMAP.md                   Phased operating roadmap
  CONTRIBUTING.md              Branch, commit, validation rules
  agents/
    definitions/AGENT_MAP.md   Role map and responsibilities
    routing/ROUTING_POLICY.md  Task routing rules
  skills/
    SKILL_CATALOG.md           Shared and stack skill catalog
  docs/
    adr/                       Decision records
    releases/                  Release policy and manifest schema
    runbooks/                  Backup and restore runbooks
  scripts/
    backup/                    Backup automation
    release/                   Release manifest automation
  specs/
    agent-release-workflow/    Requirements, design, tasks
```

## Decisions

1. Keep the current technical layout.
   `src/core`, `src/render`, `src/live`, `src/audio`, `src/platform`, `tools`, and `tests` already match the desired layered direction.

2. Use seven primary roles first.
   The initial roles are Orchestrator, C++ Engineer, Python Engineer, Frontend Web Engineer, Node.js Engineer, QA Engineer, and Release Manager.

3. Treat skills as project workflow docs first.
   The first version is a catalog, not a large set of executable Codex skills. Repeated workflows can become executable skills later.

4. Use `dist` for release outputs.
   Existing package and installer scripts already use `dist`, so the release policy builds on that instead of adding a competing `artifacts` folder.

5. Add backup scripts without making backups inside the repo by default.
   The default backup destination is the user's Desktop under `PanelLiveBackups`.

## Validation Strategy

- Documentation structure: verify expected files exist.
- PowerShell scripts: parse scripts with PowerShell parser.
- Git hygiene: verify only intended files are changed.
- Functional build/tests: not required for this docs/tooling layer, but existing release gates remain documented.

## Risks

- Process drift if the docs are not referenced during future tasks.
- Script behavior on very long paths or locked files may vary by workstation.
- Release manifest quality depends on passing the correct artifact paths.
- Too many formal gates too early can slow urgent debugging unless the Orchestrator chooses a smaller validation scope.
