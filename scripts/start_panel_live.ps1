param(
    [string]$InstallRoot = "",
    [string]$PanelExe = "",
    [int]$UiPort = 18913,
    [switch]$Console
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
}
if ([string]::IsNullOrWhiteSpace($PanelExe)) {
    $PanelExe = Join-Path $InstallRoot "NisojeStudio.exe"
}

$InstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$PanelExe = [System.IO.Path]::GetFullPath($PanelExe)

if (-not (Test-Path -LiteralPath $PanelExe)) {
    throw "Panel executable not found at $PanelExe"
}

$logRoot = if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    Join-Path $env:LOCALAPPDATA "NisojeStudio\logs"
} else {
    Join-Path $env:TEMP "NisojeStudio\logs"
}
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$env:LIVEPANEL_BRIDGE_LOG_PATH = Join-Path $logRoot "bridge.jsonl"

if ([string]::IsNullOrWhiteSpace($env:NLP3_LOCAL_GAMES_ROOT) -and -not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
    $env:NLP3_LOCAL_GAMES_ROOT = Join-Path $env:USERPROFILE "Desktop\Juegos"
}

Push-Location $InstallRoot
try {
    if ($Console) {
        & $PanelExe --console --ui-port $UiPort
    } else {
        & $PanelExe --ui-port $UiPort
    }
} finally {
    Pop-Location
}
