# Live Timer Module Contract

Last updated: 2026-09-24 (contract reconciled against the code; see the fix
pass notes inline).

This document freezes the runtime and HTTP contract for the `live_timer` game
module after the audit/fix pass. It is the source of truth when changing the
backend, the JSON payload or the overlay HTML.

## 1. State ownership

`LiveTimerGameState::remaining_seconds` is the **single source of truth (SSOT)**
for the timer's remaining time in every mode:

| Mode | `state_.running` | `state_.paused` | `state_.completed` | `state_.remaining_seconds` |
|------|------------------|-----------------|--------------------|----------------------------|
| Idle (never started) | false | false | false | `0.0` until armed; `arm()` / an `initial_time_s` change while idle set it to `initial_seconds` |
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
- `on_activated()` **continues** from the current remaining when it is `> 0`
  (restored time wins); it only falls back to `initial_seconds` on a clean
  start. If there is no time to count it stays not-running.

## 2. Event id monotonicity

`event_id_counter_` is **monotonic across**:

- `on_activated()` (multiple activations in one process).
- `arm()` and `restore_state()`.
- Process restarts (persisted in `live-timer.json`, see section 8).

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
| `initial_time_s` | Only adjusts `state_.remaining_seconds` when the timer is **idle** (`!running && !paused && !completed`) **and** the value actually changed. While running, paused, completed or hidden the SSOT is left intact; the new initial is adopted on the next `arm()` / `on_activated()`. |
| `max_time_s` | Does NOT move the clock by itself (the clamp lives in `on_game_input_event` and `adjust_time`, where the applied delta can be reported honestly). |
| `floor_time_s` | Reconciled against `max_time_s`: if `floor > max` (with `max > 0`) the floor is clamped down to the max and the config is rewritten. |
| `time_per_*` | Applied to subsequent events. Already-pending buffered events are not retro-applied. |
| `*_effect`, `*_glow_enabled`, `glow_*`, `pulse_*`, `particles_*` | Applied on the next overlay poll when the snapshot differs. |
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
| GET | `/api/timer/config` | Returns the current `live_timer` config (used by export). Requires access when auth is enabled (same gate as POST). |
| POST | `/api/timer/configure` | Partial config update. JSON body. Returns `{ok, message, warnings}`. |
| POST | `/api/timer/start` | `on_activated()`. |
| POST | `/api/timer/pause` | `pause()`. |
| POST | `/api/timer/resume` | `resume()`. |
| POST | `/api/timer/reset` | `reset()` (resets to `initial_seconds`, starts running). |
| POST | `/api/timer/stop` | `stop()` (forces completed, remaining=0). |
| POST | `/api/timer/adjust` | Body: `{"delta": <seconds>}`. Returns `{ok:true,"message":"adjusted"}` with the actually-applied delta, or `{ok:false,"message":"adjust_blocked"}` when the engine ignored it (paused, completed, hidden, clamp to zero) / `delta_zero` for `delta == 0`. |
| POST | `/api/timer/reset-config` | `reset_config_to_defaults()`. |
| POST | `/api/timer/toggle` | Toggles `enabled` (show/hide the overlay). |
| POST | `/api/timer/simulate` | R4: injects a synthetic event. Body `{"kind":"gift|like|share|follow|chat","coins":<n>,"name":"..."}`. `coins` is clamped to `[0, UINT32_MAX]`; `coins <= 0` is a no-op (never wraps). |
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
| `*_effect` | string | One of `none\|glow\|pulse\|heartbeat\|float\|flicker\|shake`. Backend normalizes invalid values to `none` and reports in `warnings`. |
| `*_glow_enabled`, `glow_color`, `glow_intensity_px` | various | Per-element glow. |
| `pulse_speed_s` | number | Pulse period for the `pulse` effect. |
| `digit_effect` | string | One of `none\|flip\|roll\|pop\|fade\|odometer\|typewriter\|blur` (see section 10.1). |
| `color_preset` | string | One of `neon-green\|cyber-blue\|clean-white\|rose-gold` (see section 10.2). |
| `particles_enabled`, `particle_count`, `particle_color` | various | Global particle system. |
| `tick_sound_path`, `tick_sound_volume` | string/number | See section 3. |
| `add_sound_path`, `add_sound_volume` | string/number | See section 3. |
| `on_complete_sound_path`, `on_complete_volume`, `on_complete_repeat` | string/number/bool | See section 3. |
| ~~`counter_font`~~ | ~~string~~ | **Removed** — merged into `counterStyle.font_family`. The panel sends the chosen font via `counter_font_family`. |

## 7. Threading

`LiveTimerGame` is **single-threaded by convention**. There is no internal mutex.
`build_live_timer_state_json` uses `const_cast` to call `tick()`; this is safe
as long as all callers (HTTP polling loop, telemetry, telemetry snapshot,
config endpoints) run on the panel's main thread. Any future second thread
emitting events must add external synchronization.

## 8. Persistence

The save lives at `%LOCALAPPDATA%\NisojeStudio\timer\live-timer.json`
(overridable with the `NLP3_TIMER_STATE_DIR` env var for tests; a legacy save
in `%TEMP%\NisojeStudio\live_timer_save.json` is migrated once on first run).
A `.bak` snapshot of the previous good save is rotated on every write.

