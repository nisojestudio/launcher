param(
    [string]$InstallRoot = "",
    [string]$PanelExe = "",
    [string]$ReportRoot = "",
    [int]$Port = 18913,
    [int]$WaitTimeoutSec = 45,
    [int]$DownloadWaitTimeoutSec = 240,
    [switch]$DoNotLaunchPanel,
    [switch]$LeavePanelRunning,
    [string]$Email = "",
    [string]$Password = "",
    [string]$LicenseKey = "",
    [string[]]$ExpectedRemoteGames = @("arena_live", "super_pang"),
    [string[]]$DownloadGames = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if (-not (Test-Path -LiteralPath $PathValue)) {
        New-Item -ItemType Directory -Path $PathValue -Force | Out-Null
    }
}

function Write-Utf8TextFile {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content
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

    if ($null -eq $Value) {
        Write-Utf8TextFile -PathValue $PathValue -Content "null`n"
        return
    }

    $json = $Value | ConvertTo-Json -Depth $Depth
    Write-Utf8TextFile -PathValue $PathValue -Content ($json + [Environment]::NewLine)
}

function Get-RegistryValueSafe {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$Name
    )

    try {
        $item = Get-ItemProperty -Path $PathValue -ErrorAction Stop
        return $item.$Name
    } catch {
        return $null
    }
}

function Get-VcRedistStatus {
    $basePath = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64"
    $installed = Get-RegistryValueSafe -PathValue $basePath -Name "Installed"
    $major = Get-RegistryValueSafe -PathValue $basePath -Name "Major"
    $minor = Get-RegistryValueSafe -PathValue $basePath -Name "Minor"
    $bld = Get-RegistryValueSafe -PathValue $basePath -Name "Bld"

    return [pscustomobject]@{
        installed = ($installed -eq 1)
        major = $major
        minor = $minor
        build = $bld
        registryPath = $basePath
    }
}

function Get-WebView2Status {
    $paths = @(
        "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "Registry::HKEY_CURRENT_USER\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    )

    foreach ($pathValue in $paths) {
        $version = Get-RegistryValueSafe -PathValue $pathValue -Name "pv"
        if ($null -ne $version -and -not [string]::IsNullOrWhiteSpace([string]$version) -and [string]$version -ne "0.0.0.0") {
            return [pscustomobject]@{
                installed = $true
                version = [string]$version
                registryPath = $pathValue
            }
        }
    }

    return [pscustomobject]@{
        installed = $false
        version = ""
        registryPath = ""
    }
}

function Get-FileEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$BaseRoot,
        [Parameter(Mandatory = $true)][string[]]$RelativePaths
    )

    $results = New-Object System.Collections.Generic.List[object]
    foreach ($relativePath in $RelativePaths) {
        $fullPath = Join-Path $BaseRoot $relativePath
        if (Test-Path -LiteralPath $fullPath) {
            $item = Get-Item -LiteralPath $fullPath
            $hash = $null
            if (-not $item.PSIsContainer) {
                $hash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToUpperInvariant()
            }
            $results.Add([pscustomobject]@{
                relativePath = $relativePath
                path = $fullPath
                exists = $true
                isDirectory = [bool]$item.PSIsContainer
                length = if ($item.PSIsContainer) { $null } else { [int64]$item.Length }
                sha256 = $hash
            })
        } else {
            $results.Add([pscustomobject]@{
                relativePath = $relativePath
                path = $fullPath
                exists = $false
                isDirectory = $false
                length = $null
                sha256 = $null
            })
        }
    }

    return $results
}

