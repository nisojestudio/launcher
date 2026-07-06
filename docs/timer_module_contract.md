# Live Timer Module Contract

Last updated: post audit + fix pass (5 phases).

This document freezes the runtime and HTTP contract for the `live_timer` game
module after the audit/fix pass. It is the source of truth when changing the
backend, the JSON payload or the overlay HTML.

## 1. State ownership

`LiveTimerGameState::remaining_seconds` is the **single source of truth (SSOT)**
for the timer's remaining time in every mode:

| Mode | `state_.running` | `state_.paused` | `state_.completed` | `state_.remaining_seconds` |
|------|------------------|-----------------|--------------------|----------------------------|
| Idle (never started) | false | false | false | `0.0` (set by `on_activated` when start fires) |
| Running | true | false | false | baseline - elapsed |
| Paused | false | true | false | frozen at pause time |
| Completed | false | false | true | `0.0` |
| Hidden (set_enabled false) | false | false | false | preserved |

Invariants:

- `remaining_seconds()` (const) is a **pure read**; it never mutates state.
- `tick()` is the **only** mutator that advances SSOT for elapsed time. It is
  called by the polling loop (`build_live_timer_state_json`) and from
  `poll_completion_sound` / `poll_tick_sound`.
- `pause()` writes the live remaining into the SSOT immediately so reads during
  pause return a stable value.
- `set_enabled(false)` enters `hidden_` mode but **preserves** `remaining_seconds`,
  `recent_events`, `event_id_counter_`, `total_time_added_`. The operator must
  call `on_activated()` to start a new run; it does not auto-resume.

## 2. Event id monotonicity

`event_id_counter_` is **monotonic across**:

- `on_activated()` (multiple activations in one process).
- `arm()` and `restore_state()`.
- Process restarts (persisted in `live_timer_save.json`).

The overlay's `lastShownEventId` is reset whenever the JSON `sessionId` field
changes. `sessionId` is regenerated on:

- `on_activated()` (each new run).
- `arm()` (panel-side explicit re-arm).

Backwards compatibility: older save files without `session_id` get a fresh one
generated at load time, which still resets the overlay cursor (intended).

## 3. Sound playback

**Backend never plays audio.** All sound effects are reproduced by the overlay
in HTML5 (`new Audio(path)`). The backend only ships these config fields in
`/api/overlay/live-timer/state`:

| JSON field | Source |
|------------|--------|
| `tick_sound_path` | `state_.tick_sound_path` |
| `tick_sound_volume` | `state_.tick_sound_volume` (clamped 0..1 by overlay) |
| `add_sound_path` | `state_.add_sound_path` |
| `add_sound_volume` | `state_.add_sound_volume` |
| `on_complete_sound_path` | `state_.on_complete_sound_path` |
| `on_complete_volume` | `state_.on_complete_volume` |
| `on_complete_repeat` | `state_.on_complete_repeat` |

Empty path == total silence. No fallback beep exists; this is by design to keep
the polling loop non-blocking and to give the operator full control.

Tick sound is fired when `remainingSeconds <= 60` and once per integer second
transition, with a per-second cursor (`lastTickSecondPlayed` in the overlay).

Completion sound fires when the JSON transitions to `completed=true`. The path
change re-arms the audio element. The cursor is released when leaving completed.

## 4. `apply_config` semantics (live edits)

| Field | Effect on runtime |
|-------|-------------------|
| `initial_time_s` | Only adjusts `state_.remaining_seconds` when the timer is **actively running** (`running && !paused && !completed`). When paused, completed or hidden, the SSOT is left intact; the new initial takes effect on the next `on_activated()`. |
| `max_time_s` | Immediately clamps `state_.remaining_seconds` to the new ceiling when `max_time_s > 0`. |
| `time_per_*` | Applied to subsequent events. Already-pending buffered events are not retro-applied. |
| `*_effect`, `*_glow_enabled`, `glow_*`, `wave_*`, `pulse_*`, `shake_intensity`, `particles_*` | Applied on the next overlay poll when the snapshot differs. |
| `*_font_size/color/family/bold` | Applied on the next overlay poll. |
| Sound paths/volumes | Re-applied on the next overlay poll; audio elements are recreated on path change. |
| `on_complete_text/color/size` | Applied on the next poll, including while in `completed` state. |
| `popup_add_color` / `popup_subtract_color` | Applied on the next popup. |

