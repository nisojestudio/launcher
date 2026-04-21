param(
    [string]$ConfigurePreset = "release",
    [string]$BuildPreset = "release",
    [string]$BuildDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$packageScript = Join-Path $PSScriptRoot "package_windows.ps1"
if (-not (Test-Path $packageScript)) {
    throw "No se encontro package_windows.ps1 en $packageScript"
}

& $packageScript `
    -ConfigurePreset $ConfigurePreset `
    -BuildPreset $BuildPreset `
    -BuildDir $BuildDir `
    -BuildOnly
