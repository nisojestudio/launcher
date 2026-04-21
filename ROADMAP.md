# Roadmap

## Phase 1 - Operating Base

Status: in progress

Goals:
- Agent map.
- Skill catalog.
- Changelog.
- Release policy.
- Backup policy.
- Restore test runbook.
- Backup and release manifest scripts.

## Phase 2 - Quality Gates

Goals:
- Pull request template.
- Issue templates.
- QA checklist per module.
- CI job names aligned with release gates.
- Smoke checklist for TikTok live, TTS, monitor, Arena bridge, installer.

## Phase 3 - Releases

Status: in progress

Goals:
- Versioned installer output names.
- `dist/releases/<version>` output folders.
- Release notes per version.
- Release manifests generated as part of package flow.
- Optional code signing plan.

## Phase 4 - Resilience

Goals:
- Scheduled backup reminders or automation.
- Monthly restore test report.
- Rollback runbook.
- Release audit checklist.
- Security review checklist for auth, downloads, and local bridges.

## Deferred

- Full repo migration to `apps/` and `libs/`.
- Linux/macOS installers.
- Executable Codex skills for every catalog entry.
- Multi-agent automation beyond role-based routing.
