param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$Changelog = "",

    [string]$Repo = "nisojestudio/launcher",

    [string]$ReleaseDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ProjectRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Assert-SemVer {
    param([string]$Value)
    if ($Value -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version must use MAJOR.MINOR.PATCH format, for example 0.2.0. Received: $Value"
    }
}

function Invoke-GitHub {
    param([string[]]$Arguments)
    $output = & gh @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $errorText = ($output | Out-String).Trim()
        throw "gh command failed (exit $exitCode): gh $($Arguments -join ' ')`n$errorText"
    }
    return ($output | Out-String).Trim()
}

function Get-ReleaseInfo {
    param([string]$Tag)
    $oldEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $json = gh release view $Tag --repo $Repo --json isDraft,isPrerelease,assets,url 2>$null
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $oldEAP
    if ($exitCode -ne 0 -or [string]::IsNullOrWhiteSpace($json)) {
        return $null
    }
    try { return $json | ConvertFrom-Json } catch { return $null }
}

# --- validate ---
Assert-SemVer -Value $Version
$projectRoot = Resolve-ProjectRoot

if ([string]::IsNullOrWhiteSpace($ReleaseDir)) {
    $ReleaseDir = Join-Path $projectRoot "dist\releases\$Version"
}
$ReleaseDir = [System.IO.Path]::GetFullPath($ReleaseDir)

if (-not (Test-Path $ReleaseDir)) {
    throw "Release directory not found: $ReleaseDir. Run prepare_release.ps1 first."
}

# --- locate assets ---
$installerDir = Join-Path $ReleaseDir "installer"
$exeFile = [System.IO.Path]::GetFullPath((Join-Path $installerDir "panel-live-$Version-win-x64.exe"))
$zipFile = [System.IO.Path]::GetFullPath((Join-Path $ReleaseDir "panel-live-$Version-win-x64-portable.zip"))
$shaFile = [System.IO.Path]::GetFullPath((Join-Path $ReleaseDir "SHA256SUMS.txt"))

$assets = @()
if (Test-Path $exeFile) { $assets += $exeFile }
if (Test-Path $zipFile) { $assets += $zipFile }
if (Test-Path $shaFile) { $assets += $shaFile }

if ($assets.Count -eq 0) {
    throw "No release assets found in $ReleaseDir. Run prepare_release.ps1 first.`nExpected: panel-live-$Version-win-x64.exe, panel-live-$Version-win-x64-portable.zip, SHA256SUMS.txt"
}

# --- check if tag already exists ---
$tag = "v$Version"
$existingRelease = Get-ReleaseInfo -Tag $tag
if ($null -ne $existingRelease) {
    throw "Release $tag already exists at $($existingRelease.url). Use a new version or clean the draft manually before retrying."
}

Write-Host "[github] Creating draft release: $tag"
Invoke-GitHub @(
    "release", "create", $tag,
    "--repo", $Repo,
    "--title", "Panel Live $Version",
    "--notes", $Changelog,
    "--draft",
    "--prerelease"
)

Write-Host "[github] Uploading $($assets.Count) assets..."
Invoke-GitHub @(
    "release", "upload", $tag,
    "--repo", $Repo,
    "--clobber"
) + $assets

Write-Host "[github] Verifying upload..."
Start-Sleep -Seconds 3
$info = Get-ReleaseInfo -Tag $tag
if ($null -eq $info) {
    throw "Release $tag was not found after upload."
}
$assetNames = @($info.assets | ForEach-Object { $_.name })
$expectedFiles = @((Split-Path -Leaf $exeFile), (Split-Path -Leaf $zipFile), (Split-Path -Leaf $shaFile))
foreach ($expectedFile in $expectedFiles) {
    if (Test-Path (Join-Path $ReleaseDir $expectedFile) -or (Test-Path (Join-Path $installerDir $expectedFile))) {
        if ($assetNames -notcontains $expectedFile) {
            Write-Warning "[github] Missing expected asset from release: $expectedFile"
        }
    }
}

Write-Host "[github] Publishing release..."
Invoke-GitHub @(
    "release", "edit", $tag,
    "--repo", $Repo,
    "--draft=false",
    "--prerelease"
)

$info = Get-ReleaseInfo -Tag $tag
Write-Host "[github] Published: $($info.url)"
Write-Host "[github] Done."
