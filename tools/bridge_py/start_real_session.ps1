param(
    [Parameter(Mandatory = $true)]
    [string]$User,

    [int]$Port = 8765,

    [int]$MaxSeconds = 0,

    [string]$PythonExe = "",

    [string]$PanelExe = "",

    [switch]$NoStartPanel
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:PowershellExe = "powershell.exe"

function Resolve-ProjectRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Convert-ToMutableConfig {
    param(
        [Parameter(Mandatory = $true)]
        [object]$InputObject
    )

    if ($null -eq $InputObject) {
        return $null
    }

    if ($InputObject -is [System.Collections.IDictionary]) {
        $hash = [ordered]@{}
        foreach ($key in $InputObject.Keys) {
            $hash[$key] = Convert-ToMutableConfig -InputObject $InputObject[$key]
        }
        return $hash
    }

    if ($InputObject -is [System.Collections.IEnumerable] -and -not ($InputObject -is [string])) {
        $items = @()
        foreach ($item in $InputObject) {
            $items += ,(Convert-ToMutableConfig -InputObject $item)
        }
        return $items
    }

    if ($InputObject -is [pscustomobject]) {
        $hash = [ordered]@{}
        foreach ($property in $InputObject.PSObject.Properties) {
            $hash[$property.Name] = Convert-ToMutableConfig -InputObject $property.Value
        }
        return $hash
    }

    return $InputObject
}

function Load-PanelConfig {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ConfigPath
    )

    if (-not (Test-Path $ConfigPath)) {
        return [ordered]@{}
    }

    $raw = Get-Content $ConfigPath -Raw
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return [ordered]@{}
    }

    return Convert-ToMutableConfig -InputObject ($raw | ConvertFrom-Json)
}

function Save-PanelConfig {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ConfigPath,

        [Parameter(Mandatory = $true)]
        [System.Collections.IDictionary]$Config
    )

    $json = $Config | ConvertTo-Json -Depth 20
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($ConfigPath, $json + [Environment]::NewLine, $encoding)
}

function Resolve-PythonExecutable {
    param(
        [AllowEmptyString()]
        [string]$RequestedPythonExe
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedPythonExe)) {
        return $RequestedPythonExe
    }

    $overridePythonExe = [Environment]::GetEnvironmentVariable("LIVEPANEL_TIKTOK_PYTHON_EXE")
    if (-not [string]::IsNullOrWhiteSpace($overridePythonExe)) {
        return $overridePythonExe
    }

    $projectRoot = Resolve-ProjectRoot
    $packagedRuntimePython = Join-Path $projectRoot "tools\bridge_py\python_runtime\python.exe"
    if (Test-Path $packagedRuntimePython) {
        return $packagedRuntimePython
    }

    $localBridgePython = Join-Path $projectRoot "tools\bridge_py\.venv\Scripts\python.exe"
    if (Test-Path $localBridgePython) {
        return $localBridgePython
    }

    return "python"
}

function Resolve-RunnerScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )

    $overrideScript = [Environment]::GetEnvironmentVariable("LIVEPANEL_TIKTOK_RUNNER_SCRIPT")
    if (-not [string]::IsNullOrWhiteSpace($overrideScript)) {
        return $overrideScript
    }

    return (Join-Path $ProjectRoot "tools\bridge_py\run_tiktok_bridge.py")
}

function Resolve-PanelExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [AllowEmptyString()]
        [string]$RequestedPanelExe
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedPanelExe)) {
        return $RequestedPanelExe
    }

    $candidates = @(
        (Join-Path $ProjectRoot "dist\NisojeStudio\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "build\release\src\platform\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "build\Release\src\platform\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "build\src\platform\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "build\release_pack\src\platform\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "build\src\platform\nlp3_app.exe")
    )

    $ranked = @()
    for ($index = 0; $index -lt $candidates.Count; $index++) {
        $candidate = $candidates[$index]
        if (-not (Test-Path $candidate)) {
            continue
        }

        $item = Get-Item -LiteralPath $candidate
        $ranked += [pscustomobject]@{
            Path = $item.FullName
            LastWriteTicks = $item.LastWriteTimeUtc.Ticks
            Preference = ($candidates.Count - $index)
        }
    }

    if ($ranked.Count -gt 0) {
        return ($ranked |
            Sort-Object LastWriteTicks, Preference -Descending |
            Select-Object -First 1).Path
    }

    return (Join-Path $ProjectRoot "build\release\src\platform\NisojeStudio.exe")
}

