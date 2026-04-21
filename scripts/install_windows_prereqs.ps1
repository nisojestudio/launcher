param(
    [string]$DependencyRoot = "",
    [string]$VcRedistUrl = "https://aka.ms/vc14/vc_redist.x64.exe",
    [string]$WebView2RuntimeUrl = "https://go.microsoft.com/fwlink/p/?LinkId=2124703",
    [switch]$ForceVcRedist,
    [switch]$ForceWebView2,
    [switch]$SkipDownloads
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

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

function Get-VcRedistStatus {
    $key = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64"
    try {
        $item = Get-ItemProperty -Path $key -ErrorAction Stop
        return [pscustomobject]@{
            installed = ($item.Installed -eq 1)
            version = "{0}.{1}.{2}" -f $item.Major, $item.Minor, $item.Bld
        }
    } catch {
        return [pscustomobject]@{
            installed = $false
            version = ""
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
                }
            }
        } catch {
        }
    }

    return [pscustomobject]@{
        installed = $false
        version = ""
    }
}

function Ensure-DownloadedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    if (Test-Path -LiteralPath $DestinationPath) {
        $item = Get-Item -LiteralPath $DestinationPath
        if ($item.Length -gt 0) {
            return
        }
    }

    if ($SkipDownloads) {
        throw "Missing bundled dependency and downloads are disabled: $DestinationPath"
    }

    Ensure-Directory -PathValue (Split-Path -Parent $DestinationPath)
    Write-Log "Downloading $Uri"
    Invoke-WebRequest -Uri $Uri -OutFile $DestinationPath
}

function Run-Installer {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$FriendlyName
    )

    Write-Log "Installing $FriendlyName from $FilePath"
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -PassThru -Wait
    Write-Log "$FriendlyName finished with exit code $($process.ExitCode)"
    return $process.ExitCode
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($DependencyRoot)) {
    $DependencyRoot = Join-Path $projectRoot "build\installer_cache"
}
$DependencyRoot = [System.IO.Path]::GetFullPath($DependencyRoot)

$logRoot = if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    Join-Path $env:LOCALAPPDATA "NisojeStudio\support"
} else {
    Join-Path $env:TEMP "NisojeStudio\support"
}
Ensure-Directory -PathValue $logRoot
$script:LogPath = Join-Path $logRoot ("install-windows-prereqs-" + (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmss") + ".log")
Set-Content -LiteralPath $script:LogPath -Value "" -Encoding UTF8

$vcRedistExe = Join-Path $DependencyRoot "vc_redist.x64.exe"
$webView2Exe = Join-Path $DependencyRoot "MicrosoftEdgeWebView2RuntimeInstallerX64.exe"

$vcStatusBefore = Get-VcRedistStatus
$webView2StatusBefore = Get-WebView2Status
Write-Log "VC++ installed before run: $($vcStatusBefore.installed) version=$($vcStatusBefore.version)"
Write-Log "WebView2 installed before run: $($webView2StatusBefore.installed) version=$($webView2StatusBefore.version)"

$restartRequired = $false

if ($ForceVcRedist -or -not $vcStatusBefore.installed) {
    Ensure-DownloadedFile -Uri $VcRedistUrl -DestinationPath $vcRedistExe
    $exitCode = Run-Installer -FilePath $vcRedistExe -Arguments @("/install", "/quiet", "/norestart") -FriendlyName "Microsoft Visual C++ Redistributable x64"
    if ($exitCode -eq 3010) {
        $restartRequired = $true
    } elseif ($exitCode -ne 0) {
        Write-Log "VC++ installation failed."
        exit 11
    }
} else {
    Write-Log "Skipping VC++ installation because it is already present."
}

if ($ForceWebView2 -or -not $webView2StatusBefore.installed) {
    Ensure-DownloadedFile -Uri $WebView2RuntimeUrl -DestinationPath $webView2Exe
    $exitCode = Run-Installer -FilePath $webView2Exe -Arguments @("/silent", "/install") -FriendlyName "Microsoft Edge WebView2 Runtime"
    if ($exitCode -eq 3010) {
        $restartRequired = $true
    } elseif ($exitCode -ne 0) {
        Write-Log "WebView2 installation failed."
        exit 12
    }
} else {
    Write-Log "Skipping WebView2 installation because it is already present."
}

$vcStatusAfter = Get-VcRedistStatus
$webView2StatusAfter = Get-WebView2Status
Write-Log "VC++ installed after run: $($vcStatusAfter.installed) version=$($vcStatusAfter.version)"
Write-Log "WebView2 installed after run: $($webView2StatusAfter.installed) version=$($webView2StatusAfter.version)"

if (-not $vcStatusAfter.installed) {
    exit 11
}

if (-not $webView2StatusAfter.installed) {
    exit 12
}

if ($restartRequired) {
    Write-Log "At least one prerequisite requested a reboot."
    exit 13
}

Write-Log "Prerequisites are installed and ready."
Write-Host "Log written to: $script:LogPath"
exit 0
