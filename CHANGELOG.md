# Changelog

All notable Panel Live changes should be recorded here.

Format follows a lightweight Keep a Changelog style. Versions use SemVer.

## 0.2.1 - 2026-06-23

### Fixed

- **Live Timer UI**: Manual adjust simplified — removed preset buttons (+30s/+60s/+5min/-30s/-60s), replaced with `−` / `+` sign buttons + numeric input.
- **Live Timer UI**: Font color inputs changed from text field to native `<input type="color">` picker.
- **Live Timer UI**: Font family inputs changed from text field to `<select>` dropdown with 10 web-safe options (Segoe UI, Arial, Helvetica, Verdana, Trebuchet MS, Courier New, Consolas, Georgia, Impact, Times New Roman).
- **Live Timer UI**: Background color removed entirely — overlay stays transparent (hardcoded `"bgColor":"transparent"`).
- **Live Timer UI**: Config details panel overflow fixed (`max-height: 380px; overflow-y: auto`).
- **C++**: Removed `background_color` from `LiveTimerGameState`, config key `kBackgroundColor`, and all parser/serializer code.
- **Tests**: Removed `test_default_config_background_color`.

## 0.2.0 - 2026-06-23

### Added

- **Live Timer: max_time_s**: New config field caps the total accumulated time.
  When set > 0, any addition that would exceed the cap is clamped.
- **Live Timer: manual time adjust**: `POST /api/timer/adjust` endpoint with
  delta support. UI includes quick buttons (+30s/+60s/+5min/-30s/-60s) and
  a custom input field. Negative deltas supported.
- **Live Timer: reset config to defaults**: `POST /api/timer/reset-config`
  restores all timer settings to factory defaults.
- **Live Timer: visual style fields in UI**: Font size, color, family, and
  bold for title, counter, and subtitle are now editable in the config form
  and sent to `POST /api/timer/configure`.
- **Live Timer: background color**: `background_color` now configurable via
  UI and accepted by the configure endpoint.
- **Live Timer: overlay connection error banner**: Red banner appears when
  the overlay loses connection to the panel.
- **Live Timer: event dedup in overlay**: Events carry a monotonic `id`;
  overlay skips already-shown popups on reconnect.
- **Live Timer: confirm on restart**: If the timer is running and the user
  clicks Start, a confirmation dialog prevents accidental reset.
- **AGENTS.md Section 5.5**: Build without re-installation rule — agent must
  check existing build before re-running cmake/vcpkg.

### Changed

- **Live Timer: overlay_host default**: Changed from `"127.0.0.1"` to
  `"localhost"` in `PanelConfig`, matching the documented behavior and
  fixing overlay URL generation in the snapshot.
- **Live Timer: substitute_placeholders extracted**: Now a shared non-anonymous
  function `substitute_timer_placeholders()` in the `nlp3::games` namespace,
  used by both the game code and overlay_assets.cpp (was duplicated).
- **Live Timer: kLiveTimerGameId constant**: All hardcoded `"live-timer"`
  strings replaced with the named constant.

### Fixed

- **Live Timer: remaining_seconds() now triggers completion**: When the
  countdown reaches 0, `remaining_seconds()` automatically sets
  `running=false, completed=true`. Previously `poll_completion_sound()` was
  the only path to detect expiry, causing the overlay to show stale values.
- **Live Timer: format_time() uses floor(), not ceil()**: Display was
  rounding up (e.g. 1.1s → 2s shown). Now truncates correctly.
- **Live Timer: pause() captures remaining before pausing**: The paused
  snapshot was stale because `remaining_seconds()` was called after setting
  `paused=true` (which stopped the clock). Now called before the state change.
- **Live Timer: set_enabled() resets full state**: Previously only toggled
  flags; now also resets `remaining_seconds`, clears `recent_events`, and
  zeroes `total_time_added`.
- **Live Timer: clamp all numeric config values**: timeouts, volumes, and
  font sizes are now clamped to sane ranges on the server side.
- **Live Timer: overlay style comparison**: Fixed object reference comparison
  that never detected style changes after the first render. Now uses
  `styleEqual()` deep comparison function.

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

## 0.1.11 - 2026-06-23

### Added

- **Live Timer**: New standalone countdown timer module independent of the game system.
  Always visible in UI (center column), always running regardless of active game.
  - `LiveTimerGame` class with real-time countdown (`steady_clock`), configurable time,
    time-per-event extensions (like/share/follow/gift/chat), completion sound polling.
  - Panel UI section between "Actividad del live" and "Métricas" with collapsible config.
  - Overlay HTML (`/overlay/live-timer`) for TikTok Live Studio browser source:
    transparent background, color thresholds, animated popups on event, completion banner.
  - REST endpoints: `GET /api/timer/config`, `POST /api/timer/configure`,
    `GET /api/overlay/live-timer/state`.
  - `PanelTimerStatus` in snapshot with `has_timer`, `remaining_seconds`, `running`, etc.
  - 14 unit tests + 1 API smoke test.

### Fixed

- **UI asset embedding**: `embed_text_asset.cmake` now generates `.inc` files for new
  overlay text assets automatically.
- **Live Timer**: `overlay_host` in `panel_config` replaces hardcoded `127.0.0.1` in overlay URL,
  allowing configurable bind address for TikTok Live Studio browser source access.
- **Live Timer**: `SND_LOOP` sound now stops properly on reset/disable/stop via `PlaySound(nullptr,0,0)`.
- **Live Timer**: Added `POST /api/timer/stop` endpoint; `total_time_added` telemetry now accumulates real deltas.
- **Live Timer**: `on_complete_video_url` and all visual style fields (font size/color/family/bold for
  title/counter/subtitle) now accepted by `POST /api/timer/configure`.
- **Live Timer**: Completion sound beep fallback restored when no sound file is configured.

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
