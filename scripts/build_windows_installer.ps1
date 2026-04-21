param(
    [string]$Version = "0.1.0",
    [string]$ConfigurePreset = "release",
    [string]$BuildPreset = "release",
    [string]$BuildDir = "",
    [string]$OutputRoot = "",
    [switch]$SkipBuild,
    [switch]$SkipManifest,
    [switch]$AllowDirtyManifest,
    [ValidateSet("pending", "passed", "failed", "skipped", "unknown")]
    [string]$TestsStatus = "pending",
    [ValidateSet("pending", "passed", "failed", "skipped", "unknown")]
    [string]$BackupStatus = "pending",
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

function Assert-SemVer {
    param([string]$Value)

    if ($Value -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version must use MAJOR.MINOR.PATCH format, for example 0.2.0. Received: $Value"
    }
}

function ConvertTo-WindowsVersionInfo {
    param([string]$Value)

    Assert-SemVer -Value $Value
    return "$Value.0"
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
    Assert-SemVer -Value $Version

    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        $OutputRoot = Join-Path $projectRoot ("dist\releases\{0}" -f $Version)
    }
    $OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

    $portableZipName = "panel-live-$Version-win-x64-portable.zip"
    $portableZipPath = [System.IO.Path]::GetFullPath((Join-Path $OutputRoot $portableZipName))
    $setupBaseName = "panel-live-$Version-win-x64"
    $windowsVersionInfo = ConvertTo-WindowsVersionInfo -Value $Version
    $sha256SumsPath = [System.IO.Path]::GetFullPath((Join-Path $OutputRoot "SHA256SUMS.txt"))

    $packageScript = Join-Path $projectRoot "scripts\package_windows.ps1"
    $manifestScript = Join-Path $projectRoot "scripts\release\new_release_manifest.ps1"
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
            PortableZipName = $portableZipName
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
        "/DAppVersion=$Version" `
        "/DSetupBaseName=$setupBaseName" `
        "/DVersionInfoVersionValue=$windowsVersionInfo" `
        "/DPackageRoot=$packageRoot" `
        "/DDependencyRoot=$installerCache" `
        "/DOutputRoot=$installerOutput" `
        "/DSetupIcon=$setupIcon" `
        $installerScript

    $setupExe = Join-Path $installerOutput "$setupBaseName.exe"
    if (-not (Test-Path $setupExe)) {
        throw "Installer was not generated at $setupExe"
    }

    Write-Sha256SumsFile `
        -OutputRoot $OutputRoot `
        -FilePaths @(
            (Join-Path $packageRoot "NisojeStudio.exe"),
            $portableZipPath,
            $setupExe
        )

    if (-not $SkipManifest) {
        $manifestParams = @{
            Version = $Version
            OutputDir = $OutputRoot
            ArtifactPaths = @(
                $portableZipPath,
                $setupExe,
                $sha256SumsPath
            )
            BuildStatus = $(if ($SkipBuild) { "skipped" } else { "passed" })
            TestsStatus = $TestsStatus
            InstallerStatus = "passed"
            BackupStatus = $BackupStatus
        }
        if ($AllowDirtyManifest) {
            $manifestParams.AllowDirty = $true
        }

        & $manifestScript @manifestParams
    }

    Write-Host "[installer] setup generated: $setupExe"
    Write-Host "[installer] portable zip:    $portableZipPath"
    Write-Host "[installer] checksums:       $sha256SumsPath"
    Write-Host "[installer] package source: $packageRoot"
    Write-Host "[installer] bundled prerequisites: $vcRedistPath, $webView2Path"
} finally {
    Pop-Location
}
