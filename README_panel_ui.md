# Panel UI

## Overview

Nisoje Studio now opens with a simple commercial-style screen designed for streamers who do not want to think about technical setup.

The main idea is:

1. add a TikTok username
2. press `Connect`
3. turn on the voice assistant
4. optionally pick a game
5. watch the live activity update in real time

The UI is still served by the existing C++ runtime through `panel_http_server`, and it still runs inside the native WebView2 desktop window.

Source assets live in:

- `src/platform/ui/index.html`
- `src/platform/ui/styles.css`
- `src/platform/ui/app.js`

They are embedded at build time into `panel_ui_assets`.

## Screen structure

### 1. Connection

This is the first thing the user sees.

It shows:
- TikTok username input
- `Connect` button
- `Disconnect` button when the live is already attached
- connection status
- live room id
- short panel health summary

### 2. Live activity

The next row gives fast numbers:

- Viewers
- Gifts
- Messages
- Shares
- Latency

These values update live from the existing `/api/metrics` endpoint.

### 3. Voice assistant

This section keeps voice controls simple:

- voice on/off
- voice preset
- language profile
- energy slider
- read gifts
- read new followers
- read subscribers
- read shared live events
- read chat messages
- read chat from:
  - everyone
  - followers only
  - subscribers only
  - moderators only
- periodic message interval
- `Edit messages`
- `Test voice`

The message editor lets the operator customize:

- gift message
- follower message
- subscriber message
- share message
- chat message
- periodic messages

Those values are sent through the TTS API and saved back into panel config.

### 4. Interactive game

Games are shown as friendly cards instead of technical manifests.

Each card includes:

- demo image area rendered with local HTML/CSS
- game title
- short description
- status badge
- `Select` or `Active` button

The active game summary stays visible above the game list so the user always knows what is selected.

### 5. Advanced settings

This section stays collapsed by default.

It contains:

- system health checks
- recent logs
- quick extra actions
- command input for advanced operators

## Visual system

- background: `#0b1220`
- main panel: `#111827`
- soft surface: `#0f172a`
- accent: `#22c55e`
- text primary: `#e5e7eb`
- text secondary: `#9ca3af`
- rounded corners: `8px` to `18px`
- font stack: `Inter, system-ui, sans-serif`

The layout favors:

- large primary actions
- quick scanning
- minimal clutter
- clear dark-theme contrast
- stable live updates without layout jump

## Runtime contract

The UI keeps using the existing routes:

### Read

- `GET /`
- `GET /app.css`
- `GET /app.js`
- `GET /api/state`
- `GET /api/events`
- `GET /api/metrics`
- `GET /api/tts/config`
- `GET /health`
- `GET /status`

### Actions

- `POST /api/command`
- `POST /api/game/start`
- `POST /api/game/pause`
- `POST /api/game/reset`
- `POST /api/game/trigger`
- `POST /api/host/tts`
- `POST /api/tts/config`
- `POST /api/tts/test`
- `POST /api/system/reconnect`

Polling remains:

- `/api/state` every `1000ms`
- `/api/events` every `250ms`
- `/api/metrics` every `250ms`

## How to use it

### Start the panel

```powershell
cd "<repo-root>"
.\build\src\platform\NisojeStudio.exe
```

### Connect a live

1. Type the TikTok username.
2. Press `Connect`.
3. The UI will prepare the live connection and start the Python runner.

### Customize the voice assistant

1. Turn `Voice on`.
2. Choose the voice, language and read options you want.
3. Press `Save voice settings`.
4. Use `Read chat from` if you want to limit who can be read from chat.
5. Use `Edit messages` if you want custom gifts, followers, subscribers, shares, chat or periodic lines.
6. Press `Test voice` to validate the current profile.

### Activate a game

1. Go to `Interactive game`.
2. Press `Select` on the game card you want.
3. Use `Pause` or `Reset` from the active game area when needed.

## Extension rules

- Keep simple language in visible UI.
- Do not surface internal ids, raw manifests, or transport details in the main screen.
- Prefer adding structured fields to `/api/state` if a new simple control truly needs backend data.
- Keep advanced or technical workflows inside `Advanced settings`.
- Do not add external CDN assets or heavy frontend frameworks.

## Validation

Validated in this workspace after the simplification pass:

- UI asset embedding build
- HTTP/UI endpoint test
- TTS config HTTP roundtrip
- TTS voice test endpoint
- full C++ test suite
- browser-side validation with the real local panel
- no console errors during page load
- live metrics and activity rendering after injected events
