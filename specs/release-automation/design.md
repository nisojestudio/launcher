# Technical Design

## Overview

This design adds a versioned Windows release path on top of the existing packaging scripts. It keeps `scripts/package_windows.ps1` and `scripts/build_windows_installer.ps1` as the artifact builders, then adds `scripts/release/prepare_release.ps1` as the release orchestrator.

## Components

```text
scripts/
  package_windows.ps1          Builds portable folder and portable ZIP
  build_windows_installer.ps1  Builds portable package, installer, checksums, manifest
  release/
    prepare_release.ps1        Runs backup, validation, installer flow
    new_release_manifest.ps1   Writes release manifest JSON
installer/
  panel_live.iss               Inno Setup definition with version defines
dist/
  releases/<version>/          Ignored output root for generated artifacts
```

## Decisions

1. Require strict `MAJOR.MINOR.PATCH` for release scripts.
   This keeps installer metadata and SemVer docs aligned.

2. Use Inno Setup preprocessor defines for versioned installer metadata.
   The installer definition stays reusable while `build_windows_installer.ps1` owns the concrete release version.

3. Keep release outputs ignored by Git.
   Release artifacts are generated outputs. Audit metadata lives in the generated manifest and checksums; source changes stay in Git.

4. Make `prepare_release.ps1` conservative by default.
   It stops on dirty Git state, creates a code backup, runs tests, and then builds artifacts unless skip flags are passed.

## Validation Strategy

- Parse all changed PowerShell scripts.
- Run `prepare_release.ps1 -DryRun` to verify orchestration without building heavy artifacts.
- Check Git diff to confirm only release tooling and docs changed.
- Full release validation remains: configure/build Release, `ctest`, Python bridge tests, installer generation, and bridge runtime validation during installer build.

## Risks

- Full release generation can still fail if Inno Setup, MSVC, WebView2 download, VC++ redistributable download, or the bridge `.venv` is missing.
- The prepare script currently uses local `python` for bridge tests; a workstation with multiple Python installs may need environment cleanup.
- `prepare_release.ps1` intentionally creates a code backup by default, which can take time on large working copies.
