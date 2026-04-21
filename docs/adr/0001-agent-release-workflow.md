# ADR 0001 - Agent And Release Workflow Layer

## Status

Accepted

## Date

2026-04-21

## Context

Panel Live already has a functional technical layout for C++, Python tooling, embedded web UI, packaging, and tests. The project needed stronger operational structure for agent routing, skills, release evidence, backup policy, and restore discipline.

The external proposal suggested a larger workspace with `agents`, `skills`, `apps`, `libs`, `configs`, and `artifacts`. Moving the current working code into that layout would add risk without immediate product value.

## Decision

Add the operating workflow as a non-invasive layer:

- `agents/` for role definitions and routing policy.
- `skills/` for a shared skill catalog.
- `docs/releases/` for release governance.
- `docs/runbooks/` for backup and restore procedures.
- `scripts/backup/` and `scripts/release/` for automation.
- `CHANGELOG.md`, `ROADMAP.md`, and `CONTRIBUTING.md` at repo root.

Do not move existing product code or release scripts in this phase.

## Consequences

Benefits:
- Clear accountability for future agent work.
- Better release and backup traceability.
- Lower adoption risk than a full repo reorganization.
- Existing build/package paths remain stable.

Tradeoffs:
- Some script folders remain flat for now.
- The `apps/libs/configs/artifacts` layout is deferred.
- Process docs must be actively followed to provide value.

## Alternatives Considered

1. Full repo migration to `apps/` and `libs/`.
   Rejected for now because it would touch many working paths and increase regression risk.

2. Only update `AGENTS.md`.
   Rejected because release, backup, and skill workflows need dedicated runbooks and scripts.

3. Create executable Codex skills immediately.
   Deferred until workflows stabilize through actual use.
