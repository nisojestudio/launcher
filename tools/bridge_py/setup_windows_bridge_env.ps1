Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$bridgeRoot = Join-Path $projectRoot "tools\bridge_py"
$venvRoot = Join-Path $bridgeRoot ".venv"
$pythonExe = Join-Path $venvRoot "Scripts\python.exe"
$requirements = Join-Path $bridgeRoot "requirements.txt"

if (-not (Test-Path $requirements)) {
    throw "requirements.txt not found at $requirements"
}

if (-not (Test-Path $pythonExe)) {
    Write-Host "[bridge-env] creating virtual environment..." -ForegroundColor Cyan
    python -m venv $venvRoot
}

Write-Host "[bridge-env] upgrading pip..." -ForegroundColor Cyan
& $pythonExe -m pip install --upgrade pip

Write-Host "[bridge-env] installing bridge requirements..." -ForegroundColor Cyan
& $pythonExe -m pip install -r $requirements

Write-Host "[bridge-env] ready: $pythonExe" -ForegroundColor Green
