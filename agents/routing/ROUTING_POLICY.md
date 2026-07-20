# Agent Routing Policy

## Intake

For every task, classify:
- Goal: bug, feature, release, audit, docs, support.
- Surface: C++, Python, frontend, Node, installer, docs, backup.
- Risk: low, medium, high.
- Required evidence: read-only audit, unit tests, build, smoke, release package, restore test.

## Skill Activation

Based on task classification, activate the corresponding lifecycle skill (defined in `skills/SKILL_CATALOG.md`) BEFORE routing to an agent persona:

| Task | Lifecycle Skill | Load with `skill` tool |
|------|----------------|------------------------|
| New feature / change design | `spec-driven-development` | Always |
| Implementation planning | `planning-and-task-breakdown` | Always |
| Write/modify code (1+ files) | `incremental-implementation` | Always |
| Write/fix tests | `test-driven-development` | Always |
| Debug unexpected behavior | `debugging-and-error-recovery` | Always |
| Code review before merge | `code-review-and-quality` | Always |
| Security-sensitive change | `security-and-hardening` | Always |
| Simplify existing code | `code-simplification` | When complexity is flagged |
| Prepare release / deploy | `shipping-and-launch` + `ci-cd-and-automation` | Always |
| Architecture decision | `documentation-and-adrs` | Always |
| Git/versioning workflow | `git-workflow-and-versioning` | When branching/versioning is involved |

The agent persona (below) operates WITHIN the skill workflow — the skill defines the process, the persona brings domain expertise.

## Ownership

Use one primary owner. Add supporting roles only when the task crosses boundaries.

Examples:
- TikTok bridge chat issue: Python Engineer primary, C++ Engineer and QA supporting.
- Monitor UI issue: Frontend Web Engineer primary, QA supporting.
- Installer release: Release Manager primary, QA and C++ Engineer supporting.
- CMake dependency issue: C++ Engineer primary, DevOps / Build Engineer supporting.

## Persona Activation

For specialized reviews, invoke the relevant persona from `agents/<name>.md` as an additional quality gate:

| Review Type | Persona | When |
|-------------|---------|------|
| Code quality | `code-reviewer` (Staff Engineer) | Before merge of any non-trivial change |
| Test coverage | `test-engineer` (QA Specialist) | When test gaps are suspected or coverage matters |
| Security audit | `security-auditor` (Security Engineer) | Auth, WebSocket, file I/O, release, or external integrations |
| Web performance | `web-performance-auditor` (Perf Engineer) | UI changes, embedded web views, asset loading |

Personas are additive — invoke them after the primary skill has completed its workflow. Do not invoke a persona from within another persona. See `docs/agent-skills-orchestration.md`.

## Escalation Rules

Escalate to Orchestrator when:
- More than two domains are touched.
- A folder move or architectural boundary change is proposed.
- A dependency is added.
- A release gate fails.
- A rollback or restore decision is needed.
- A lifecycle skill flags a `RED FLAG` that requires human decision.

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
- Skill verification gates passed (from the activated lifecycle skill).

Release change:
- Changelog update.
- Manifest/checksum plan.
- Installer/package validation plan.
- Backup or rollback path.

## Non-Goals

This policy does not require actual subagents for every task. It defines roles and accountability. A single agent can perform multiple roles as long as the report is explicit. Skill activation always takes precedence over persona — process first, expertise second.
