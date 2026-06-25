# Release Protocol

## Purpose

Define the exact step-by-step procedure for cutting a Panel Live 3.0 release, from
code-freeze to production deploy. Every release MUST follow this protocol.

## Overview

```
Git freeze → CHANGELOG → bump version → build → test → backup → package →
installer → manifest → GitHub Release → update sitio URLs → deploy Worker → verify
```

The full pipeline is automated in `scripts/release/deploy_launcher.ps1`.
This document describes each gate so the operator (human or agent) can validate
every step individually if needed.

---

## Phase 0 — Preparation

### Gate 0.1 — Working tree clean

```powershell
git status --short
```

If dirty:
- Commit fix/feat changes with conventional message.
- Or pass `-AllowDirty` for a draft release (not for production).

### Gate 0.2 — CHANGELOG entry exists

```
CHANGELOG.md has ## <version> - <YYYY-MM-DD>
```

Write the entry with Added / Fixed / Changed / Removed sections.
The entry MUST be complete before the release tag is created.

### Gate 0.3 — Version bump

Files to update (search for all occurrences):

| File | Field |
|------|-------|
| `CMakeLists.txt` | `project(NisojeLivePanelV3 VERSION ...)` |
| `src/platform/CMakeLists.txt` | `set(NLP3_PANEL_VERSION "x.y.z")` |
| `vcpkg.json` | `"version": "x.y.z"` (if present) |

Commit the bump:

```powershell
git add -A
git commit -m "release: prepare v<version>"
```

### Gate 0.4 — Tag

```powershell
git tag -a "v<version>" -m "Panel Live <version>"
```

---

## Phase 1 — Build & Test

### Gate 1.1 — C++ Release build

```powershell
cmake --preset release
cmake --build --preset release
```

Binary must appear in `build/release-<version>/src/platform/Release/`.

### Gate 1.2 — C++ tests

```powershell
ctest --preset release --output-on-failure
```

All tests must pass. If a test is skipped, document the reason in the release notes.

### Gate 1.3 — Python bridge tests

```powershell
python -m unittest discover -s tools/bridge_py/tests -t tools/bridge_py -v
```

Requires `LIVEPANEL_TIKTOK_PYTHON_EXE` env var or `.venv` in `tools/bridge_py/`.

---

## Phase 2 — Package

### Gate 2.1 — Backup

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\backup\create_project_backup.ps1 -Mode code
```

Use `-Mode full` if release is a MAJOR or MINOR version.

### Gate 2.2 — Portable package

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\scripts\package_windows.ps1 `
    -OutputRoot .\dist\releases\<version> `
    -PortableZipName panel-live-<version>-win-x64-portable.zip
```

Output: `dist/releases/<version>/panel-live-<version>-win-x64-portable.zip`

### Gate 2.3 — Windows installer

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\scripts\build_windows_installer.ps1 `
    -Version <version> `
    -OutputRoot .\dist\releases\<version>
```

Output: `dist/releases/<version>/installer/panel-live-<version>-win-x64.exe`

### Gate 2.4 — Checksums

```powershell
Get-FileHash -Path "dist/releases/<version>/installer/panel-live-<version>-win-x64.exe" -Algorithm SHA256
Get-FileHash -Path "dist/releases/<version>/panel-live-<version>-win-x64-portable.zip" -Algorithm SHA256
```

Write to `dist/releases/<version>/SHA256SUMS.txt`.

### Gate 2.5 — Release manifest

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\scripts\release\new_release_manifest.ps1 `
    -Version <version> `
    -ArtifactPaths "dist/releases/<version>/installer/panel-live-<version>-win-x64.exe","dist/releases/<version>/panel-live-<version>-win-x64-portable.zip"
```

Output: `dist/releases/<version>/release-manifest-<version>.json`

### Gate 2.6 — Debug CRT check (Windows)

Verify the packaged host does not depend on debug CRT:

```powershell
dumpbin /dependents .\build\release-<version>\src\platform\Release\panel_app.exe
```

Ensure `MSVCP140D.dll` and `VCRUNTIME140D.dll` are NOT present.

---

## Phase 3 — Release Orchestrated

The single-command pipeline:

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\scripts\release\prepare_release.ps1 `
    -Version <version> `
    -BackupMode code
```

This runs:
1. Clean working tree check (unless `-AllowDirty`)
2. Backup
3. C++ Release build
4. C++ tests
5. Python bridge tests
6. Windows installer + portable zip + checksums + manifest

---

## Phase 4 — Publish

### Gate 4.1 — GitHub Release

Requires `gh` CLI authenticated (`gh auth status`).

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\scripts\release\github_release.ps1 `
    -Version <version> `
    -Changelog "Short description of changes"
```

This creates a **draft** prerelease, uploads assets, then publishes.

Verify at: `https://github.com/nisojestudio/launcher/releases/tag/v<version>`

### Gate 4.2 — Update INSTALLER_URL

Files in `sitio/` (sibling repo `panel-live-master/sitio/`):

| File | What to change |
|------|---------------|
| `wrangler.api.jsonc` | `INSTALLER_URL` download URL |
| `.env` | `INSTALLER_URL` download URL |
| `.github/workflows/deploy-pages.yml` | download URL in deploy step |

The pattern to replace:

```
https://github.com/nisojestudio/launcher/releases/download/v<old>/panel-live-<old>-win-x64.exe
→ https://github.com/nisojestudio/launcher/releases/download/v<new>/panel-live-<new>-win-x64.exe
```

