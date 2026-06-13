# Changelog

All notable Panel Live changes should be recorded here.

Format follows a lightweight Keep a Changelog style. Versions use SemVer.

## 0.1.10 - 2026-06-13

### Fixed

- **Brand logo**: Replaced corrupt base64 PNG with real Nisoje Studio logo (resized 32×32 from logo package), properly embedded as inline data URI.
- **TTS per-notice toggle**: Each notice now has an ON/OFF toggle. Disabled notices are skipped during sync to legacy bridge and excluded from the voice payload sent to the server.
- **Latency meter not updating**: `renderSystemStatus()` was defined but never called. Added the call in `renderAll()` so `pipelineLatencyMs` reaches `status-latency` on every poll cycle.

## 0.1.9 - 2026-06-12

### Fixed

- **Infinite update loop**: The Worker returns `latest_version` with a `v` prefix ("v0.1.8") but the panel's internal
  version has no prefix ("0.1.8"). The comparison always failed, so the update button never hid after updating.
  Fixed by normalizing: the panel now strips the leading `v` when parsing the Worker response.

## 0.1.8 - 2026-06-12

### Fixed

- **Update button now works correctly**: Replaced `std::system()` with `ShellExecuteExW` (no CMD window, UAC via `runas`).
  After installing, the panel automatically relaunches itself and shuts down the old instance (`PostQuitMessage`).

- **Visual feedback when updating**: The update button now shows "Descargando..." while downloading, and "Error" if
  something goes wrong (with auto-reset after 4 seconds).

- **Fixed version mismatch**: `NLP3_PANEL_VERSION` in `CMakeLists.txt` now correctly reads `0.1.8` (was stuck at `0.1.6`
  even though the release metadata said `0.1.7`). The panel now reports its internal version correctly.

## 0.1.7 - 2026-06-11

### Added

- Device activation on login: Panel Live 3.0 now calls `/api/license/activate` after successful license validation.
  Each PC running the launcher registers itself as a device tied to the license, preventing multi-account abuse.
- New config field `license_activate_path` (default `/api/license/activate`) in panel config.
- Device activation errors are surfaced to the UI as warnings (limit reached, device already registered on another account).

### Fixed

- Removed `SECURITY_FLAG_IGNORE_REVOCATION` usage in `win_http_client.cpp` — constant was removed in Windows 11 24H2 SDK (10.0.26100.0).
  WinHTTP now uses default certificate revocation checking, which is the correct security posture.
- Fixed `PanelUpdaterService` shutdown hang: the worker thread used `sleep_for(6h)` between update checks, blocking the
  destructor's `join()` for up to 6 hours. Replaced with `wait_for()` + `condition_variable` so `stop()` wakes the
  thread immediately and shutdown is instant. This fixes the `panel_app_smoke_test` hang.

## Unreleased

## 0.1.2 - 2026-04-29

### Fixed

- Refined the embedded game catalog UI actions and download states for the latest Panel Live 3.0 build.
- Tightened catalog card styling so controls remain readable in the release installer.

## 0.1.1 - 2026-04-24

### Fixed

- Kept the TTS apply action visible in the automatic messages header.
- Tightened panel layout constraints so form controls and notice actions stay inside their cards.
- Made the release validation script use the bridge Python runtime with the required TikTok dependencies.

### Added

- Added project-level agent map and routing policy.
- Added shared skill catalog for agent and human workflows.
- Added release policy and release manifest schema.
- Added backup and restore runbooks.
- Added backup and release manifest helper scripts.
- Added versioned release preparation flow for Windows installer, portable ZIP, checksums, and manifest outputs under `dist/releases/<version>`.
- Added contributing guide and roadmap.

### Fixed

- Corrected TikTok timestamp normalization so live events are no longer clamped to `1000000`.
- Expanded TikTok chat ingestion to cover alternate chat event classes.
- Hid timestamps from the activity monitor UI.

### Operational

- Initialized Git history for the project baseline.
- Added a complete operational backup on Desktop after the TikTok monitor fix.
