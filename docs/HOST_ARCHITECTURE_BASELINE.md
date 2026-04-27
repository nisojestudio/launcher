# Host-First Architecture Baseline

## Product direction

Nisoje Studio is now treated as an installable Windows host platform for live games, not as a core-first abstract engine.

Target flow:

`TikTok -> Bridge -> Normalizer -> Host Runtime -> Game`

## Minimal module structure introduced

- `src/events`
  Defines `HostEvent`, actor metadata and normalized host-facing event data.
- `src/host`
  Defines `HostSessionState`, `HostRuntime` and the central event routing point.
- `src/gamesdk`
  Defines `IGameModule`, `GameManifest`, `GameCompatibility` and `IGameFactory`.
- `src/tts`
  Defines `ITtsService`, the boundary for chat reading and scheduled speech.
- `src/bridge`
  Defines `IBridgeAdapter`, the external adapter boundary for TikTok and future sources.
- `src/bridge/tiktok_*`
  Separates the TikTok raw event model, bridge config, raw-to-normalized mapper, and now a formal host-managed stub session with lifecycle, polling, metrics and fault surface.
- `src/live`
  Temporarily reused as the normalization layer that converts external live events into `HostEvent`.
- `src/platform`
  Owns app startup and the runtime manifest for the installable host executable.
- `src/host/local_game_registry.*`
  Registers local game factories, exposes manifests, validates compatibility and activates a local game on the host runtime.

## Reused pieces

