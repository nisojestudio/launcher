param(
    [string]$GameRoot = "",
    [string]$GamesRoot = "",
    [string]$GameId = "arena_live",
    [string]$PythonExe = "",
    [switch]$SkipPrerequisiteChecks
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-PanelRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Resolve-LocalGamesRoot {
    param([string]$RequestedRoot)

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return (Resolve-Path $RequestedRoot).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($env:NLP3_LOCAL_GAMES_ROOT)) {
        return (Resolve-Path $env:NLP3_LOCAL_GAMES_ROOT).Path
    }

    $desktop = [Environment]::GetFolderPath([Environment+SpecialFolder]::Desktop)
    if (-not [string]::IsNullOrWhiteSpace($desktop)) {
        return (Join-Path $desktop "Juegos")
    }

    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        return (Join-Path $env:USERPROFILE "Desktop\Juegos")
    }

    return "Juegos"
}

function Resolve-BridgePython {
    param(
        [string]$PanelRoot,
        [string]$RequestedPython
    )

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($RequestedPython)) {
        $candidates += $RequestedPython
    }
    if (-not [string]::IsNullOrWhiteSpace($env:NLP3_LOCAL_GAME_BRIDGE_PYTHON_EXE)) {
        $candidates += $env:NLP3_LOCAL_GAME_BRIDGE_PYTHON_EXE
    }

    $candidates += @(
        (Join-Path $PanelRoot "tools\bridge_py\python_runtime\python.exe"),
        (Join-Path $PanelRoot "tools\bridge_py\.venv\Scripts\python.exe"),
        "python"
    )

    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        if ($candidate -eq "python") {
            return $candidate
        }

        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "No Python runtime found for the local game bridge."
}

function Resolve-TargetGameRoot {
    param(
        [string]$RequestedGameRoot,
        [string]$ResolvedGamesRoot,
        [string]$RequestedGameId
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedGameRoot)) {
        if (-not (Test-Path $RequestedGameRoot)) {
            throw "Game root not found at $RequestedGameRoot"
        }
        return (Resolve-Path $RequestedGameRoot).Path
    }

    $candidateFolders = @(
        (Join-Path $ResolvedGamesRoot "Arena Live")
    )

    foreach ($folder in $candidateFolders) {
        if (Test-Path (Join-Path $folder "module_manifest.json")) {
            return (Resolve-Path $folder).Path
        }
    }

    foreach ($entry in Get-ChildItem -Path $ResolvedGamesRoot -Directory -ErrorAction SilentlyContinue) {
        $manifestPath = Join-Path $entry.FullName "module_manifest.json"
        if (-not (Test-Path $manifestPath)) {
            continue
        }

        try {
            $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
            if ($manifest.id -eq $RequestedGameId) {
                return $entry.FullName
            }
        } catch {
        }
    }

    throw "No external game manifest found for '$RequestedGameId' under $ResolvedGamesRoot"
}

function Test-VcRuntimeInstalled {
    $registryPath = "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64"
    try {
        $installed = (Get-ItemProperty -Path $registryPath -Name Installed -ErrorAction Stop).Installed
        return [int]$installed -eq 1
    } catch {
        return $false
    }
}

function Test-WebView2Installed {
    $paths = @(
        "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    )

    foreach ($path in $paths) {
        try {
            $version = (Get-ItemProperty -Path $path -Name pv -ErrorAction Stop).pv
            if (-not [string]::IsNullOrWhiteSpace($version)) {
                return $true
            }
        } catch {
        }
    }

    return $false
}

$panelRoot = Resolve-PanelRoot
$gamesRoot = Resolve-LocalGamesRoot -RequestedRoot $GamesRoot
$resolvedGameRoot = Resolve-TargetGameRoot -RequestedGameRoot $GameRoot -ResolvedGamesRoot $gamesRoot -RequestedGameId $GameId
$bridgeScript = Join-Path $panelRoot "tools\game_bridge_py\run_local_game_bridge.py"
$resolvedPythonExe = Resolve-BridgePython -PanelRoot $panelRoot -RequestedPython $PythonExe

if (-not (Test-Path $bridgeScript)) {
    throw "Bridge script not found at $bridgeScript"
}

$manifestPath = Join-Path $resolvedGameRoot "module_manifest.json"
if (-not (Test-Path $manifestPath)) {
    throw "module_manifest.json not found at $manifestPath"
}

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$entryExecutable = $manifest.entryExecutable
$entryPath = if ([System.IO.Path]::IsPathRooted($entryExecutable)) {
    $entryExecutable
} else {
    Join-Path $resolvedGameRoot $entryExecutable
}

if (-not (Test-Path $entryPath)) {
    throw "Game entry not found at $entryPath"
}

if (-not $SkipPrerequisiteChecks) {
    if (-not (Test-VcRuntimeInstalled)) {
        throw "Microsoft Visual C++ Redistributable x64 is required before launching the external game."
    }

    $manifestType = [string]$manifest.type
    if ($manifestType.ToLowerInvariant().Contains("webview") -and -not (Test-WebView2Installed)) {
        throw "Microsoft Edge WebView2 Runtime is required for this external game."
    }
}

Write-Host "[game-bridge] panel_root=$panelRoot"
Write-Host "[game-bridge] games_root=$gamesRoot"
Write-Host "[game-bridge] game_root=$resolvedGameRoot"
Write-Host "[game-bridge] python=$resolvedPythonExe"
Write-Host "[game-bridge] entry=$entryPath"

& $resolvedPythonExe $bridgeScript --game-root $resolvedGameRoot
exit $LASTEXITCODE
