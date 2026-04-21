param(
    [string]$InstallRoot = "",
    [string]$PanelExe = "",
    [int]$UiPort = 18913,
    [int]$WaitTimeoutSec = 30,
    [switch]$LaunchPanelIfNeeded,
    [switch]$RunSyntheticBurst
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-PanelRequest {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("GET", "POST")][string]$Method,
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Body = ""
    )

    $uri = "http://127.0.0.1:{0}{1}" -f $script:UiPort, $Path
    try {
        if ($Method -eq "GET") {
            $response = Invoke-WebRequest -Uri $uri -Method Get -UseBasicParsing -TimeoutSec 10
        } else {
            $response = Invoke-WebRequest -Uri $uri -Method Post -Body $Body -ContentType "application/json" -UseBasicParsing -TimeoutSec 15
        }

        $json = $null
        try {
            $json = $response.Content | ConvertFrom-Json -ErrorAction Stop
        } catch {
        }

        return [pscustomobject]@{
            statusCode = [int]$response.StatusCode
            content = [string]$response.Content
            json = $json
        }
    } catch {
        return [pscustomobject]@{
            statusCode = -1
            content = [string]$_.Exception.Message
            json = $null
        }
    }
}

function Wait-ForPanelState {
    param([int]$TimeoutSec)

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $response = Invoke-PanelRequest -Method "GET" -Path "/api/state"
        if ($response.statusCode -eq 200 -and $null -ne $response.json) {
            return $response
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    return $null
}

function Invoke-PythonJson {
    param(
        [Parameter(Mandatory = $true)][string]$PythonExe,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$Workdir = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($Workdir)) {
        Push-Location $Workdir
    }
    try {
        $output = & $PythonExe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        if (-not [string]::IsNullOrWhiteSpace($Workdir)) {
            Pop-Location
        }
    }
    $text = ($output | Out-String).Trim()
    $json = $null
    if (-not [string]::IsNullOrWhiteSpace($text)) {
        try {
            $json = $text | ConvertFrom-Json -ErrorAction Stop
        } catch {
        }
    }

    return [pscustomobject]@{
        exitCode = $exitCode
        text = $text
        json = $json
    }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
}
if ([string]::IsNullOrWhiteSpace($PanelExe)) {
    $PanelExe = Join-Path $InstallRoot "NisojeStudio.exe"
}

$InstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$PanelExe = [System.IO.Path]::GetFullPath($PanelExe)
$bridgeRoot = Join-Path $InstallRoot "tools\bridge_py"
$script:UiPort = $UiPort

$pythonExe = Join-Path $bridgeRoot "python_runtime\python.exe"
$bridgeEnvCheck = Join-Path $bridgeRoot "bridge_env_check.py"
$runBridge = Join-Path $bridgeRoot "run_tiktok_bridge.py"

$result = [ordered]@{
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    installRoot = $InstallRoot
    panelExe = $PanelExe
    panelApiReachable = $false
    bridgeEnvCheck = $null
    syntheticBurst = $null
    state = $null
    metrics = $null
    exitCode = 0
}

if (-not (Test-Path -LiteralPath $pythonExe)) {
    $result.exitCode = 20
    $result.error = "python_runtime missing"
    $result | ConvertTo-Json -Depth 12
    exit $result.exitCode
}

$result.bridgeEnvCheck = Invoke-PythonJson -PythonExe $pythonExe -Arguments @(
    $bridgeEnvCheck,
    "--format", "json",
    "--config-path", (Join-Path $InstallRoot "panel_config.json")
) -Workdir $InstallRoot
if ($result.bridgeEnvCheck.exitCode -ne 0) {
    $result.exitCode = 21
}

$panelProcess = $null
$startedPanel = $false
try {
    if ($LaunchPanelIfNeeded) {
        $existingListener = Get-NetTCPConnection -LocalPort $UiPort -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $existingListener) {
            $panelProcess = Start-Process -FilePath $PanelExe -WorkingDirectory $InstallRoot -PassThru
            $startedPanel = $true
        }
    }

    $stateResponse = Wait-ForPanelState -TimeoutSec $WaitTimeoutSec
    if ($null -ne $stateResponse) {
        $result.panelApiReachable = $true
        $result.state = $stateResponse.json
        $metricsResponse = Invoke-PanelRequest -Method "GET" -Path "/api/metrics"
        if ($metricsResponse.statusCode -eq 200) {
            $result.metrics = $metricsResponse.json
        }
    } elseif ($result.exitCode -eq 0) {
        $result.exitCode = 22
    }

    if ($RunSyntheticBurst) {
        $result.syntheticBurst = Invoke-PythonJson -PythonExe $pythonExe -Arguments @(
            $runBridge,
            "--simulate-burst", "3",
            "--max-seconds", "5",
            "--no-broadcast-ws"
        ) -Workdir $InstallRoot
        if ($result.syntheticBurst.exitCode -ne 0 -and $result.exitCode -eq 0) {
            $result.exitCode = 23
        }
    }
}
finally {
    if ($startedPanel -and $null -ne $panelProcess) {
        try {
            if (-not $panelProcess.HasExited) {
                Stop-Process -Id $panelProcess.Id -Force
            }
        } catch {
        }
    }
}

$result | ConvertTo-Json -Depth 12
exit $result.exitCode
