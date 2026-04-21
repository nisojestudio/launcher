# Agent Routing Policy

## Intake

For every task, classify:
- Goal: bug, feature, release, audit, docs, support.
- Surface: C++, Python, frontend, Node, installer, docs, backup.
- Risk: low, medium, high.
- Required evidence: read-only audit, unit tests, build, smoke, release package, restore test.

## Ownership

Use one primary owner. Add supporting roles only when the task crosses boundaries.

Examples:
- TikTok bridge chat issue: Python Engineer primary, C++ Engineer and QA supporting.
- Monitor UI issue: Frontend Web Engineer primary, QA supporting.
- Installer release: Release Manager primary, QA and C++ Engineer supporting.
- CMake dependency issue: C++ Engineer primary, DevOps / Build Engineer supporting.

## Escalation Rules

Escalate to Orchestrator when:
- More than two domains are touched.
- A folder move or architectural boundary change is proposed.
- A dependency is added.
- A release gate fails.
- A rollback or restore decision is needed.

Escalate to QA when:
- Tests are missing for changed behavior.
- The change affects user-facing live workflows.
- The change affects packaging, install, or update.

Escalate to Release Manager when:
- Version, changelog, installer, manifest, artifact, or backup policy changes.

## Minimum Evidence

Read-only audit:
- Files inspected.
- Evidence found.
- Unknowns and assumptions.

Code change:
- Git diff.
- Targeted tests.
- Build if compiled assets or native code are touched.

Release change:
- Changelog update.
- Manifest/checksum plan.
- Installer/package validation plan.
- Backup or rollback path.

## Non-Goals

This policy does not require actual subagents for every task. It defines roles and accountability. A single agent can perform multiple roles as long as the report is explicit.