function Invoke-PanelRequest {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("GET", "POST")][string]$Method,
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Body = ""
    )

    $uri = "http://127.0.0.1:{0}{1}" -f $script:Port, $Path
    $statusCode = -1
    $content = ""

    try {
        if ($Method -eq "GET") {
            $response = Invoke-WebRequest -Uri $uri -Method Get -UseBasicParsing -TimeoutSec 10
        } else {
            $response = Invoke-WebRequest `
                -Uri $uri `
                -Method Post `
                -Body $Body `
                -ContentType "application/json" `
                -UseBasicParsing `
                -TimeoutSec 20
        }

        $statusCode = [int]$response.StatusCode
        $content = [string]$response.Content
    } catch {
        if ($_.Exception.Response) {
            $httpResponse = $_.Exception.Response
            $statusCode = [int]$httpResponse.StatusCode
            try {
                $reader = New-Object System.IO.StreamReader($httpResponse.GetResponseStream())
                try {
                    $content = $reader.ReadToEnd()
                } finally {
                    $reader.Dispose()
                }
            } catch {
                $content = ""
            }
        } else {
            $content = [string]$_.Exception.Message
        }
    }

    $json = $null
    if (-not [string]::IsNullOrWhiteSpace($content)) {
        try {
            $json = $content | ConvertFrom-Json
        } catch {
            $json = $null
        }
    }

    return [pscustomobject]@{
        method = $Method
        path = $Path
        uri = $uri
        statusCode = $statusCode
        content = $content
        json = $json
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

function Get-CatalogItem {
    param(
        [AllowNull()]$StateJson,
        [Parameter(Mandatory = $true)][string]$GameId
    )

    if ($null -eq $StateJson -or $null -eq $StateJson.catalog -or $null -eq $StateJson.catalog.items) {
        return $null
    }

    return @($StateJson.catalog.items) | Where-Object { $_.gameId -eq $GameId } | Select-Object -First 1
}

function Wait-ForCatalogItemState {
    param(
        [Parameter(Mandatory = $true)][string]$GameId,
        [int]$TimeoutSec
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $stateResponse = Invoke-PanelRequest -Method "GET" -Path "/api/state"
        $stateJson = $stateResponse.json
        $item = Get-CatalogItem -StateJson $stateJson -GameId $GameId
        if ($null -ne $item -and $item.installed) {
            return [pscustomobject]@{
                state = $stateJson
                item = $item
                completed = $true
            }
        }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $deadline)

    $finalResponse = Invoke-PanelRequest -Method "GET" -Path "/api/state"
    return [pscustomobject]@{
        state = $finalResponse.json
        item = Get-CatalogItem -StateJson $finalResponse.json -GameId $GameId
        completed = $false
    }
}

function Copy-IfExists {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    if (-not (Test-Path -LiteralPath $SourcePath)) {
        return $false
    }

    Ensure-Directory -PathValue (Split-Path -Parent $DestinationPath)
    Copy-Item -LiteralPath $SourcePath -Destination $DestinationPath -Force
    return $true
}

if ([string]::IsNullOrWhiteSpace($InstallRoot) -and [string]::IsNullOrWhiteSpace($PanelExe)) {
    $InstallRoot = Join-Path $env:ProgramFiles "Panel Live"
}
if ([string]::IsNullOrWhiteSpace($PanelExe)) {
    $PanelExe = Join-Path $InstallRoot "NisojeStudio.exe"
}
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Split-Path -Parent $PanelExe
}
if ([string]::IsNullOrWhiteSpace($ReportRoot)) {
    $desktopRoot = [Environment]::GetFolderPath("Desktop")
    $ReportRoot = Join-Path $desktopRoot ("PanelLiveCleanInstallEvidence-" + (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmss"))
}

$InstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$PanelExe = [System.IO.Path]::GetFullPath($PanelExe)
$ReportRoot = [System.IO.Path]::GetFullPath($ReportRoot)
$script:Port = $Port

Ensure-Directory -PathValue $ReportRoot

$summary = [ordered]@{
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    installRoot = $InstallRoot
    panelExe = $PanelExe
    reportRoot = $ReportRoot
    port = $Port
    prerequisites = $null
    installedFiles = @()
    panelApiReachable = $false
    authGate = $null
    login = $null
    remoteCatalog = $null
    downloads = @()
    supportBundle = $null
    localConfig = $null
    logs = @()
}

$environmentInfo = [ordered]@{
    generatedAtUtc = $summary.generatedAtUtc
    machineName = $env:COMPUTERNAME
    userName = $env:USERNAME
    isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    os = (Get-ComputerInfo -Property WindowsProductName,WindowsVersion,OsBuildNumber,OsHardwareAbstractionLayer)
}
Write-JsonFile -PathValue (Join-Path $ReportRoot "environment.json") -Value $environmentInfo

$summary.prerequisites = [ordered]@{
    vcRedist = Get-VcRedistStatus
    webView2 = Get-WebView2Status
}
Write-JsonFile -PathValue (Join-Path $ReportRoot "prerequisites.json") -Value $summary.prerequisites

$summary.installedFiles = Get-FileEvidence -BaseRoot $InstallRoot -RelativePaths @(
    "NisojeStudio.exe",
    "WebView2Loader.dll",
    "panel_config.json",
    "tools",
    "tools\\bridge_py\\python_runtime\\python.exe",
    "tools\\bridge_py\\bridge_env_check.py",
    "tools\\game_bridge_py\\run_local_game_bridge.py"
)
Write-JsonFile -PathValue (Join-Path $ReportRoot "installed-files.json") -Value $summary.installedFiles

$launchedProcess = $null
$scriptStartedProcess = $false
try {
    if (-not $DoNotLaunchPanel) {
        if (-not (Test-Path -LiteralPath $PanelExe)) {
            throw "Panel executable not found at $PanelExe"
        }

        $existingListener = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -eq $existingListener) {
            $launchedProcess = Start-Process -FilePath $PanelExe -WorkingDirectory $InstallRoot -PassThru
            $scriptStartedProcess = $true
        }
    }

    $stateResponse = Wait-ForPanelState -TimeoutSec $WaitTimeoutSec
    if ($null -ne $stateResponse) {
        $summary.panelApiReachable = $true
        Write-JsonFile -PathValue (Join-Path $ReportRoot "state-pre-auth.json") -Value $stateResponse.json

        $summary.authGate = [ordered]@{
            authRequired = [bool]$stateResponse.json.snapshot.auth.required
            authenticated = [bool]$stateResponse.json.snapshot.auth.authenticated
            targetUser = [string]$stateResponse.json.snapshot.externalBridge.targetUser
            runnerRunning = [bool]$stateResponse.json.snapshot.externalBridge.runnerRunning
            runtimeReady = [bool]$stateResponse.json.snapshot.externalBridge.runtimeReady
            wsRunning = [bool]$stateResponse.json.snapshot.externalWs.running
        }

        $reconnectResponse = Invoke-PanelRequest -Method "POST" -Path "/api/system/reconnect" -Body "{}"
        $downloadBlockedResponse = Invoke-PanelRequest -Method "POST" -Path "/api/game/download" -Body '{"gameId":"super_pang"}'
        $summary.authGate = [ordered]@{
            authRequired = $summary.authGate.authRequired
            authenticated = $summary.authGate.authenticated
            targetUser = $summary.authGate.targetUser
            runnerRunning = $summary.authGate.runnerRunning
            runtimeReady = $summary.authGate.runtimeReady
            wsRunning = $summary.authGate.wsRunning
            reconnectStatusCode = $reconnectResponse.statusCode
            downloadStatusCode = $downloadBlockedResponse.statusCode
        }
        Write-JsonFile -PathValue (Join-Path $ReportRoot "protected-endpoints.json") -Value @{
            reconnect = $reconnectResponse
            gameDownload = $downloadBlockedResponse
        }

        if (-not [string]::IsNullOrWhiteSpace($Email) -and -not [string]::IsNullOrWhiteSpace($Password) -and -not [string]::IsNullOrWhiteSpace($LicenseKey)) {

            $loginBody = @{
                email = $Email
                password = $Password
                licenseKey = $LicenseKey
                deviceName = "$($env:COMPUTERNAME)-vm"
                deviceId = "$($env:COMPUTERNAME)-vm"
            } | ConvertTo-Json -Compress

            $loginResponse = Invoke-PanelRequest -Method "POST" -Path "/api/auth/login" -Body $loginBody
            Write-JsonFile -PathValue (Join-Path $ReportRoot "login-response.json") -Value $loginResponse

            $postLoginState = Invoke-PanelRequest -Method "GET" -Path "/api/state"
            Write-JsonFile -PathValue (Join-Path $ReportRoot "state-post-auth.json") -Value $postLoginState.json

            $remoteGames = @()
            if ($null -ne $postLoginState.json -and $null -ne $postLoginState.json.catalog) {
                $remoteGames = @($postLoginState.json.catalog.items) | Where-Object { $_.source -eq "remote" }
            }

            $missingRemoteGames = @()
            foreach ($gameId in $ExpectedRemoteGames) {
                if (-not [string]::IsNullOrWhiteSpace($gameId)) {
                    $match = $remoteGames | Where-Object { $_.gameId -eq $gameId } | Select-Object -First 1
                    if ($null -eq $match) {
                        $missingRemoteGames += $gameId
                    }
                }
            }

            $summary.login = [ordered]@{
                attempted = $true
                ok = [bool]($loginResponse.json.ok)
                message = if ($null -ne $loginResponse.json) { [string]$loginResponse.json.message } else { "" }
                errorCode = if ($null -ne $loginResponse.json) { [string]$loginResponse.json.errorCode } else { "" }
                authenticated = if ($null -ne $postLoginState.json) { [bool]$postLoginState.json.snapshot.auth.authenticated } else { $false }
            }
            $summary.remoteCatalog = [ordered]@{
                expectedGames = $ExpectedRemoteGames
                discoveredRemoteGames = @($remoteGames | Select-Object -ExpandProperty gameId)
                missingExpectedGames = $missingRemoteGames
            }

            $gamesToDownload = if ($DownloadGames.Count -gt 0) { $DownloadGames } else { $ExpectedRemoteGames }
            foreach ($gameId in $gamesToDownload) {
                if ([string]::IsNullOrWhiteSpace($gameId)) {
                    continue
                }

                $requestBody = @{ gameId = $gameId } | ConvertTo-Json -Compress
                $downloadResponse = Invoke-PanelRequest -Method "POST" -Path "/api/game/download" -Body $requestBody
                $downloadState = Wait-ForCatalogItemState -GameId $gameId -TimeoutSec $DownloadWaitTimeoutSec

                $downloadRecord = [ordered]@{
                    gameId = $gameId
                    request = $downloadResponse
                    completed = [bool]$downloadState.completed
                    finalItem = $downloadState.item
                }
                $summary.downloads += $downloadRecord
                Write-JsonFile -PathValue (Join-Path $ReportRoot ("download-" + $gameId + ".json")) -Value $downloadRecord
            }
        } else {
            $summary.login = [ordered]@{
                attempted = $false
                ok = $false
                message = "Credentials not provided to the validation script."
                errorCode = "credentials_missing"
                authenticated = $false
            }
        }

        $supportResponse = Invoke-PanelRequest -Method "POST" -Path "/api/support/export" -Body '{"reason":"vm_clean_install_validation"}'
        Write-JsonFile -PathValue (Join-Path $ReportRoot "support-export.json") -Value $supportResponse
        if ($null -ne $supportResponse.json) {
            $summary.supportBundle = $supportResponse.json
            if (-not [string]::IsNullOrWhiteSpace([string]$supportResponse.json.path)) {
                $bundlePath = [string]$supportResponse.json.path
                $bundleName = Split-Path -Leaf $bundlePath
                Copy-IfExists -SourcePath $bundlePath -DestinationPath (Join-Path $ReportRoot $bundleName) | Out-Null
            }
        }
    } else {
        $summary.login = [ordered]@{
            attempted = $false
            ok = $false
            message = "Panel API was not reachable."
            errorCode = "api_unreachable"
            authenticated = $false
        }
    }

    $localConfigPath = Join-Path $env:LOCALAPPDATA "NisojeStudio\\panel_config.json"
    $summary.localConfig = [ordered]@{
        expectedPath = $localConfigPath
        exists = (Test-Path -LiteralPath $localConfigPath)
    }
    if ($summary.localConfig.exists) {
        Copy-IfExists -SourcePath $localConfigPath -DestinationPath (Join-Path $ReportRoot "panel_config.localappdata.json") | Out-Null
    }

    foreach ($candidate in @(
        (Join-Path $InstallRoot "embedded_ui.log"),
        (Join-Path $env:TEMP "NisojeStudio\\embedded_ui.log")
    )) {
        if (Test-Path -LiteralPath $candidate) {
            $destinationName = "log-" + ([System.IO.Path]::GetFileName($candidate))
            Copy-IfExists -SourcePath $candidate -DestinationPath (Join-Path $ReportRoot $destinationName) | Out-Null
            $summary.logs += $candidate
        }
    }
}
finally {
    if ($scriptStartedProcess -and -not $LeavePanelRunning -and $null -ne $launchedProcess) {
        try {
            if (-not $launchedProcess.HasExited) {
                Stop-Process -Id $launchedProcess.Id -Force
            }
        } catch {
        }
    }
}

Write-JsonFile -PathValue (Join-Path $ReportRoot "summary.json") -Value $summary

$summaryLines = @(
    "Panel Live clean-install validation summary",
    "generated_at_utc=" + $summary.generatedAtUtc,
    "install_root=" + $summary.installRoot,
    "panel_api_reachable=" + $summary.panelApiReachable,
    "auth_required=" + $(if ($null -ne $summary.authGate) { [string]$summary.authGate.authRequired } else { "false" }),
    "authenticated=" + $(if ($null -ne $summary.authGate) { [string]$summary.authGate.authenticated } else { "false" }),
    "reconnect_status=" + $(if ($null -ne $summary.authGate) { [string]$summary.authGate.reconnectStatusCode } else { "-1" }),
    "download_status_before_auth=" + $(if ($null -ne $summary.authGate) { [string]$summary.authGate.downloadStatusCode } else { "-1" }),
    "login_attempted=" + [string]$summary.login.attempted,
    "login_ok=" + [string]$summary.login.ok,
    "login_message=" + [string]$summary.login.message,
    "report_root=" + $summary.reportRoot
)
Write-Utf8TextFile -PathValue (Join-Path $ReportRoot "SUMMARY.txt") -Content (($summaryLines -join [Environment]::NewLine) + [Environment]::NewLine)

Write-Host "Validation evidence saved to: $ReportRoot"
