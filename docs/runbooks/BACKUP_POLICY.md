# Backup Policy

## Purpose

Panel Live needs backups that protect code, docs, release artifacts, local configuration, and build/runtime material when needed.

## Backup Types

### Code Backup

Use for normal daily work.

Includes:
- Git history.
- Tracked files.
- Current committed source.

Excludes:
- build outputs.
- virtualenvs.
- local caches.
- logs.

### Full Operational Backup

Use before releases, risky refactors, workstation changes, or delivery handoff.

Includes:
- Git history.
- tracked files.
- `build`
- `dist`
- `.venv`
- `tools/bridge_py/.venv`
- `vcpkg_installed`
- local support diagnostics when present.

Excludes:
- `.codex_tmp`
- `ctest_temp`
- transient `*.tmp` files.

## Cadence

Minimum policy:
- Daily code backup during active development.
- Full operational backup before release.
- Weekly full operational backup.
- Monthly restore test.

## Default Location

Use:

```text
%USERPROFILE%\Desktop\PanelLiveBackups
```

## Command

Code backup:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\backup\create_project_backup.ps1 -Mode code
```

Full backup:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\backup\create_project_backup.ps1 -Mode full
```

Full backup with zip:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\backup\create_project_backup.ps1 -Mode full -Zip
```

## Manifest

Every backup should include:
- project name
- timestamp
- backup mode
- source path
- destination path
- git commit
- branch
- dirty status
- file count
- byte count
- exclusions
- zip checksum when zip is generated

## Restore Rule

A backup is not trusted until at least one restore test has been performed for its backup class.
