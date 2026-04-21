# Requirements Document

## Introduction

Panel Live needs an operating model for agent-assisted work, releases, backups, and traceability without disrupting the current C++/Python/web architecture. This spec formalizes the first non-invasive layer: agent roles, shared skills, release rules, backup rules, and minimum validation gates.

## Requirements

### Requirement 1 - Agent Operating Model

**User Story:** As the project owner, I want a small set of clear agent roles so that work can be routed consistently without creating unnecessary coordination overhead.

#### Acceptance Criteria

1. When a task enters the project, the Panel Live workflow shall classify the task by domain, risk, and validation needs.
2. When a task touches multiple domains, the Panel Live workflow shall identify one accountable lead role and any supporting specialist roles.
3. When an agent completes work, the Panel Live workflow shall require changed files, decisions, risks, validation, and next steps.
4. When a task is exploratory, the Panel Live workflow shall separate hypotheses from validated findings.

### Requirement 2 - Shared Skills

**User Story:** As a contributor, I want shared skills documented in one catalog so that agents and humans apply the same quality bar.

#### Acceptance Criteria

1. When a task starts, the Panel Live workflow shall select applicable shared skills before implementation.
2. When a task changes C++, Python, frontend, Node, QA, release, docs, or security surfaces, the Panel Live workflow shall reference the matching stack skills.
3. When a new repeated workflow appears, the Panel Live workflow shall document it as a candidate skill before automating it.

### Requirement 3 - Release Governance

**User Story:** As the product owner, I want releases to be versioned and reproducible so that installers and portable artifacts are not loose files.

#### Acceptance Criteria

1. When a release is prepared, the Panel Live workflow shall use SemVer and record the source commit.
2. When artifacts are generated, the Panel Live workflow shall write checksums and a release manifest.
3. When an installer is generated, the Panel Live workflow shall preserve older release artifacts instead of overwriting them.
4. When a release is closed, the Panel Live workflow shall record validation results and known risks.

### Requirement 4 - Backup And Restore

**User Story:** As the project owner, I want backups with manifests so that the project can be restored after accidental loss or a bad release.

#### Acceptance Criteria

1. When a backup is created, the Panel Live workflow shall include a manifest with timestamp, source, commit, file count, byte count, and mode.
2. When a release is cut, the Panel Live workflow shall create or identify a release backup before publishing.
3. When a restore test is scheduled, the Panel Live workflow shall verify that code, configuration, build prerequisites, and release artifacts can be recovered.
4. When backup exclusions are used, the Panel Live workflow shall document what was excluded and why.

### Requirement 5 - Non-Disruptive Adoption

**User Story:** As the maintainer, I want the workflow layer added without moving working code so that current builds and installers remain stable.

#### Acceptance Criteria

1. While the current repo layout is functional, the Panel Live workflow shall not move `src`, `tools`, `tests`, `scripts`, or installer files only to match a template.
2. When a folder reorganization is proposed, the Panel Live workflow shall require an ADR and a rollback path.
3. When docs are added, the Panel Live workflow shall link to existing scripts and architecture rather than duplicating conflicting instructions.
