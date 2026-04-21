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
- `max_queue_size`
- `backend_queue_size`
- `max_dispatch_per_tick`
- `max_text_length`
- `drop_oldest_on_overflow`
- `selected_voice_id`
- `selected_language`
- `frequency`

### `tts`

- `allow_chat_messages`
- `allow_scheduled_messages`
- `allow_manual_messages`
- `include_actor_name_for_chat` (legacy; chat TTS now keeps usernames out of spoken audio)
- `min_text_length`
- `chat_filter_mode`
- `chat_cooldown_ms`
- `chat_message_template` (`{user}` is ignored for chat reads; default is `{message}`)

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

- `GET /api/tts/config`

Write:

- `POST /api/tts/config`
- `POST /api/tts/test`

The compact UI uses these endpoints directly.

## UI flow

In `Voice assistant` the operator can:

- enable or disable voice
- pick a voice
- pick a language profile
- change frequency
- choose what events are read
- choose who can be read from chat
- edit message templates
- trigger `Test voice`

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
