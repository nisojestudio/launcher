# Architecture Baseline

## Real workspace structure found

- `CMakeLists.txt` builds one library (`nlp3_core`), one app target (`nlp3_app`, output `NisojeStudio.exe`) and one smoke test (`nlp3_smoke_test`).
- `CMakePresets.json` uses a single `default` Ninja preset with output in `build/`.
- `src/core` contains the only implemented module today: a minimal engine identity contract.
- `src/platform/main.cpp` is the current host entry point.
- `src/live`, `src/render` and `src/audio` already exist as folders, but they are still empty.
- `tests/smoke_test.cpp` only checks the engine version.
- `vcpkg.json` declares future dependencies, but phase 1 does not need to wire them yet.
- The workspace directory does not contain `.git` metadata, so traceability must live in files and command validation.

## Initial target architecture

### Core
- Owns engine identity, domain contracts and future simulation/runtime state.
- Must stay independent from host, web and external live providers.

### Live
- Normalizes external live events into stable inputs that the core can consume.
- Must not contain platform host logic.

### Render
- Translates core state into view-oriented data for native or future web/WASM hosts.
- Must not own game rules.

### Audio
- Defines audio policy, cues and future playback integration points.
- Must stay replaceable by different host implementations.

### Platform
- Acts as composition root for the executable host.
- Owns startup, module wiring and future bridge configuration.

### Tests
- Validate contracts and runtime composition before feature work grows.

## Short phase plan

1. `Phase 1 - Modular foundation`
   Split CMake by module, create explicit module contracts, and expose a runtime manifest that proves the layers compose correctly.
2. `Phase 2 - Diagnostics and live event contracts`
   Add logging/diagnostics and define normalized live event types flowing toward the core.
3. `Phase 3 - Core runtime`
   Build the first simulation loop and snapshot output from core to render/platform.
4. `Phase 4 - External integration`
   Attach websocket or bridge layers and prepare a clean path toward web/WASM hosting.

## Phase 1 decision

Phase 1 stays on the C++ standard library only. No new dependency is justified until a module needs real formatting, logging, JSON parsing or richer testing.

## Phase 2 event flow decision

- `live` owns a provider-agnostic normalized event contract plus the normalization rules from raw external inputs.
- `core` owns its own input port and does not depend on `live` headers or implementation details.
- `platform` acts as the temporary composition layer that translates normalized live events into core input.
- `render` and `audio` remain outside this flow until the core exposes richer runtime state.