JSON shape:

- `version`: int (currently **3**; v2 files still load). Since v3 the clock
  freezes on close and the saved `running` flag is **ignored on load** — the
  panel never auto-resumes; wall-clock compensation is gone.
- `config`: full key-value snapshot of `LiveTimerGame::config()`.
- `state`:
  - `remaining_seconds` (committed via `tick()` before save).
  - `running`, `paused`, `completed`, `enabled`.
  - `saved_at_ms` (wall-clock at save; informational only since v3).
  - `event_id_counter`, `session_id`, `total_time_added`.

Write strategy: atomic (`.tmp` + rename) with `.bak` rotation; on load the
primary file is tried first, then `.bak`, then defaults. Saves happen:

- on every configure/start/pause/resume/reset/stop/adjust/simulate/toggle endpoint,
- on `PanelApp` shutdown (timer state is flushed before anything else is torn
  down),
- autosave every **10 s while running**, or every **30 s** when only
  events/config changed.

On load the timer **never auto-starts**: the remaining time is preserved and
the operator presses Start to continue (contract test `cp6`).

## 9. Known limitations (post-fix)

- `on_activated()` clears `recent_events` (the visible popup buffer) but
  preserves `event_id_counter_`. New IDs continue monotonically.
- Effects reset state (`confettiSpawned`, `effectState`, `lastShownEventId`)
  is bound to `sessionId` change in the overlay. Reloading the overlay page
  loses in-flight effect deltas until the next server poll.
- The `panel_http_ui_test` integration test is environment-dependent
  (requires free port 18881). Not a regression of the timer module.
- Caps (`cap_per_user_per_minute_s` / `cap_total_per_minute_s`) use a **fixed
  60 s sliding window** (`kCapWindowS`); the cap *value* is the budget of
  seconds allowed per minute, never the window length.
- Gift time consumes `diamond_count` as the **total** coins of the event
  (fallback to `quantity` when 0). **Verified for `tiktools`** (production
  provider): tik.tools documents `diamondCount` as the *unit* price and the
  official total as `diamondCount * repeatCount`, so the Python mapper
  (`tiktools_connection._ws_event_to_canonical`) converts to a per-frame
  total with streak-delta state before the event reaches C++. The `direct`
  (TikTokLive) and `euler` adapters have NOT been re-verified against this
  contract — if you switch providers, validate against a real payload first.

## 10. V3 Visual Enhancements (digit effects, palettes, fonts)

Added post-audit 2026-07-09. These fields are fully validated at every layer.

### 10.1 `digit_effect`

Per-digit transition animation when the counter changes.

| Value | Effect |
|-------|--------|
| `none` | No transition |
| `flip` | 3D flip animation (rotateX, 0.4s) |
| `roll` | Slot-machine roll (translateY + blur, 0.3s) |
| `pop` | Scale bounce (scale 1→1.15→1, 0.2s) |
| `fade` | Opacity fade (0→1, 0.3s) |
| `odometer` | Odometer roll (0.35s) |
| `typewriter` | Typewriter steps (0.16s) |
| `blur` | Blur in (0.25s) |

**Validation:** Backend normalizes invalid values to `none`. Overlay has defensive CSS class check.
**Config key:** `digit_effect` (string)
**JSON field:** `digit_effect`
**Default:** `"none"`

### 10.2 `color_preset`

Predefined color palette for the counter digits.

| Value | Counter | Warning | Danger |
|-------|---------|---------|--------|
| `neon-green` | `#00FF88` | `#FF8C00` | `#FF4444` |
| `cyber-blue` | `#00D4FF` | `#FFB800` | `#FF3366` |
| `clean-white` | `#FFFFFF` | `#FFCC00` | `#FF4444` |
| `rose-gold` | `#FF6B8A` | `#FFB347` | `#CC1144` |

**Validation:** Backend normalizes invalid values to `neon-green`. Overlay has defensive fallback.
**Config key:** `color_preset` (string)
**JSON field:** `color_preset`
**Default:** `"neon-green"`

### 10.3 Counter font (merged into `counter_font_family`)

The `counter_font` selector was removed and its 4 mono font options merged into the
existing `counter_font_family` (the "Fuente" dropdown for the counter). The dropdown
now includes these mono fonts alongside the standard options:

| Value | CSS value | Source |
|-------|-----------|--------|
| `"Space Mono", monospace` | `"Space Mono", monospace` | Google Fonts (wght@400;700) |
| `"JetBrains Mono", monospace` | `"JetBrains Mono", monospace` | Google Fonts (wght@400;700) |
| `"Share Tech Mono", monospace` | `"Share Tech Mono", monospace` | Google Fonts |
| `Segoe UI, monospace` (selected) | `Segoe UI, monospace` | System font |

**Behavior:** The "Tipografía" separate dropdown (`#timer-counter-font`) no longer
exists. All font selection for the counter is done via `counter_font_family`.
Google Fonts link loads all mono families for use in both title and subtitle dropdowns.
The backend removed the `counter_font` config key entirely; any old JSON payloads
containing `counter_font` are ignored (not read, not persisted).

**Config key:** N/A (removed). Use `counter_font_family` instead.
**JSON field:** N/A (removed).
**Default:** `"Segoe UI, monospace"` (from `counter_family` default).
