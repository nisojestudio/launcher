param(
    [string]$ConfigurePreset = "release",
    [string]$BuildPreset = "release",
    [string]$BuildDir = "",
    [string]$OutputRoot = "",
    [switch]$SkipBuild,
    [string]$VcRedistUrl = "https://aka.ms/vc14/vc_redist.x64.exe",
    [string]$WebView2RuntimeUrl = "https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/0622d6c1-fc78-41a1-88d9-7097d919158f/MicrosoftEdgeWebView2RuntimeInstallerX64.exe",
    [string]$InnoCompiler = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Resolve-ProjectRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Remove-IfExists {
    param([string]$PathValue)
    if (Test-Path $PathValue) {
        Remove-Item -LiteralPath $PathValue -Recurse -Force
    }
}

function Ensure-DownloadedFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Uri,

        [Parameter(Mandatory = $true)]
        [string]$DestinationPath
    )

    $destinationDir = Split-Path -Parent $DestinationPath
    if (-not (Test-Path $destinationDir)) {
        New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
    }

    if (Test-Path $DestinationPath) {
        $item = Get-Item -LiteralPath $DestinationPath
        if ($item.Length -gt 0) {
            return
        }
    }

    Write-Host "[installer] downloading $Uri"
    Invoke-WebRequest -Uri $Uri -OutFile $DestinationPath
}

function Write-Sha256SumsFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputRoot,

        [Parameter(Mandatory = $true)]
        [string[]]$FilePaths
    )

    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
    $lines = foreach ($filePath in $FilePaths) {
        if (-not (Test-Path $filePath)) {
            continue
        }

        $resolvedFilePath = [System.IO.Path]::GetFullPath($filePath)
        $relativePath = $resolvedFilePath.Substring($resolvedOutputRoot.Length).TrimStart('\').Replace('/', '\')
        $hash = (Get-FileHash -LiteralPath $resolvedFilePath -Algorithm SHA256).Hash.ToUpperInvariant()
        "{0}  {1}" -f $hash, $relativePath
    }

    [System.IO.File]::WriteAllLines((Join-Path $OutputRoot "SHA256SUMS.txt"), $lines, $utf8)
}

function Resolve-IsccPath {
    param([string]$RequestedPath)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        $candidates += $RequestedPath
    }

    $candidates += @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
    )

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    Write-Host "[installer] Inno Setup 6 not found, installing with winget..."
    winget install `
        --id JRSoftware.InnoSetup `
        --exact `
        --source winget `
        --accept-source-agreements `
        --accept-package-agreements `
        --disable-interactivity

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "ISCC.exe was not found after installing Inno Setup."
}

$projectRoot = Resolve-ProjectRoot
Push-Location $projectRoot
try {
    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        $OutputRoot = Join-Path $projectRoot "dist"
    }

    $packageScript = Join-Path $projectRoot "scripts\package_windows.ps1"
    $installerScript = Join-Path $projectRoot "installer\panel_live.iss"
    $packageRoot = [System.IO.Path]::GetFullPath((Join-Path $OutputRoot "NisojeStudio"))
    $installerCache = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "build\installer_cache"))
    $installerOutput = [System.IO.Path]::GetFullPath((Join-Path $OutputRoot "installer"))
    $setupIcon = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "assets\branding\panel_live.ico"))
    $vcRedistPath = [System.IO.Path]::GetFullPath((Join-Path $installerCache "vc_redist.x64.exe"))
    $webView2Path = [System.IO.Path]::GetFullPath((Join-Path $installerCache "MicrosoftEdgeWebView2RuntimeInstallerX64.exe"))

    if (-not $SkipBuild) {
        Write-Host "[installer] building portable package source..."
        $packageParams = @{
            OutputRoot = $OutputRoot
            PanelName = "Panel Live"
            RequireRemoteAuth = $true
            ClearExternalTargetUser = $true
            AllowBlankExternalTargetUser = $true
        }

        if (-not [string]::IsNullOrWhiteSpace($ConfigurePreset)) {
            $packageParams.ConfigurePreset = $ConfigurePreset
        }

        if (-not [string]::IsNullOrWhiteSpace($BuildPreset)) {
            $packageParams.BuildPreset = $BuildPreset
        }

        if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
            $packageParams.BuildDir = $BuildDir
        }

        & $packageScript @packageParams
    }

    if (-not (Test-Path $packageRoot)) {
        throw "Package root not found at $packageRoot. Run without -SkipBuild first."
    }

    Ensure-DownloadedFile -Uri $VcRedistUrl -DestinationPath $vcRedistPath
    Ensure-DownloadedFile -Uri $WebView2RuntimeUrl -DestinationPath $webView2Path

    if (-not (Test-Path $setupIcon)) {
        throw "Installer icon not found at $setupIcon"
    }

    Remove-IfExists $installerOutput
    New-Item -ItemType Directory -Path $installerOutput -Force | Out-Null

    $isccPath = Resolve-IsccPath -RequestedPath $InnoCompiler
    Write-Host "[installer] using ISCC: $isccPath"

    & $isccPath `
        "/DPackageRoot=$packageRoot" `
        "/DDependencyRoot=$installerCache" `
        "/DOutputRoot=$installerOutput" `
        "/DSetupIcon=$setupIcon" `
        $installerScript

    $setupExe = Join-Path $installerOutput "PanelLive-3.0-Windows-x64-Setup.exe"
    if (-not (Test-Path $setupExe)) {
        throw "Installer was not generated at $setupExe"
    }

    Write-Sha256SumsFile `
        -OutputRoot $OutputRoot `
        -FilePaths @(
            (Join-Path $packageRoot "NisojeStudio.exe"),
            (Join-Path $OutputRoot "NisojeStudio-portable.zip"),
            $setupExe
        )

    Write-Host "[installer] setup generated: $setupExe"
    Write-Host "[installer] package source: $packageRoot"
    Write-Host "[installer] bundled prerequisites: $vcRedistPath, $webView2Path"
} finally {
    Pop-Location
}
