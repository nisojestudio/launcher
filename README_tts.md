# TTS Real

## Overview

Nisoje Studio now uses a real Windows TTS backend inside the current `src/tts` architecture.

The legacy project was used as the functional source for:

- curated voice profiles
- chat read modes
- message-template behavior
- event importance rules

The current project remains the source of truth for runtime orchestration:

`UI / config -> PanelApp -> HostRuntime -> TtsPolicy -> TtsScheduler -> RealTtsBackend`

## Main files

- `src/tts/real_tts_backend.*`
- `src/tts/voice_catalog.*`
- `src/tts/tts_template_formatter.*`
- `src/tts/tts_service.*`
- `src/host/host_automation.*`
- `src/platform/panel_http_server.cpp`
- `src/platform/panel_http_json.cpp`

## Available voice profiles

The UI and config use a curated catalog:

- `spanish-female`
- `spanish-male`
- `spanish-neutral`
- `english-female`
- `english-male`

Each profile exposes:

- internal id
- display name
- language
- gender
- availability

Availability depends on the Windows voices installed on the target machine.

## Windows runtime dependency

The real backend uses Windows SAPI.

That means:

- no external cloud TTS dependency is required
- the app uses the voices already installed in Windows
- Spanish playback requires Spanish TTS voices installed on the machine

On this validation machine the backend reported:

- English voices available
- Spanish voices unavailable until extra Windows language capabilities are installed

## Install Spanish voices on Windows

