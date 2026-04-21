param(
    [string[]]$Targets = @(
        "NisojeStudio.exe",
        "tests\\nlp3_panel_http_ui_test.exe",
        "tests\\nlp3_window_host_test.exe",
        "tests\\nlp3_webview_host_test.exe",
        "tests\\nlp3_panel_app_smoke_test.exe"
    )
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build/release_pack"
$logPath = Join-Path $repoRoot "output/manual_rebuild_release_pack.log"
$logDir = Split-Path -Parent $logPath
$batchPath = Join-Path $repoRoot "output/manual_rebuild_release_pack.cmd"
$vsDevCmdPath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"

if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
}

Set-Content -Path $logPath -Value ""

$joinedTargets = [string]::Join(" ", $Targets)
$commandLines = & cmd.exe /d /c "cd /d `"$buildDir`" && ninja -t commands $joinedTargets"

if (-not $commandLines -or $commandLines.Count -eq 0) {
    throw "No se pudieron obtener comandos de reconstruccion desde ninja."
}

$scriptLines = @(
    "@echo off",
    "setlocal",
    "call ""$vsDevCmdPath"" -arch=x64 -host_arch=x64",
    "if errorlevel 1 exit /b %errorlevel%",
    "cd /d ""$buildDir""",
    "if errorlevel 1 exit /b %errorlevel%"
)

foreach ($line in $commandLines) {
    $trimmed = $line.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        continue
    }

    $scriptLines += $trimmed
    $scriptLines += "if errorlevel 1 exit /b %errorlevel%"
}

Set-Content -Path $batchPath -Value $scriptLines -Encoding ASCII
Write-Host ("Ejecutando rebuild quirurgico con {0} pasos..." -f (($scriptLines.Count - 2) / 2))

& cmd.exe /d /c """$batchPath"" >> ""$logPath"" 2>&1"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Fallo durante el rebuild. Ultimas lineas del log:"
    Get-Content -Path $logPath -Tail 60
    exit $LASTEXITCODE
}

Write-Host ("Rebuild quirurgico completado. Log: {0}" -f $logPath)
