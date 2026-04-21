# ADR 0002 - Versioned Release Automation

## Status

Accepted

## Context

Panel Live already had portable packaging, Windows installer generation, a release policy, and a manifest helper. The weak point was that artifacts could still be produced as loose outputs under `dist`, and the installer file name was tied to a hardcoded `3.0` label instead of the release version.

## Decision

Windows release artifacts are generated under:

```text
dist/releases/<version>/
```

The release artifact names are:

```text
panel-live-<version>-win-x64.exe
panel-live-<version>-win-x64-portable.zip
```

`scripts/release/prepare_release.ps1` is the preferred release entry point. It checks Git hygiene, creates a backup, runs validation, invokes the installer flow, and propagates validation status to the release manifest. Direct package and installer scripts remain available for troubleshooting.

## Consequences

- Old release artifacts are preserved by version folder.
- The generated manifest now records known build, test, installer, and backup statuses.
- A release can be dry-run before doing expensive work.
- Full release generation still depends on local release prerequisites: MSVC, CMake, Inno Setup, bridge `.venv`, and network access for prerequisite downloads when not cached.
