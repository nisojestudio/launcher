param(
    [string]$InstallRoot = "",
    [string]$ReportRoot = "",
    [string]$PanelExe = "",
    [int]$UiPort = 18913,
    [int]$WaitTimeoutSec = 30,
    [switch]$LaunchPanel,
    [string]$TikTokUser = "",
    [int]$TikTokProbeSeconds = 20,
    [switch]$RequireTikTokEvent
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if (-not (Test-Path -LiteralPath $PathValue)) {
        New-Item -ItemType Directory -Path $PathValue -Force | Out-Null
    }
}

function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$Content
    )

    Ensure-Directory -PathValue (Split-Path -Parent $PathValue)
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($PathValue, $Content, $utf8)
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [AllowNull()]$Value,
        [int]$Depth = 12
    )

    $json = if ($null -eq $Value) { "null" } else { $Value | ConvertTo-Json -Depth $Depth }
    Write-Utf8File -PathValue $PathValue -Content ($json + [Environment]::NewLine)
}

function Get-VcRedistStatus {
    $key = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64"
    try {
        $item = Get-ItemProperty -Path $key -ErrorAction Stop
        return [pscustomobject]@{
            installed = ($item.Installed -eq 1)
            version = "{0}.{1}.{2}" -f $item.Major, $item.Minor, $item.Bld
            registryPath = $key
        }
    } catch {
        return [pscustomobject]@{
            installed = $false
            version = ""
            registryPath = $key
        }
    }
}

function Get-WebView2Status {
    $paths = @(
        "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "Registry::HKEY_CURRENT_USER\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    )

    foreach ($pathValue in $paths) {
        try {
            $item = Get-ItemProperty -Path $pathValue -ErrorAction Stop
            $version = [string]$item.pv
            if (-not [string]::IsNullOrWhiteSpace($version) -and $version -ne "0.0.0.0") {
                return [pscustomobject]@{
                    installed = $true
                    version = $version
                    registryPath = $pathValue
                }
            }
        } catch {
        }
    }

    return [pscustomobject]@{
        installed = $false
        version = ""
        registryPath = ""
    }
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
            $json = $null
        }
    }

    return [pscustomobject]@{
        exitCode = $exitCode
        text = $text
        json = $json
    }
}

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

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
}
if ([string]::IsNullOrWhiteSpace($PanelExe)) {
    $PanelExe = Join-Path $InstallRoot "NisojeStudio.exe"
}
if ([string]::IsNullOrWhiteSpace($ReportRoot)) {
    $baseReportRoot = if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        Join-Path $env:LOCALAPPDATA "NisojeStudio\support"
    } else {
        Join-Path $env:TEMP "NisojeStudio\support"
    }
    $ReportRoot = Join-Path $baseReportRoot ("post-install-validation-" + (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmss"))
}

$InstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$PanelExe = [System.IO.Path]::GetFullPath($PanelExe)
$ReportRoot = [System.IO.Path]::GetFullPath($ReportRoot)
$bridgeRoot = Join-Path $InstallRoot "tools\bridge_py"
$script:UiPort = $UiPort

Ensure-Directory -PathValue $ReportRoot

$pythonExe = Join-Path $bridgeRoot "python_runtime\python.exe"
$bridgeEnvCheck = Join-Path $bridgeRoot "bridge_env_check.py"
$tiktokProbe = Join-Path $bridgeRoot "test_tiktok_connection.py"
$bridgeLog = if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    Join-Path $env:LOCALAPPDATA "NisojeStudio\logs\bridge.jsonl"
} else {
    Join-Path $env:TEMP "NisojeStudio\logs\bridge.jsonl"
}

$summary = [ordered]@{
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    installRoot = $InstallRoot
    bridgeRoot = $bridgeRoot
    panelExe = $PanelExe
    reportRoot = $ReportRoot
    panelApiReachable = $false
    prerequisites = @{
        vcRedist = Get-VcRedistStatus
        webView2 = Get-WebView2Status
    }
    files = @()
    bridgeEnvCheck = $null
    tiktokProbe = $null
    exitCode = 0
}

if (-not $summary.prerequisites.vcRedist.installed -or -not $summary.prerequisites.webView2.installed) {
    $summary.exitCode = 22
}

$summary.files = @(
    "NisojeStudio.exe",
    "panel_config.json",
    "tools\bridge_py\python_runtime\python.exe",
    "tools\bridge_py\bridge_env_check.py",
    "tools\bridge_py\test_tiktok_connection.py"
) | ForEach-Object {
    $pathValue = Join-Path $InstallRoot $_
    [pscustomobject]@{
        relativePath = $_
        exists = (Test-Path -LiteralPath $pathValue)
        path = $pathValue
    }
}