Run as Administrator:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\install_tts_voices.ps1
```

This script installs common Spanish text-to-speech capabilities and prints the installed voice inventory afterwards.

## Config fields

`panel_config.json` now persists TTS in four blocks.

### `tts_runtime`

- `enabled`
- `max_queue_size` (default 50; `0` = unlimited)
- `backend_queue_size` (default 20; `0` = unlimited)
- `max_message_age_ms` (default 30000; `0` = TTL disabled; stale queued messages are dropped)
- `max_dispatch_per_tick`
- `max_text_length` (UTF-8 safe truncation applied to `text` and `content_text`)
- `drop_oldest_on_overflow`
- `selected_voice_id`
- `selected_language`
- `frequency`
- `volume` (0–100, default 100; Windows SAPI volume; omitted in old configs → 100)
- `test_rate_limit_ms` (default 1000; window for `POST /api/tts/test`; `0` = no limit; omitted in old configs → 1000)

### `tts`

- `allow_chat_messages`
- `allow_scheduled_messages`
- `allow_manual_messages`
- `include_actor_name_for_chat` (live flag: when `false`, `{user}` in the chat template is filled empty; template is always applied — see P2.2)
- `min_text_length`
- `chat_filter_mode`
- `chat_cooldown_ms` (exposed as `chatCooldownMs` in `/api/tts/config` and the panel chat row; measured on **receive wall clock**, not `source_timestamp_ms`)
- `chat_message_template` (always applied; empty `{user}` when actor name is off → sanitize collapses `"Dice : hola"` to `"Dice: hola"`)

### `automation`

- `enable_gift_thanks_tts`
- `enable_follow_thanks_tts`
- `enable_subscriber_thanks_tts`
- `enable_share_thanks_tts`
- cooldowns per event type
- message templates per event type

### `periodic_tts`

- `enabled`
- `interval_ms`
- `messages`

## Template variables

Supported placeholders:

- `{user}`
- `{message}`
- `{gift}`
- `{count}`
- `{viewers}`

Unknown placeholders are safely ignored.

Chat template is **always applied** (P2.2). When `include_actor_name_for_chat`
is `false`, `{user}` becomes empty and sanitizer collapses the leftover spacing.

## Event reading rules

Current priority order:

- gifts
- subscribers
- follows / shares
- manual announcements
- chat
- periodic reminders

Chat reads now speak only sanitized message text. Usernames, emojis, icons and unsupported symbols are filtered out before synthesis.

Current chat filters:

- `everyone`
- `followers_only`
- `subscribers_only`
- `moderators_only`

Follower / subscriber / moderator flags are now supported end to end in the external event contract.

## HTTP endpoints

Read:

- `GET /api/tts/config` — returns runtime + voices catalog
- `GET /api/tts/config?refresh=1` — re-scans installed SAPI voices before responding (M7)

Write:

- `POST /api/tts/config`
- `POST /api/tts/test` — rate-limited by `tts_runtime.test_rate_limit_ms` (default 1 s; `0` = no limit; HTTP 429 on spam). State lives on `PanelApp` (not a process-wide static) and resets on `apply_live_config`.

Saving voice config (`POST /api/tts/config` / `POST /api/host/tts`) applies live
**without** purging the TTS queue or resetting the periodic timer (#3). The
periodic engine only resets on re-enable or interval change
(`apply_periodic_tts_config`). Explicit purges remain on
`clear_pending_live_backlog` (disconnect/reconnect paths).

`energyLevel` is **not** accepted as a free body field on `POST /api/host/tts` /
`POST /api/tts/config`. Energy only changes through actions (`boost_hype`,
`calm_mode`), so a hardcoded UI value cannot reset the mode (P1.6+).

The compact UI uses these endpoints directly.

## Event metadata note (B6)

External/bridge events carry `metadata.source_event_type` (e.g. `comment`, `gift`,
`follow`, `like`, `share`, `join`, `subscribe`, …). Automation uses it to detect
subscriber thank-yous (`source_event_type` containing `subscribe`). Keep the field
stable in the external contract and in `tools/bridge_py`.

## Automation cooldowns

Gift / follow / like / share / subscriber thank-you cooldowns use the **wall clock
at evaluation time**, not `metadata.source_timestamp_ms` (same class of fix as
chat cooldown M2). Events with `timestamp=0` no longer bypass the cooldown.

## NullTtsBackend note (B1)

`src/tts/null_tts_backend.*` has **no runtime use**. It remains in
`src/tts/CMakeLists.txt` only to avoid a CMake re-configure (AGENTS.md §5.5).
Do not expose it in the UI or treat it as a production fallback.

## UI flow

In `Voice assistant` the operator can:

- enable or disable voice
- pick a voice
- pick a language profile
- change frequency
- set SAPI volume with the `Volumen` slider (0–100, B7 — **0 is a real mute**; the UI no longer coerces it to 100)
- toggle `Incluir nombre de quien habla` (`includeActorName`, B3 — controls whether `{user}` is filled in chat templates)
- choose what events are read
- choose who can be read from chat
- edit message templates
- trigger `Test voice` (rate-limited by `test_rate_limit_ms`, default 1 s)

## Practical validation

### Build

```powershell
cmake --build --preset default
```

### Tests

```powershell
ctest --test-dir build --output-on-failure
```

### Manual API check

```powershell
.\build\src\platform\NisojeStudio.exe --ui --no-browser --ui-port 18924
Invoke-WebRequest http://127.0.0.1:18924/api/tts/config
Invoke-WebRequest -Method Post -ContentType 'application/json' -Body '{"message":"Nisoje Studio voice test"}' http://127.0.0.1:18924/api/tts/test
```

## Notes

- The backend is non-blocking for the live pipeline: synthesis/playback happens behind the scheduler and backend queue.
- If the selected voice profile is unavailable, the backend falls back to any installed voice of the selected language, then to any available installed voice.
- Subscriber thank-you messages currently depend on the external bridge marking subscriber events or source types appropriately.
- Chat cooldown uses receive wall clock (`enqueue_chat_read`), so events with `source_timestamp_ms = 0` or skewed source clocks cannot bypass the window.
- Periodic TTS arms on the first tick and emits only after `interval_ms` has elapsed (epoch-safe; no instant first emit).
