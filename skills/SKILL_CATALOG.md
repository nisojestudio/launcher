# Panel Live Skill Catalog

## Purpose

This catalog lists all reusable workflows available across Panel Live. It combines:
- **Project-specific skills**: conventions and practices for the Panel Live stack.
- **Lifecycle skills** (from addyosmani/agent-skills): production-grade engineering workflows covering the full Define → Plan → Build → Verify → Review → Ship lifecycle.

Skills in `skills/<name>/SKILL.md` are full executable workflows with steps, verification gates, and anti-rationalization tables. Skills listed as descriptions only are lightweight documented practices.

## Lifecycle Skills (addyosmani/agent-skills)

Full executable workflows in `skills/<name>/SKILL.md`. The agent activates these automatically based on the task.

### Define — Clarify what to build

| Skill | What It Does | Use When |
|-------|-------------|----------|
| [spec-driven-development](spec-driven-development/SKILL.md) | Write a PRD covering objectives, structure, code style, testing, and boundaries before any code | Starting a new project, feature, or significant change |

### Plan — Break it down

| Skill | What It Does | Use When |
|-------|-------------|----------|
| [planning-and-task-breakdown](planning-and-task-breakdown/SKILL.md) | Decompose specs into small, verifiable tasks with acceptance criteria and dependency ordering | You have a spec and need implementable units |

### Build — Write the code

| Skill | What It Does | Use When |
|-------|-------------|----------|
| [incremental-implementation](incremental-implementation/SKILL.md) | Thin vertical slices — implement, test, verify, commit. Feature flags, safe defaults, rollback-friendly changes | Any change touching more than one file |
| [test-driven-development](test-driven-development/SKILL.md) | Red-Green-Refactor, test pyramid (80/15/5), DAMP over DRY, Beyonce Rule | Implementing logic, fixing bugs, or changing behavior |
| [code-simplification](code-simplification/SKILL.md) | Chesterton's Fence, Rule of 500, reduce complexity while preserving exact behavior | Code works but is harder to read or maintain than it should be |

### Verify — Prove it works

| Skill | What It Does | Use When |
|-------|-------------|----------|
| [debugging-and-error-recovery](debugging-and-error-recovery/SKILL.md) | Five-step triage: reproduce, localize, reduce, fix, guard. Stop-the-line rule, safe fallbacks | Tests fail, builds break, or behavior is unexpected |

### Review — Quality gates before merge

| Skill | What It Does | Use When |
|-------|-------------|----------|
| [code-review-and-quality](code-review-and-quality/SKILL.md) | Five-axis review (correctness, readability, architecture, security, performance), change sizing, severity labels | Before merging any change |
| [security-and-hardening](security-and-hardening/SKILL.md) | OWASP Top 10 prevention, auth patterns, secrets management, dependency auditing, three-tier boundary system | Handling user input, auth, data storage, or external integrations |

### Ship — Deploy with confidence

| Skill | What It Does | Use When |
|-------|-------------|----------|
| [git-workflow-and-versioning](git-workflow-and-versioning/SKILL.md) | Trunk-based development, atomic commits, change sizing (~100 lines), commit-as-save-point pattern | Making any code change |
| [ci-cd-and-automation](ci-cd-and-automation/SKILL.md) | Shift Left, Faster is Safer, feature flags, quality gate pipelines, failure feedback loops | Setting up or modifying build and deploy pipelines |
| [documentation-and-adrs](documentation-and-adrs/SKILL.md) | Architecture Decision Records, API docs, inline documentation standards — document the *why* | Making architectural decisions, changing APIs, or shipping features |
| [shipping-and-launch](shipping-and-launch/SKILL.md) | Pre-launch checklists, feature flag lifecycle, staged rollouts, rollback procedures, monitoring setup | Preparing to deploy to production |

## Agent Personas

Specialist personas available in `agents/<name>.md` for targeted reviews:

| Agent | Role | Perspective |
|-------|------|-------------|
| [code-reviewer](../agents/code-reviewer.md) | Senior Staff Engineer | Five-axis code review with "would a staff engineer approve this?" standard |
| [test-engineer](../agents/test-engineer.md) | QA Specialist | Test strategy, coverage analysis, and the Prove-It pattern |
| [security-auditor](../agents/security-auditor.md) | Security Engineer | Vulnerability detection, threat modeling, OWASP assessment |
| [web-performance-auditor](../agents/web-performance-auditor.md) | Web Performance Engineer | Core Web Vitals audit with Quick/Deep modes |

