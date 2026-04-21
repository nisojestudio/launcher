# Panel Live Skill Catalog

## Purpose

This catalog lists the reusable workflows expected across Panel Live. These are project skills first: documented practices that can later become executable Codex skills if they prove repetitive and valuable.

## Shared Skills

### task-intake

Clarify goal, affected modules, risk, validation needs, and expected deliverable.

### repo-standards

Follow `AGENTS.md`, `docs/WORKING_CONTRACT.md`, and existing repo patterns before adding new structure.

### git-workflow

Check status before changes, keep commits scoped, avoid reverting unrelated work, and record meaningful commit messages.

### testing-minimum

Pick the smallest validation that proves the behavior. Never claim validation that was not executed.

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

Record irreversible or cross-cutting decisions with context, decision, consequences, and alternatives.

### runbook-writing

Write operational steps that can be executed under pressure.

## Security Skills

### security-review

Apply for auth, downloads, WebSocket/event ingestion, local file writes, release publishing, and backup handling.

## Promotion Rule

Promote a catalog entry into a full executable skill only after it is used repeatedly and has stable inputs, outputs, and validation steps.
