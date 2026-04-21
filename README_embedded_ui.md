# Embedded Desktop UI

## Overview

Nisoje Studio now ships with a native Windows desktop window that hosts the existing embedded HTTP panel through Microsoft Edge WebView2.

The runtime model does not change:

- `PanelApp` still owns the local runtime
- `PanelHttpServer` still serves the embedded UI assets and JSON endpoints
- `WindowHost` creates the native top-level window
- `WebViewHost` embeds WebView2 and navigates to the local panel URL

The result is a standalone desktop experience without a manual browser step.

## Startup sequence

```text
NisojeStudio.exe
  -> PanelApp::initialize()
  -> start local PanelHttpServer
  -> wait until /health is reachable
  -> create native WindowHost
  -> initialize WebView2
  -> navigate WebView2 to local panel URL
  -> enter runtime/message loop
```

Structured startup phases are emitted as JSON lines:

- `runtime_start`
- `http_server_ready`
- `webview_init_start`
- `webview_init_success`
- `webview_navigation_start`
- `webview_navigation_success`
- `webview_fallback_browser`
- `shutdown_begin`
- `shutdown_complete`

## Runtime dependency

Two pieces are required on Windows x64:

1. `WebView2Loader.dll`
2. Microsoft Edge WebView2 Runtime installed on the target machine

The packaged host itself also requires:

3. `Microsoft Visual C++ Redistributable x64` installed on the target machine

This repository vendors the SDK/loader under:

- `third_party/webview2/sdk-1.0.3800.47/`

The build copies:

- `third_party/webview2/sdk-1.0.3800.47/build/native/x64/WebView2Loader.dll`

to:

- `build/src/platform/WebView2Loader.dll`

for local runs.

## Config

`panel_config.json` now supports:

```json
{
  "embedded_ui_enabled": true,
  "embedded_ui_fallback_to_browser": true,
  "embedded_ui_devtools": false,
  "embedded_ui_url": "http://127.0.0.1:18913/",
  "embedded_ui_startup_timeout_ms": 8000
}
```

Environment variable overrides:

- `NLP3_EMBEDDED_UI_ENABLED`
- `NLP3_EMBEDDED_UI_FALLBACK_TO_BROWSER`
- `NLP3_EMBEDDED_UI_DEVTOOLS`
- `NLP3_EMBEDDED_UI_URL`
- `NLP3_EMBEDDED_UI_STARTUP_TIMEOUT_MS`

## Usage

### Native desktop window

```powershell
cd "<repo-root>"
.\build\src\platform\NisojeStudio.exe
```

### Console + native window

```powershell
.\build\src\platform\NisojeStudio.exe --console --ui
```

### Alternate local UI port

```powershell
.\build\src\platform\NisojeStudio.exe --ui --ui-port 18919
```

## Fallback behavior

If WebView2 cannot initialize:

- the process does not crash
- the native window stays alive with a fallback message overlay
- if `embedded_ui_fallback_to_browser=true`, the system browser is opened on the same local panel URL

If the local HTTP server itself cannot start or cannot become reachable before timeout:

- startup logs the failure clearly
- the application exits cleanly
- a Windows error dialog is shown when launched without console mode

## Packaging notes

For installer packaging:

1. include `WebView2Loader.dll` next to `NisojeStudio.exe`
2. validate that the Evergreen WebView2 Runtime is installed
3. validate that `Microsoft Visual C++ Redistributable x64` is installed for the host executable
4. if your installer does not guarantee WebView2, bundle or bootstrap the official Microsoft WebView2 Runtime installer
5. if your installer does not guarantee the VC++ runtime, direct the user to install `vc_redist.x64.exe`

Recommended validation on target machines:

- launch `NisojeStudio.exe`
- confirm the `Nisoje Studio` window appears
- confirm the panel renders without opening Chrome/Edge manually
- confirm the machine already has the VC++ runtime or that your installer/bootstrapper covers it

## Troubleshooting

### WebView2Loader.dll not found

Cause:
- loader DLL missing next to `NisojeStudio.exe`

Fix:
- copy `WebView2Loader.dll` beside the executable
- rebuild with the current CMake configuration if needed

### WebView2 runtime missing

Cause:
- target machine does not have the Evergreen runtime installed

Fix:
- install Microsoft Edge WebView2 Runtime
- relaunch the app

### Missing MSVCP140.dll or VCRUNTIME140.dll

Cause:
- target machine does not have `Microsoft Visual C++ Redistributable x64`

Fix:
- install `Microsoft Visual C++ Redistributable x64`
- relaunch the app
- the portable package also includes `PORTABLE_REQUIREMENTS.txt` with the download note

### Panel window opens but the UI is blank

Cause:
- local panel server did not become reachable
- local navigation failed during startup

Checks:
- `http://127.0.0.1:18913/health`
- `%TEMP%\\NisojeStudio\\embedded_ui.log`

### Need browser fallback only

Use:

```powershell
.\build\src\platform\NisojeStudio.exe --ui
```

and keep:

- `embedded_ui_fallback_to_browser=true`

If you need to suppress browser fallback during debugging:

```powershell
.\build\src\platform\NisojeStudio.exe --ui --no-browser
```

## Validation performed

Validated in this workspace:

- full CMake build
- full automated test suite
- native `WindowHost` creation test
- embedded UI server readiness test
- WebView2 missing-loader fallback test
- real executable startup with native window detection
- local `/health` availability during embedded window run
- real window resize verification through Win32 automation