### Gate 4.3 — Deploy Cloudflare Worker

```powershell
Push-Location <sitio_root>
npx wrangler deploy --config wrangler.api.jsonc
Pop-Location
```

### Gate 4.4 — Verify production

```powershell
Invoke-RestMethod -Uri "https://nisoje.com/api/version/latest"
```

Expected:

```json
{
  "latest_version": "v<version>",
  "installer_url": "https://github.com/nisojestudio/launcher/releases/download/v<version>/panel-live-<version>-win-x64.exe"
}
```

---

## Phase 5 — Full Pipeline (Recommended)

For a full automated release from preparation to deploy:

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\scripts\release\deploy_launcher.ps1 `
    -Version <version> `
    -Changelog "Short description of changes"
```

This executes all 6 steps:
1. Prepare release (build + tests + backup + installer + manifest)
2. GitHub Release (draft → upload → publish)
3. Update INSTALLER_URL in sitio files
4. Commit + push sitio changes
5. Deploy Cloudflare Worker
6. Verify production endpoint

Optional flags: `-SkipBuild`, `-SkipGitHubRelease`, `-SkipSitioUpdate`, `-DryRun`

---

## Phase 6 — Post-Release

### Gate 6.1 — Clean up release branch

If a `release/<version>` branch was used, merge to `main` and `develop` and delete.

### Gate 6.2 — Update AGENTS.md context

Note the new version in AGENTS.md or release notes so future agent sessions know
the current baseline.

### Gate 6.3 — Rollback plan

Document what a rollback would require:
- Previous release: `dist/releases/<previous-version>/`
- Previous Worker config: git revert in `sitio/`
- Previous backup: `scripts/backup/restore_project_backup.ps1`

---

## 15 Release Gates Summary

| # | Gate | Script | Manual? |
|---|------|--------|---------|
| 1 | Clean tree | `git status` | Manual |
| 2 | CHANGELOG entry | — | Manual |
| 3 | C++ Release build | `cmake --build --preset release` | Auto (Phase 3) |
| 4 | C++ tests pass | `ctest --preset release` | Auto (Phase 3) |
| 5 | Python bridge tests | `python -m unittest ...` | Auto (Phase 3) |
| 6 | Portable package | `package_windows.ps1` | Auto (Phase 3) |
| 7 | Windows installer | `build_windows_installer.ps1` | Auto (Phase 3) |
| 8 | Debug CRT check | `dumpbin /dependents` | Manual |
| 9 | Checksums | `Get-FileHash` | Auto (Phase 3) |
| 10 | Release manifest | `new_release_manifest.ps1` | Auto (Phase 3) |
| 11 | Backup | `create_project_backup.ps1` | Auto (Phase 3) |
| 12 | GitHub Release | `github_release.ps1` | Auto (Phase 4) |
| 13 | INSTALLER_URL updated | — | Auto (Phase 4) |
| 14 | Worker deployed | `wrangler deploy` | Auto (Phase 4) |
| 15 | Verification | `curl ... /api/version/latest` | Auto (Phase 4) |

## Hard Rules for Agent Operators

These rules exist because past agent sessions committed the same mistakes repeatedly.
Follow them strictly — no exceptions.

### Rule 1 — Run the script, do not inspect prerequisites manually

If a gate says `build_windows_installer.ps1 -Version X.Y.Z`, **run that script**.
Do NOT check whether Inno Setup is installed first. Do NOT search for ISCC.exe.
Do NOT try to predict whether it will work. The script resolves its own
dependencies (it installs Inno Setup via winget if missing). Running the script
is the only valid way to determine if a gate passes or fails.

**Wrong:** `if (Test-Path "ISCC.exe") { ... } else { skip }`
**Correct:** `powershell -File .\scripts\build_windows_installer.ps1 -Version X.Y.Z`

### Rule 2 — Build within the MSVC environment

The C++ toolchain requires the MSVC environment variables (`vcvars64.bat` or
`VsDevCmd.bat`). Running `ninja` or `cmake --build` without this environment
will fail with `fatal error C1083: cannot open include file: 'string_view'`
even though the file exists. This is NOT a project bug.

The package scripts (`package_windows.ps1`, `build_windows_installer.ps1`,
`prepare_release.ps1`) all call `Ensure-MsvcBuildEnvironment` internally.
When running build commands manually:

```powershell
# Correct — load environment first
cmd.exe /c "`"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat`" >nul && ninja -C build\release -j4"
```

Or use `prepare_release.ps1` / `deploy_launcher.ps1` which handle this automatically.

### Rule 3 — Never mark a gate "skipped" without running the script

If the protocol lists a script for a gate, execute that script first. Only if the
script itself fails with a clear error should you consider skipping, and then
follow the Emergency Override procedure below.

### Rule 4 — Read the script before assuming what it does

Before declaring a tool or dependency missing, read the relevant script.
Many scripts in this project install their own dependencies (winget, curl, etc.)
or search multiple locations. A `Test-Path` in the shell is NOT the same as
the script's own resolution logic.

---

## Emergency Override

If a gate must be skipped, record:
- Which gate
- Reason
- Risk assessment
- Recovery plan

Example: "Gate 5 (Python bridge tests) skipped because TikTok API is down for
maintenance. Risk: bridge may fail until API is back. Recovery: run tests before
next release and patch if needed."
