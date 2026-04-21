# Restore Test Runbook

## Purpose

This runbook verifies that a backup can restore Panel Live to a usable state.

## Frequency

Run monthly and after major release workflow changes.

## Code Backup Restore

1. Copy or clone the backup into a temporary restore folder.
2. Run:

```powershell
git status --short
git log --oneline -3
```

3. Confirm expected commits are present.
4. Confirm `README.md`, `AGENTS.md`, `src`, `tools`, and `tests` exist.

## Full Operational Backup Restore

1. Copy the full backup into a temporary restore folder.
2. Confirm these paths exist:

```text
.git
src
tools
tests
build
dist
.venv
tools/bridge_py/.venv
vcpkg_installed
panel_config.json
```

3. Run script parse checks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "$errors=$null; [System.Management.Automation.PSParser]::Tokenize((Get-Content -Raw .\scripts\backup\create_project_backup.ps1), [ref]$errors) > $null; if ($errors) { $errors; exit 1 }"
```

4. If toolchain is available, run:

```powershell
cmake --build --preset release
python -m unittest discover -s tools/bridge_py/tests -t tools/bridge_py -v
```

5. Document skipped steps and why.

## Evidence

Record:
- backup path
- restore path
- date
- commit
- commands executed
- results
- risks