$missingCritical = @($summary.files | Where-Object { -not $_.exists })
if ($missingCritical.Count -gt 0) {
    $summary.exitCode = 20
    Write-JsonFile -PathValue (Join-Path $ReportRoot "summary.json") -Value $summary
    throw "Faltan archivos criticos en la instalacion: $($missingCritical.relativePath -join ', ')"
}

if (-not (Test-Path -LiteralPath $pythonExe)) {
    $summary.exitCode = 21
    Write-JsonFile -PathValue (Join-Path $ReportRoot "summary.json") -Value $summary
    throw "No se encontro python_runtime en $pythonExe"
}

$bridgeEnvCheckResult = Invoke-PythonJson -PythonExe $pythonExe -Arguments @(
    $bridgeEnvCheck,
    "--format", "json",
    "--config-path", (Join-Path $InstallRoot "panel_config.json")
) -Workdir $InstallRoot
$summary.bridgeEnvCheck = @{
    exitCode = $bridgeEnvCheckResult.exitCode
    report = $bridgeEnvCheckResult.json
    rawText = $bridgeEnvCheckResult.text
}
Write-JsonFile -PathValue (Join-Path $ReportRoot "bridge-env-check.json") -Value $summary.bridgeEnvCheck

if ($bridgeEnvCheckResult.exitCode -ne 0) {
    $summary.exitCode = 23
}

$panelProcess = $null
$startedPanel = $false
try {
    if ($LaunchPanel) {
        $existingListener = Get-NetTCPConnection -LocalPort $UiPort -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $existingListener) {
            $panelProcess = Start-Process -FilePath $PanelExe -WorkingDirectory $InstallRoot -PassThru
            $startedPanel = $true
        }

        $stateResponse = Wait-ForPanelState -TimeoutSec $WaitTimeoutSec
        if ($null -ne $stateResponse) {
            $summary.panelApiReachable = $true
            Write-JsonFile -PathValue (Join-Path $ReportRoot "panel-state.json") -Value $stateResponse.json
        } else {
            $summary.exitCode = if ($summary.exitCode -eq 0) { 24 } else { $summary.exitCode }
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($TikTokUser)) {
        $probeArgs = @(
            $tiktokProbe,
            "--user", $TikTokUser,
            "--max-seconds", [string]$TikTokProbeSeconds,
            "--report-path", (Join-Path $ReportRoot "tiktok-probe.json")
        )
        if ($RequireTikTokEvent) {
            $probeArgs += "--require-event"
        }

        $probeResult = Invoke-PythonJson -PythonExe $pythonExe -Arguments $probeArgs -Workdir $InstallRoot
        $summary.tiktokProbe = @{
            exitCode = $probeResult.exitCode
            report = $probeResult.json
            rawText = $probeResult.text
        }
        if ($probeResult.exitCode -ne 0 -and $summary.exitCode -eq 0) {
            $summary.exitCode = 25
        }
    }

    $summary.logs = @{
        bridgeLogPath = $bridgeLog
        bridgeLogExists = (Test-Path -LiteralPath $bridgeLog)
    }
    if ($summary.logs.bridgeLogExists) {
        Copy-Item -LiteralPath $bridgeLog -Destination (Join-Path $ReportRoot "bridge.jsonl") -Force
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

Write-JsonFile -PathValue (Join-Path $ReportRoot "summary.json") -Value $summary

$summaryText = @(
    "Panel Live post-install validation",
    "generated_at_utc=$($summary.generatedAtUtc)",
    "install_root=$($summary.installRoot)",
    "vc_redist_installed=$([string]$summary.prerequisites.vcRedist.installed)",
    "webview2_installed=$([string]$summary.prerequisites.webView2.installed)",
    "bridge_env_ok=$([string]($summary.bridgeEnvCheck.exitCode -eq 0))",
    "panel_api_reachable=$([string]$summary.panelApiReachable)",
    "tiktok_probe_exit=$([string]$(if ($null -ne $summary.tiktokProbe) { $summary.tiktokProbe.exitCode } else { '' }))",
    "report_root=$($summary.reportRoot)",
    "exit_code=$($summary.exitCode)"
) -join [Environment]::NewLine
Write-Utf8File -PathValue (Join-Path $ReportRoot "SUMMARY.txt") -Content ($summaryText + [Environment]::NewLine)

Write-Host "Validation evidence saved to: $ReportRoot"
exit $summary.exitCode