function Start-PanelConsole {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [Parameter(Mandatory = $true)]
        [string]$ResolvedPanelExe
    )

    $panelCommand = "Set-Location `"$ProjectRoot`"; & `"$ResolvedPanelExe`" --console"
    Start-Process -FilePath $script:PowershellExe -ArgumentList @("-NoExit", "-Command", $panelCommand) | Out-Null
}

if ($Port -le 0 -or $Port -gt 65535) {
    throw "Port must be between 1 and 65535."
}

if ($MaxSeconds -lt 0) {
    throw "MaxSeconds must be >= 0."
}

$projectRoot = Resolve-ProjectRoot
$configPath = Join-Path $projectRoot "panel_config.json"
$resolvedPythonExe = Resolve-PythonExecutable -RequestedPythonExe $PythonExe
$resolvedRunnerScript = Resolve-RunnerScript -ProjectRoot $projectRoot
$resolvedPanelExe = Resolve-PanelExecutable -ProjectRoot $projectRoot -RequestedPanelExe $PanelExe

if (-not (Test-Path $resolvedPanelExe)) {
    throw "Panel executable not found at $resolvedPanelExe. Build the project first."
}

if (-not (Test-Path $resolvedRunnerScript)) {
    throw "Bridge runner script not found at $resolvedRunnerScript."
}

$config = Load-PanelConfig -ConfigPath $configPath
$config["bridge_mode"] = "external"
$config["external_target_user"] = $User
$config["external_ws_port"] = $Port

$bridgeConfig = [ordered]@{}
if ($config.Contains("bridge") -and $config["bridge"] -is [System.Collections.IDictionary]) {
    $bridgeConfig = Convert-ToMutableConfig -InputObject $config["bridge"]
}

$sourceName = ""
if ($bridgeConfig.Contains("source_name") -and $null -ne $bridgeConfig["source_name"]) {
    $sourceName = [string]$bridgeConfig["source_name"]
}

$bridgeConfig["stub_mode"] = $false
if ([string]::IsNullOrWhiteSpace($sourceName) -or $sourceName -eq "tiktok" -or $sourceName -eq "tiktok-stub") {
    $bridgeConfig["source_name"] = "tiktok-external"
}
$config["bridge"] = $bridgeConfig

Save-PanelConfig -ConfigPath $configPath -Config $config

Write-Host "[launcher] panel config updated: bridge_mode=external, external_target_user=$User, external_ws_port=$Port"
Write-Host "[launcher] project root: $projectRoot"
Write-Host "[launcher] panel exe: $resolvedPanelExe"
Write-Host "[launcher] python exe: $resolvedPythonExe"
Write-Host "[launcher] runner script: $resolvedRunnerScript"

if (-not $NoStartPanel) {
    Write-Host "[launcher] starting panel console..."
    Start-PanelConsole -ProjectRoot $projectRoot -ResolvedPanelExe $resolvedPanelExe
    Start-Sleep -Seconds 2
} else {
    Write-Host "[launcher] panel start skipped (--NoStartPanel)"
}

$runnerArgs = @(
    $resolvedRunnerScript,
    "--user", $User,
    "--ws", "ws://127.0.0.1:$Port"
)

if ($MaxSeconds -gt 0) {
    $runnerArgs += @("--max-seconds", [string]$MaxSeconds)
}

Write-Host "[launcher] starting real TikTok runner..."
Write-Host "[launcher] command: $resolvedPythonExe $($runnerArgs -join ' ')"

Push-Location $projectRoot
try {
    & $resolvedPythonExe @runnerArgs
} finally {
    Pop-Location
}
