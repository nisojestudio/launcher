# Panel Live 3.0 Packaging and Installer Report

Date: 2026-04-10
Project root: `C:\Users\Nisoje\Desktop\Panel live 3.0`

## 1. Goal

Prepare `Panel Live` for Windows 10/11 distribution, lock the panel behind remote login and license validation, and generate a distributable installer with the required runtime dependencies bundled or installed conditionally.

## 2. Step 1: Analysis and packaging readiness

### Local documentation reviewed before code changes

- `README.md`
- `docs/WORKING_CONTRACT.md`
- `docs/ARCHITECTURE_START.md`
- Related runtime, UI, packaging, and test files

### Real findings from the analysis

- The project already had a valid Release build path and test suite.
- The project already had a portable packaging script, but it was not enough for a full Windows installer distribution flow.
- The runtime did not yet enforce the requested startup login and license gate.
- The installer flow was missing:
  - a dedicated Windows installer
  - conditional prerequisite installation
  - official branding integration
  - a reliable checksum and manifest refresh for release artifacts
- The auth and license endpoints were traced from the HTML assets in `C:\Users\Nisoje\Desktop\nisoje-studio`.

### Readiness conclusion

The codebase was technically suitable for packaging after:

- adding a runtime auth and license layer
- adding the startup popup and lock state in the UI
- creating a Windows installer pipeline
- hardening the packaging scripts so they can run outside a Visual Studio developer shell

## 3. Step 2: Work executed

### Runtime auth and licensing

Changed files:

- `src/platform/panel_config.hpp`
- `src/platform/panel_snapshot.hpp`
- `src/platform/server_license_service.hpp`
- `src/platform/server_license_service.cpp`
- `src/platform/panel_config_storage.cpp`
- `src/platform/panel_snapshot_builder.hpp`
- `src/platform/panel_snapshot_builder.cpp`
- `src/platform/panel_http_json.cpp`
- `src/platform/panel_app.hpp`
- `src/platform/panel_app.cpp`
- `src/platform/panel_http_server.cpp`

Why:

- Added persisted auth configuration to the panel config.
- Added runtime auth state to the panel snapshot and state JSON.
- Implemented Firebase email/password validation by REST.
- Implemented remote license validation through the Nisoje API license list endpoint.
- Added `POST /api/auth/login` and `POST /api/auth/logout`.
- Blocked protected panel actions until access is granted.
- Added a safe per-user config fallback under `%LOCALAPPDATA%\NisojeStudio\panel_config.json` when the app is installed under `Program Files`.

### UI startup popup and lock state

Changed files:

- `src/platform/ui/index.html`
- `src/platform/ui/styles.css`
- `src/platform/ui/app.js`

Why:

- Added the startup popup for email, password, and license key.
- Added locked and unlocked dashboard states.
- Added session visibility, logout support, validation feedback, and server response handling.

### Branding and Windows executable identity

Changed files:

- `assets/branding/panel_live.ico`
- `assets/branding/installer_background.png`
- `src/platform/app_resource.h`
- `src/platform/app_icon.rc`
- `src/platform/CMakeLists.txt`
- `src/platform/main.cpp`

Why:

- Embedded the official icon into the Windows executable.
- Reused the project branding for the installer wizard background.
- Ensured shortcuts and the app window use the branded icon.

### Packaging and installer pipeline

Changed files:

- `installer/panel_live.iss`
- `scripts/package_windows.ps1`
- `scripts/build_windows_installer.ps1`

Why:

- Created an Inno Setup installer for Windows 10/11 x64.
- Added conditional installation checks for:
  - Microsoft Visual C++ Redistributable x64
  - Microsoft Edge WebView2 Runtime
- Added an optional desktop shortcut task.
- Added installer progress and status text updates during prerequisite installation.
- Limited the installed payload to essential runtime files:
  - `NisojeStudio.exe`
  - `WebView2Loader.dll`
  - `panel_config.json`
  - `tools\...`
- Bundled prerequisite installers into the installer build.
- Hardened the packaging script so it can load the MSVC build environment automatically.
- Hardened the installer build script so it can find Inno Setup even when installed per-user.
- Added automatic manifest and SHA256 generation for release artifacts.

### Tests updated

Changed files:

- `tests/smoke_test.cpp`
- `tests/panel_http_ui_test.cpp`
- `tests/panel_config_storage_test.cpp`

Why:

- Updated snapshot construction for the new auth model.
- Added auth-lock coverage to the HTTP and UI tests.
- Added config storage roundtrip checks for auth settings.

## 4. Deliverables generated

Primary artifacts:

- Installer: `C:\Users\Nisoje\Desktop\Panel live 3.0\dist\installer\PanelLive-3.0-Windows-x64-Setup.exe`
- Portable package: `C:\Users\Nisoje\Desktop\Panel live 3.0\dist\NisojeStudio-portable.zip`
- Checksums: `C:\Users\Nisoje\Desktop\Panel live 3.0\dist\SHA256SUMS.txt`
- Package manifest: `C:\Users\Nisoje\Desktop\Panel live 3.0\dist\NisojeStudio-manifest.txt`

Current sizes:

- `PanelLive-3.0-Windows-x64-Setup.exe` -> `229,207,991` bytes
- `NisojeStudio-portable.zip` -> `15,548,836` bytes
- `NisojeStudio.exe` -> `1,266,176` bytes

Current SHA256:

- `NisojeStudio\NisojeStudio.exe`
  - `946F72F9392D79012A9E9A861A1C56776075D0801BB74DF2219603B0A54618EB`
- `NisojeStudio-portable.zip`
  - `76068636FBE39E100CC0751BA4001AF2275108AE4CCA2A744A8783DF1068C7F2`
- `installer\PanelLive-3.0-Windows-x64-Setup.exe`
  - `09AAB2FA068A69075812365E9B8F754AEC5CA222CD848601F0AACE89B6732A84`

## 5. Validation actually executed

The following validations were really executed:

1. `cmake --build --preset release`
   - Result: success
2. `ctest --preset release --output-on-failure`
   - Result: `26/26` tests passed
3. `powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1 -SkipBuild -PanelName 'Panel Live' -RequireRemoteAuth`
   - Result: success
4. `powershell -ExecutionPolicy Bypass -File .\scripts\build_windows_installer.ps1`
   - First run exposed a real issue in the packaging environment
   - The issue was fixed by importing the MSVC build environment automatically
5. `powershell -ExecutionPolicy Bypass -File .\scripts\build_windows_installer.ps1 -SkipBuild`
   - Result: success
   - Generated `PanelLive-3.0-Windows-x64-Setup.exe`

## 6. Risks and pending validation

### Risks already addressed

- Packaging from a normal PowerShell session could fail because MSVC was not loaded.
- Per-user Inno Setup installations were not detected by the installer build script.
- Installed copies under `Program Files` could fail to persist config in place.

### Still pending

- A full clean-install smoke test on a separate Windows 10 machine.
- A full clean-install smoke test on a separate Windows 11 machine.
- An end-to-end login using a real production user and a real active license.
- A final distribution rehearsal from the target download server.

These items were not marked as validated because they were not executed in this session.

## 7. Recommended next block of work

1. Test the installer on a clean Windows 10 VM.
2. Test the installer on a clean Windows 11 VM.
3. Validate login and license acceptance with at least one real licensed account.
4. Upload the installer and `SHA256SUMS.txt` together to the distribution server.
5. If desired, add code signing for the final public installer.
