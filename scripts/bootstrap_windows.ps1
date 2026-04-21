param(
    [switch]$SkipRootVenv,
    [switch]$SkipBridgeVenv
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if (-not (Test-Path -LiteralPath $PathValue)) {
        New-Item -ItemType Directory -Path $PathValue -Force | Out-Null
    }
}

function Write-Log {
    param([Parameter(Mandatory = $true)][string]$Message)
    $timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    $line = "[{0}] {1}" -f $timestamp, $Message
    Write-Host $line
    Add-Content -LiteralPath $script:LogPath -Value $line -Encoding UTF8
}

function Get-CommandPathSafe {
    param([Parameter(Mandatory = $true)][string]$Name)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        return ""
    }
    return [string]$command.Source
}

function Resolve-VsWherePath {
    return Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$logRoot = Join-Path $projectRoot "build\bootstrap"
Ensure-Directory -PathValue $logRoot
$script:LogPath = Join-Path $logRoot ("bootstrap-" + (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmss") + ".log")
Set-Content -LiteralPath $script:LogPath -Value "" -Encoding UTF8

$pythonCmd = Get-CommandPathSafe -Name "python.exe"
if ([string]::IsNullOrWhiteSpace($pythonCmd)) {
    Write-Log "Python was not found in PATH."
    exit 10
}

$cmakeCmd = Get-CommandPathSafe -Name "cmake.exe"
$ninjaCmd = Get-CommandPathSafe -Name "ninja.exe"
$clCmd = Get-CommandPathSafe -Name "cl.exe"
$vswhere = Resolve-VsWherePath

Write-Log "Project root: $projectRoot"
Write-Log "Python: $pythonCmd"
Write-Log "CMake: $(if ($cmakeCmd) { $cmakeCmd } else { '<missing>' })"
Write-Log "Ninja: $(if ($ninjaCmd) { $ninjaCmd } else { '<missing>' })"
Write-Log "cl.exe: $(if ($clCmd) { $clCmd } else { '<not loaded>' })"
Write-Log "vswhere: $(if (Test-Path $vswhere) { $vswhere } else { '<missing>' })"

if (-not $SkipRootVenv) {
    $rootVenv = Join-Path $projectRoot ".venv"
    if (-not (Test-Path -LiteralPath (Join-Path $rootVenv "Scripts\python.exe"))) {
        Write-Log "Creating root .venv"
        & $pythonCmd -m venv $rootVenv
    } else {
        Write-Log "Root .venv already exists."
    }
} else {
    Write-Log "Skipping root .venv creation."
}

if (-not $SkipBridgeVenv) {
    $bridgeSetup = Join-Path $projectRoot "tools\bridge_py\setup_windows_bridge_env.ps1"
    if (-not (Test-Path -LiteralPath $bridgeSetup)) {
        Write-Log "Bridge setup script not found: $bridgeSetup"
        exit 11
    }

    Write-Log "Preparing bridge Python runtime source (.venv)."
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $bridgeSetup
    if ($LASTEXITCODE -ne 0) {
        Write-Log "Bridge environment setup failed with exit code $LASTEXITCODE"
        exit 12
    }
} else {
    Write-Log "Skipping bridge runtime source preparation."
}

Write-Log "Bootstrap finished."
Write-Log "Suggested next steps:"
Write-Log "1. powershell -ExecutionPolicy Bypass -File .\\scripts\\build_windows_release.ps1"
Write-Log "2. powershell -ExecutionPolicy Bypass -File .\\scripts\\package_windows.ps1"
Write-Log "3. powershell -ExecutionPolicy Bypass -File .\\scripts\\build_windows_installer.ps1"
Write-Host "Bootstrap log written to: $script:LogPath"
exit 0
