# Changelog

All notable Panel Live changes should be recorded here.

Format follows a lightweight Keep a Changelog style. Versions use SemVer.

## 0.1.7 - 2026-06-11

### Added

- Device activation on login: Panel Live 3.0 now calls `/api/license/activate` after successful license validation.
  Each PC running the launcher registers itself as a device tied to the license, preventing multi-account abuse.
- New config field `license_activate_path` (default `/api/license/activate`) in panel config.
- Device activation errors are surfaced to the UI as warnings (limit reached, device already registered on another account).

### Fixed

- Removed `SECURITY_FLAG_IGNORE_REVOCATION` usage in `win_http_client.cpp` — constant was removed in Windows 11 24H2 SDK (10.0.26100.0).
  WinHTTP now uses default certificate revocation checking, which is the correct security posture.

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
