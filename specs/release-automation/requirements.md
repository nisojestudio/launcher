# Requirements Document

## Introduction

Panel Live needs the Windows release process to produce versioned artifacts with checksums and a manifest in one predictable folder. This spec turns the existing release policy into executable local tooling without changing product code.

## Requirements

### Requirement 1 - Versioned Artifacts

**User Story:** As the project owner, I want installer and portable outputs named with the release version so that old releases are never overwritten accidentally.

#### Acceptance Criteria

1. When a Windows release is built, the release flow shall write artifacts under `dist/releases/<version>/`.
2. When a Windows installer is generated, the release flow shall name it `panel-live-<version>-win-x64.exe`.
3. When a portable ZIP is generated for release, the release flow shall name it `panel-live-<version>-win-x64-portable.zip`.
4. When a version is provided, the release flow shall require `MAJOR.MINOR.PATCH`.

### Requirement 2 - Release Manifest

**User Story:** As the release owner, I want every generated release folder to contain checksums and a manifest so that artifacts can be audited later.

#### Acceptance Criteria

1. When release artifacts are generated, the release flow shall write `SHA256SUMS.txt`.
2. When release artifacts are generated, the release flow shall write `release-manifest-<version>.json`.
3. When validation or backup statuses are known, the release manifest shall record those statuses instead of leaving them implicit.

### Requirement 3 - One Command Preparation

**User Story:** As the maintainer, I want one command to prepare a release so that build, tests, backup, packaging, and manifest generation follow the same order each time.

#### Acceptance Criteria

1. When `prepare_release.ps1` runs without skip flags, it shall check Git hygiene, create a backup, run local validation, and build release artifacts.
2. When the working tree is dirty, the release preparation shall stop unless explicitly allowed for a draft release.
3. When dry-run mode is used, the release preparation shall print planned commands without creating artifacts.

### Requirement 4 - Non-Disruptive Adoption

**User Story:** As the maintainer, I want this release flow added without moving existing code or changing runtime behavior.

#### Acceptance Criteria

1. The release automation shall reuse the existing package and installer scripts.
2. The release automation shall avoid changes to C++ core, Python bridge runtime logic, or frontend behavior.
3. Existing direct package and installer scripts shall remain callable for focused troubleshooting.