`handle_timer_configure` returns:

```json
{"ok": true, "message": "config_applied", "warnings": ["<key> normalized from '<v1>' to '<v2>'", ...]}
```

The `warnings` array lists every key the backend normalized/clamped. The UI
shows the warning count and logs to `console.warn` for inspection.

## 5. HTTP endpoints

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/timer/config` | Returns the current `live_timer` config (used by export). |
| POST | `/api/timer/configure` | Partial config update. JSON body. Returns `{ok, message, warnings}`. |
| POST | `/api/timer/start` | `on_activated()`. |
| POST | `/api/timer/pause` | `pause()`. |
| POST | `/api/timer/resume` | `resume()`. |
| POST | `/api/timer/reset` | `reset()` (resets to `initial_seconds`, starts running). |
| POST | `/api/timer/stop` | `stop()` (forces completed, remaining=0). |
| POST | `/api/timer/adjust` | Body: `{"delta": <seconds>}`. `adjust_time()`. |
| GET | `/api/overlay/live-timer/state` | Returns the JSON consumed by the overlay (see section 6). |

## 6. Overlay JSON contract

Top-level fields (relevant subset):

| Field | Type | Notes |
|-------|------|-------|
| `remainingSeconds` | number | Computed (baseline - elapsed) when running; SSOT otherwise. |
| `format` | string | `HH:MM:SS` or `N dia HH:MM:SS`. |
| `running` | bool | |
| `paused` | bool | |
| `completed` | bool | Committed by `tick()`. |
| `enabled` | bool | `!hidden_`. |
| `sessionId` | int64 | Changes when the operator starts a new run. |
| `title`, `subtitle` | string | `subtitle` has placeholders resolved server-side. |
| `titleStyle`, `counterStyle`, `subtitleStyle` | object | `{font_size_px, font_color, font_family, bold}`. |
| `popupAddColor`, `popupSubtractColor` | string | Hex. |
| `completedText`, `completedTextColor`, `completedTextSize` | string/int | |
| `recentEvents` | array | Each `{id, icon, label, delta, isAddition}`. Overlay filters by `id > lastShownEventId`. |
| `*_effect` | string | One of `none|glow|pulse|shake|wave`. Backend normalizes invalid values to `none` and reports in `warnings`. |
| `*_glow_enabled`, `glow_color`, `glow_intensity_px` | various | Per-element glow. |
| `wave_colors`, `pulse_speed_s`, `shake_intensity` | various | Effect parameters. |
| `particles_enabled`, `particle_count`, `particle_color` | various | Global particle system. |
| `tick_sound_path`, `tick_sound_volume` | string/number | See section 3. |
| `add_sound_path`, `add_sound_volume` | string/number | See section 3. |
| `on_complete_sound_path`, `on_complete_volume`, `on_complete_repeat` | string/number/bool | See section 3. |

## 7. Threading

`LiveTimerGame` is **single-threaded by convention**. There is no internal mutex.
`build_live_timer_state_json` uses `const_cast` to call `tick()`; this is safe
as long as all callers (HTTP polling loop, telemetry, telemetry snapshot,
config endpoints) run on the panel's main thread. Any future second thread
emitting events must add external synchronization.

## 8. Persistence

`live_timer_save.json` (next to `panel_config.json`) contains:

- `version`: int (currently 1).
- `config`: full key-value snapshot of the current `LiveTimerGame::config()`.
- `state`:
  - `remaining_seconds` (committed via `tick()` before save).
  - `running`, `paused`, `completed`, `enabled`.
  - `saved_at_ms` (wall-clock at save).
  - `event_id_counter`, `session_id`, `total_time_added`.

The save is rewritten on every configure/start/pause/resume/reset/stop/adjust
operation.

## 9. Known limitations (post-fix)

- `on_activated()` clears `recent_events` (the visible popup buffer) but
  preserves `event_id_counter_`. New IDs continue monotonically.
- Effects reset state (`confettiSpawned`, `effectState`, `lastShownEventId`)
  is bound to `sessionId` change in the overlay. Reloading the overlay page
  loses in-flight effect deltas until the next server poll.
- The `panel_http_ui_test` integration test is environment-dependent
  (requires free port 18881). Not a regression of the timer module.
