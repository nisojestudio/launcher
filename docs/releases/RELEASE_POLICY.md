# Release Policy

## Purpose

Panel Live releases must be reproducible, versioned, and reversible. Installers and portable packages are release artifacts, not loose files.

## Versioning

Use SemVer:

```text
MAJOR.MINOR.PATCH
```

Rules:
- MAJOR: incompatible product or protocol changes.
- MINOR: new compatible capabilities.
- PATCH: fixes and low-risk improvements.

Current project baseline is `0.1.0` in `CMakeLists.txt` and `vcpkg.json`.

## Branches

Recommended branch model:

```text
main
develop
feature/<name>
fix/<name>
release/<version>
hotfix/<version>
```

For this local phase, `master` currently exists. Rename or introduce `main/develop` only as a deliberate Git operation.

## Commit Convention

Use conventional prefixes:

```text
feat: add live monitor filter
fix: correct TikTok timestamp normalization
docs: add backup runbook
build: update CMake release preset
test: add bridge regression coverage
release: prepare v0.2.0
```

## Release Gates

Before publishing a release:

1. Git working tree is clean.
2. `CHANGELOG.md` has an entry for the version.
3. C++ build passes for Release.
4. C++ tests pass, or skipped tests are documented with reason.
5. Python bridge tests pass.
6. Portable package is generated.
7. Windows installer is generated when the release includes installer distribution.
8. Debug CRT check passes for packaged host.
9. Checksums are generated.
10. Release manifest is generated.
11. Backup exists or is created.

## Existing Commands

Build:

```powershell
cmake --preset release
cmake --build --preset release
```

Tests:

```powershell
ctest --preset release --output-on-failure
python -m unittest discover -s tools/bridge_py/tests -t tools/bridge_py -v
```

Portable package:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1
```

Installer:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_windows_installer.ps1
```

Release manifest:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\release\new_release_manifest.ps1 -Version 0.2.0 -ArtifactPaths .\dist\NisojeStudio-portable.zip,.\dist\installer\PanelLive-0.2.0-win-x64.exe
```

## Artifact Naming

Preferred format:

```text
panel-live-<version>-win-x64.exe
panel-live-<version>-win-x64-portable.zip
```

Current installer script still emits `PanelLive-3.0-Windows-x64-Setup.exe`. Versioned artifact naming should be implemented before public release publishing.

## No Overwrite Rule

Do not overwrite old release artifacts. Store release outputs under:

```text
dist/releases/<version>/
```

Each release folder should contain:
- installer or portable zip
- `SHA256SUMS.txt`
- release manifest JSON
- release notes

## Rollback

Rollback requires:
- previous release artifact
- previous manifest
- previous config or documented config migration
- known compatible backup

If rollback is not tested, say so explicitly in the release report.
