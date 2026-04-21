# Panel Live 3.0 Clean Install VM Protocol

Date: 2026-04-18
Project root: `C:\Users\Nisoje\Desktop\Panel live 3.0`
Primary installer under test: `dist\installer\PanelLive-3.0-Windows-x64-Setup.exe`

## 1. Objective

Close the final release certification gap that is still open:

- real `Setup.exe` installation on clean Windows 10 x64
- real `Setup.exe` installation on clean Windows 11 x64
- successful login with a real production user and a real active license
- remote game discovery and download from Cloudflare R2
- manual TikTok connection after login

This protocol is the exact execution plan for that validation.

## 2. Local finding from this workstation

The current workstation cannot close that certification by itself.

Verified locally:

- no `VBoxManage`
- no VMware CLI
- no `WindowsSandbox.exe`
- no `vmconnect.exe`
- no local Hyper-V console tools available
- current shell is not elevated

Hardware capability is present:

- `HyperVRequirementVMMonitorModeExtensions = true`
- `HyperVRequirementVirtualizationFirmwareEnabled = true`
- `HyperVRequirementSecondLevelAddressTranslation = true`
- `HyperVRequirementDataExecutionPreventionAvailable = true`

Conclusion:

- the VM validation must run on a machine that already has Hyper-V, Windows Sandbox, VirtualBox, or VMware actually enabled and usable

## 3. Inputs required before starting

You need these five things before opening the VM:

1. Installer:
   - `PanelLive-3.0-Windows-x64-Setup.exe`
2. Checksums:
   - `SHA256SUMS.txt`
3. Validation helper:
   - `scripts\validate_clean_install_vm.ps1`
4. One real production account:
   - email
   - password
   - active license key
5. One real TikTok test account:
   - username that can be connected during a live session

## 4. Target environments

Run the full protocol in both:

1. Windows 10 x64 clean VM
2. Windows 11 x64 clean VM

Recommended baseline:

- newly created VM snapshot
- default Windows Defender enabled
- no developer tools installed
- no Python installed manually
- no custom PATH modifications
- default firewall policy

## 5. Pass criteria

Certification is only closed if all of these are true in both VMs:

1. Installer completes successfully.
2. Installed app exists in `C:\Program Files\Panel Live`.
3. VC++ Redistributable is present after install.
4. WebView2 Runtime is present after install.
5. First launch blocks the panel behind login/license.
6. Protected endpoints reject actions before auth.
7. Real login succeeds with production credentials.
8. Remote catalog exposes at least `arena_live` and `super_pang` for the licensed account.
9. At least one real remote game download completes from the panel.
10. TikTok can be connected manually after login.
11. No hidden manual step is needed outside install + login + user TikTok input.

If any of those fail, certification remains open.

## 6. Manual execution flow

### Step 1. Verify artifact integrity in the VM

Copy these files into the VM:

- `PanelLive-3.0-Windows-x64-Setup.exe`
- `SHA256SUMS.txt`
- `validate_clean_install_vm.ps1`

Run:

```powershell
Get-FileHash .\PanelLive-3.0-Windows-x64-Setup.exe -Algorithm SHA256
```

Expected:

- hash matches the installer entry in `SHA256SUMS.txt`

### Step 2. Run the installer interactively

Run the installer normally, not silently.

Observe and record:

1. UAC prompt appears if expected
2. installer wizard opens with branding
3. desktop shortcut option is offered
4. progress window is visible
5. install finishes without fatal error

Expected install root:

- `C:\Program Files\Panel Live`

### Step 3. Verify installed payload

After install, verify these exist:

- `C:\Program Files\Panel Live\NisojeStudio.exe`
- `C:\Program Files\Panel Live\WebView2Loader.dll`
- `C:\Program Files\Panel Live\panel_config.json`
- `C:\Program Files\Panel Live\tools\bridge_py\python_runtime\python.exe`
- `C:\Program Files\Panel Live\tools\bridge_py\bridge_env_check.py`

### Step 4. Run the automated evidence collector

Run this inside the VM:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\validate_clean_install_vm.ps1 `
  -Email "<real-email>" `
  -Password "<real-password>" `
  -LicenseKey "<real-license>" `
  -ExpectedRemoteGames arena_live,super_pang `
  -DownloadGames arena_live,super_pang
```

Expected outputs:

- a new evidence folder on Desktop named `PanelLiveCleanInstallEvidence-<timestamp>`
- `environment.json`
- `prerequisites.json`
- `installed-files.json`
- `state-pre-auth.json`
- `protected-endpoints.json`
- `login-response.json`
- `state-post-auth.json`
- `download-arena_live.json`
- `download-super_pang.json`
- `support-export.json`
- `summary.json`
- `SUMMARY.txt`

### Step 5. Manual TikTok validation

After login succeeds:

1. open the panel UI
2. enter the real TikTok username
3. click `Conectar`
4. wait for connection state to settle
5. confirm the monitor starts receiving real live events

Expected:

- no hidden bootstrap command
- no Python installation needed
- no manual bridge start outside the panel
- no missing DLL/runtime popup

### Step 6. Manual game validation from panel UI

From the logged-in panel:

1. confirm `Arena Live` and `Super Pang` appear in the catalog
2. download each one from the panel
3. confirm the downloaded game can be launched from panel controls

Expected:

- download starts from remote catalog
- package installs locally
- game launches from panel

## 7. Evidence review checklist

Review `SUMMARY.txt` and `summary.json`.

Required values:

- `panel_api_reachable=true`
- `auth_required=true`
- `authenticated=false` before login
- `reconnect_status=403`
- `download_status_before_auth=403`
- `login_ok=true`

Then review `state-post-auth.json`:

- `snapshot.auth.authenticated = true`
- `catalog.items` contains remote entries for `arena_live`
- `catalog.items` contains remote entries for `super_pang`

Then review each download result:

- request JSON reports `ok=true`
- final catalog item has `installed=true`

## 8. Failure classification

Use this exact classification:

- `BLOCKING`
  - installer fails
  - missing VC++ after install
  - missing WebView2 after install
  - app does not launch
  - login fails with known-good credentials
  - remote catalog does not expose licensed games
  - remote download fails
  - TikTok connect fails after valid login and valid user input

- `RISK`
  - SmartScreen warning because installer is unsigned
  - install succeeds but logs are weak
  - remote download is slow but completes

- `NO VERIFICADO`
  - any step not executed in one of the two VMs

## 9. Final release rule

You may only mark the installer as commercially ready for clean machines if:

1. Windows 10 VM passes
2. Windows 11 VM passes
3. real auth/license passes
4. remote game download passes
5. manual TikTok connection passes

Anything less than that is not final certification.