## Reference Checklists

Available in `skills/references/`:

| Reference | Covers |
|-----------|--------|
| [definition-of-done.md](references/definition-of-done.md) | Project-wide standing bar every change clears |
| [testing-patterns.md](references/testing-patterns.md) | Test structure, naming, mocking, anti-patterns |
| [security-checklist.md](references/security-checklist.md) | Pre-commit checks, auth, input validation, OWASP Top 10 |
| [performance-checklist.md](references/performance-checklist.md) | Core Web Vitals targets, frontend/backend checklists |
| [accessibility-checklist.md](references/accessibility-checklist.md) | Keyboard nav, screen readers, ARIA, testing tools |
| [observability-checklist.md](references/observability-checklist.md) | Structured logging, RED/USE metrics, tracing, pre-launch gate |
| [orchestration-patterns.md](references/orchestration-patterns.md) | Multi-persona orchestration patterns and anti-patterns |

## Project-Specific Skills

Practices y convenciones específicas del stack Panel Live. Complementan los lifecycle skills — cuando exista un lifecycle skill equivalente, el agente DEBE cargar ese primero y aplicar estas reglas como contexto adicional.

### task-intake

Clarify goal, affected modules, risk, validation needs, and expected deliverable.

### repo-standards

Follow `AGENTS.md`, `docs/WORKING_CONTRACT.md`, and existing repo patterns before adding new structure.

### git-workflow

Convenciones específicas de Panel Live: check status before changes, keep commits scoped, avoid reverting unrelated work, record meaningful commit messages.
→ **Lifecycle skill**: `git-workflow-and-versioning` para el workflow completo con atomic commits y versionado.

### testing-minimum

Regla mínima: pick the smallest validation that proves the behavior. Never claim validation that was not executed.
→ **Lifecycle skill**: `test-driven-development` para red-green-refactor completo.

### changelog-update

Add user-relevant changes under `CHANGELOG.md` before a release or operational milestone.

### release-versioning

Use SemVer, source commit, artifact names, checksums, and manifest.

### backup-procedure

Create code or full operational backups with manifests before risky work and releases.

### incident-log

Record production/live issues with timeline, evidence, root cause, mitigation, and follow-up.

## Stack Skills

### cpp-cmake-build

Configure and build with CMake presets, vcpkg toolchain when needed, and Release mode for packaging.

### cpp-testing

Run relevant `ctest` targets and add focused C++ tests for native behavior.

### cpp-memory-safety

Review ownership, lifetimes, bounds, concurrency, and error handling for native code.

### python-automation

Build scripts and bridge tools with explicit parameters, useful logs, and safe defaults.

### python-pytest-standards

Use unittest/pytest-compatible tests, deterministic fixtures, and no live network requirement unless explicitly marked.

### frontend-component-pattern

Respect existing embedded UI patterns, state shape, and asset generation.

### frontend-accessibility

Preserve keyboard access, readable labels, contrast, responsive layout, and non-overlapping text.

### node-api-pattern

Keep Node services explicit about contracts, auth, inputs, outputs, and error responses.

### node-integration-testing

Validate worker/API behavior with local integration tests or documented smoke checks.

## QA Skills

### qa-regression-map

Identify workflows that could regress: TikTok connection, monitor, TTS, Arena bridge, packaging, auth.

### qa-evidence-report

Summarize commands, results, skipped checks, and residual risk.

## Release Skills

### artifact-publishing

Publish artifacts with version, platform, architecture, commit, checksum, date, and release notes.

### installer-versioning

Name installers by product, version, OS, and architecture. Do not overwrite previous versions.

### release-notes-format

Write concise changes, fixes, validation, known issues, and rollback instructions.

## Docs Skills

### adr-writing

Convención: record irreversible or cross-cutting decisions with context, decision, consequences, and alternatives.
→ **Lifecycle skill**: `documentation-and-adrs` para el workflow completo con ADR structure y trazabilidad.

### runbook-writing

Write operational steps that can be executed under pressure.

## Security Skills

### security-review

Áreas específicas de Panel Live: auth, downloads, WebSocket/event ingestion, local file writes, release publishing, and backup handling.
→ **Lifecycle skill**: `security-and-hardening` para OWASP Top 10, threat modeling y three-tier boundary system.

## Promotion Rule

Promote a catalog entry into a full executable skill only after it is used repeatedly and has stable inputs, outputs, and validation steps.
