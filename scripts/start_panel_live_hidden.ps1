param(
    [int]$UiPort = 18913
)

$ErrorActionPreference = "Stop"

function Show-LauncherMessage {
    param(
        [string]$Title,
        [string]$Message
    )

    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show($Message, $Title) | Out-Null
}

function Resolve-PanelLauncherScript {
    param(
        [string]$ToolsRoot
    )

    $files =
        Get-ChildItem -Path $ToolsRoot -Filter *.py -File -Recurse |
        Where-Object { $_.FullName -notmatch '\\\.venv\\' }
    $candidate =
        $files |
        Where-Object {
            $content = Get-Content $_.FullName -Raw
            $content -match 'NLP3_PANEL_DESKTOP_LAUNCHER_SIGNATURE'
        } |
        Sort-Object FullName |
        Select-Object -First 1

    return $candidate
}

function Resolve-DefaultGamesRoot {
    $desktop = [Environment]::GetFolderPath([Environment+SpecialFolder]::Desktop)
    if (-not [string]::IsNullOrWhiteSpace($desktop)) {
        return (Join-Path $desktop "Juegos")
    }

    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        return (Join-Path $env:USERPROFILE "Desktop\Juegos")
    }

    return "Juegos"
}

function Resolve-LauncherPython {
    param(
        [string]$ProjectRoot
    )

    $candidates = @(
        (Join-Path $ProjectRoot "tools\bridge_py\python_runtime\python.exe"),
        (Join-Path $ProjectRoot "tools\bridge_py\.venv\Scripts\python.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -ne $pythonCommand -and -not [string]::IsNullOrWhiteSpace($pythonCommand.Source)) {
        return $pythonCommand.Source
    }

    $pyLauncher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($null -ne $pyLauncher -and -not [string]::IsNullOrWhiteSpace($pyLauncher.Source)) {
        return $pyLauncher.Source
    }

    return ""
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
$toolsRoot = Join-Path $projectRoot "tools\bridge_py"
$pythonExe = Resolve-LauncherPython -ProjectRoot $projectRoot
$gamesRoot = Resolve-DefaultGamesRoot
$logRoot = Join-Path $env:TEMP "NisojeStudio"
$logPath = Join-Path $logRoot "desktop_launcher.log"
$bridgeLogRoot = if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    Join-Path $env:LOCALAPPDATA "NisojeStudio\logs"
} else {
    Join-Path $env:TEMP "NisojeStudio\logs"
}

try {
    if (-not (Test-Path $toolsRoot)) {
        throw "No se encontro tools\bridge_py en `"$toolsRoot`"."
    }

    if ([string]::IsNullOrWhiteSpace($pythonExe)) {
        throw @"
No se encontro un Python utilizable para el launcher.

Se revisaron estas opciones:
1. tools\bridge_py\python_runtime\python.exe
2. tools\bridge_py\.venv\Scripts\python.exe
3. python.exe o py.exe en PATH
"@
    }

    $launcherScript = Resolve-PanelLauncherScript -ToolsRoot $toolsRoot
    if ($null -eq $launcherScript) {
        throw "No se encontro un launcher Python del panel dentro de tools\bridge_py."
    }

    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $bridgeLogRoot | Out-Null
    $env:LIVEPANEL_BRIDGE_LOG_PATH = Join-Path $bridgeLogRoot "bridge.jsonl"
    $output =
        & $pythonExe $launcherScript.FullName --wait-until-ready --restart-if-running --ui-port $UiPort --games-root $gamesRoot 2>&1 |
        Out-String
    $exitCode = $LASTEXITCODE

    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logEntry = @"
[$timestamp] launcher=$($launcherScript.FullName)
[$timestamp] ui_port=$UiPort
[$timestamp] games_root=$gamesRoot
$output
"@
    Set-Content -LiteralPath $logPath -Value $logEntry -Encoding UTF8

    if ($exitCode -ne 0) {
        throw "Panel Live 3.0 no pudo iniciarse.`n`n$($output.Trim())`n`nLog: $logPath"
    }
} catch {
    Show-LauncherMessage -Title "Panel Live 3.0" -Message $_.Exception.Message
    exit 1
}