- The previous live normalization work remains useful and is now routed into `HostEvent`.
- The previous platform pipeline remains useful and is now oriented around `HostRuntime`.
- Previous runtime accumulation work informed `HostSessionState`, but the architectural center is now the host.
- Avatar URL normalization and event-type gating ideas were rescued from the previous project, but they now live behind a host-first TikTok bridge boundary.
- The host no longer depends only on a point producer stub: it now polls a formal TikTok bridge session, while the mapper remains separate from session lifecycle.
- A small `TikTokBridgeController` now manages bridge session start/stop/reset/poll status in the host-first phase, while keeping mapping outside the controller.
- The host now also owns a minimal automation layer for event-driven TTS, starting with `gift` and optional `follow` thank-you messages.
- The host now begins to expose a stable `GameInputEvent` contract to games, so game modules can consume normalized input without depending directly on `HostEvent` shape.
- A minimal `PanelConfig` now centralizes bridge, TTS policy and host automation defaults so the host uses one small panel-level configuration surface.
- The host now supports simple periodic preconfigured TTS messages through a small host-side engine, still without timers or background workers.
- A first `gamesdk` catalog and registry layer now sits above factories, separated from bridge and TTS, and ready for future remote game sources.
- The panel now also has a simple `GameRuntimeController` for active-game lifecycle, covering activate, deactivate and restart on top of the catalog/registry layer.
- A first real test game, `event-counter`, is now connected end-to-end to the panel pipeline as a minimal runtime/game-input validation target.
- Games can now expose a richer manifest with capabilities metadata, and the panel catalog can store that metadata for future distribution and configuration flows.
- Games can now also expose and apply minimal per-game configuration from the panel, starting with `event-counter`.
- The platform now exposes a unified panel snapshot that combines bridge, TTS, host session and active-game lifecycle state for future UI and monitoring layers.
- The platform now also keeps a short in-memory recent-activity log for host events and TTS enqueue actions, ready for future UI and debugging surfaces.
- The platform now also exposes a minimal command surface for bridge, TTS and active-game lifecycle actions, ready for future UI and local automation layers.
- The platform can now save and load a minimal local `PanelConfig` file, covering bridge, TTS, automation and periodic TTS defaults without any online dependency.
- The platform now has a small `PanelApp` composition layer that owns local bootstrap and reduces manual wiring in `main.cpp`, preparing the host for future packaging and UI work.
- The platform now also exposes local-only interfaces for future remote catalog and licensing integrations, while the current product mode remains fully local.
- The platform now also has a local view-model layer that turns panel state into UI-friendly sections, actions and recent activity lines without coupling future UI code to internal runtime objects.
- The platform now also has a minimal interactive local console built on `PanelApp`, snapshots, the view-model layer and panel commands, so the host can be exercised manually before a graphical UI exists.
- The local console now also supports inspecting available games, current config and local license state, plus saving and reloading panel config during local operation.
- Local config reload can now hot-apply safe runtime settings such as host automation, periodic TTS and the TikTok bridge mapper without rebuilding the whole panel process.
- The TikTok bridge session is now separated from a raw event source boundary, so the current local stub source can later be replaced by a real TikTok source without reshaping the host/controller path.
- The bridge layer now also includes a local external raw-event source placeholder, preparing a future real TikTok ingest path without introducing any network dependency yet.
- The bridge layer now also includes a local external-session variant, parallel to the stub session, as a clean step toward a future real TikTok bridge without changing the host path yet.
- The panel can now choose between local `stub` and `external` bridge-session modes through `PanelConfig`, keeping `stub` as the default while preparing the future real bridge path.
- The local `external` bridge mode can now also accept raw test events directly from `PanelApp` or the local console, still without introducing any network transport.
- The bridge layer now also includes a local JSON codec for external raw events, so a future real bridge can target a simple stable payload format before any transport is introduced.
- The local `external` bridge mode now also supports replaying raw events from JSONL files, making it easier to simulate live sessions without any real transport.
- The local `external` bridge mode now also supports an in-panel recording session state with active JSONL capture and file replay reflected in panel status, still without any real transport.
- The panel now also exposes a small operational manifest for the local `external` bridge mode, including recording state, replay metadata and submitted-event counters for UI/debug and future real integration work.
- `PanelApp` now also exposes a small central `tick(...)` that processes bridge ingest and periodic TTS together, preparing a cleaner local loop for future UI and real bridge integration.
- `PanelApp` now also supports a simple local `run_ticks(...)` mode, allowing repeated tick execution with explicit step values for operational testing without threads or transport.
- The platform now also supports a local diagnostics/self-check report built from the panel snapshot, giving quick health visibility for bridge, TTS, game, license and activity state.
- The local `external` bridge mode can now also consume raw JSON events from an inbox folder, moving processed and failed files locally as a bridge-friendly step before any real transport exists.
- The local `external` bridge mode now also exposes a real polled local WebSocket intake surface, so an external bridge can push JSON events into the existing panel pipeline without adding threads or changing the host path.
- The panel now also consumes `viewer_join` end-to-end through the external TikTok bridge path, and the adapted Python bridge can emit the same contract by JSONL, inbox or local WebSocket depending on the test flow.
- The platform now also has a local Python-emitter-to-inbox path, so sample external bridge events can move end-to-end from Python JSON files into the panel `external` flow before any real TikTok bridge exists.
- The platform now also exposes a practical local TikTok/inbox/panel demo path through the adapted Python bridge and console helpers, making real-PC operational testing easier before final transport integration.
- The panel now also keeps live external-session metadata such as current room, last accepted event and per-kind counters, so operators can inspect real TikTok intake directly from panel state instead of relying only on Python-side logs.
- The local WS bridge path now also accepts a small `session_status` side-channel from the Python runner, allowing the panel to expose target user and connection state as part of its own product-facing state.
- The panel config and console now also persist the preferred external TikTok target and WS port, so daily operation can prepare a real live session with fewer manual commands.
- The local product flow now also includes a one-command PowerShell launcher that persists the external target/port and starts panel plus real TikTok runner together for day-to-day local operation.
- The panel can now also own the Python TikTok runner lifecycle directly from the console, exposing runner start/stop/status in its own operational state instead of depending only on manual external launches.
- The panel now also captures a short tail of Python runner logs and treats panel-driven runner stops as clean operational stops, which makes the external bridge state more reliable and UI-ready for support surfaces.
- The console now also exposes a small `bridge demo ready` check so local operators can confirm external mode, bridge, active game, WS intake and license state before starting a real or simulated Python bridge session.
- The console now also exposes a `bridge demo live` helper that prepares the local WS demo path and prints the exact Python commands to use for simulated or real external bridge testing.
- The console now also exposes a small one-shot `bridge demo session` flow that prepares or reuses the local WS path, pumps ticks until events arrive, and prints a short operational summary with recent activity.
- The console now also exposes a one-shot `bridge demo observe` flow that prepares or reuses the local WS path, pumps ticks until events arrive, and then prints a compact final summary of diagnostics, activity, TTS queue and active game state.
- The platform now also serves a local HTTP UI directly from the host executable, exposing `/api/state` and `/api/command` on top of the real `PanelApp` surfaces so the product can move into a first real UI without inventing a second runtime.
- That embedded UI now also exposes a true single-screen control room with realtime feed polling, active-game control, host AI controls, game selection, diagnostics and sparkline metrics on top of the same host runtime surfaces.
- The Python TikTok bridge is now also modularized into connection, supervision, normalization, dispatch, replay, metrics and HTTP status layers, with a real bridge config YAML, container packaging files and an automated Python test suite integrated into CTest.
- The panel-driven Python runner now also attempts a clean local `/shutdown` before falling back to hard process termination, so day-to-day operator stops behave like a service stop instead of a crash.
- The host now also includes a real Windows SAPI TTS backend behind the existing `src/tts` scheduler/service architecture, with curated voice profiles, chat filtering, editable templates and HTTP/UI configuration surfaces.
- The local HTTP UI now exposes a dedicated `/api/realtime` read model for fast-changing metrics and recent events, leaving `/api/state` for broader state refreshes and reducing repeated full-state polling.
- Local control surfaces now enforce loopback and Origin boundaries across the embedded HTTP UI, the Python bridge server and the C++ external WebSocket intake, keeping browser-origin writes inside the local trust boundary.
- Support bundles now pass through a sanitizer boundary before export, redacting sensitive config and structured-log fields while preserving enough operational context for debugging.
- The UI polling loop is now adaptive to document visibility, and high-churn lists avoid redundant DOM writes when their generated markup has not changed.

## Deferred areas

- No real TikTok integration yet.
- No real networking yet.
- No remote game download yet.
- A portable Windows package flow now exists through `scripts/package_windows.ps1`, while installer authoring remains the next packaging layer.

## Immediate next build target

Finish installer authoring and final deployment polish on top of the now-stable local host, embedded UI and real TTS surfaces.
