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

function Quote-NativeArgument {
    param([string]$Arg)
    if ($Arg -match '\s|"') {
        # CmdLineToArgvW rules: escape backslashes before the closing quote, then the quote.
        return '"' + ($Arg -replace '(\\+)("|$)', '$1$1$2' -replace '"', '\"') + '"'
    }
    return $Arg
}

function Invoke-GitHub {
    param([string[]]$Arguments)
    # Native arg parsing in PowerShell 5.1 silently splits arguments that
    # contain spaces (like "Panel live 3.0" paths). Quote them ourselves and
    # launch gh via System.Diagnostics.Process instead of relying on splatting.
    # Stdout is drained asynchronously to avoid pipe-buffer deadlocks on long
    # upload progress output.
    $argv = @($Arguments | ForEach-Object { Quote-NativeArgument $_ })
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "gh"
    $psi.Arguments = ($argv -join ' ')
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($psi)
    try {
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $err = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        $out = $outTask.Result
        if ($process.ExitCode -ne 0) {
            throw "gh command failed (exit $($process.ExitCode)): gh $($Arguments -join ' ')`n$(("$out $err").Trim())"
        }
        return ($out -as [string]).Trim()
    } finally {
        $process.Dispose()
    }
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
    if (-not $existingRelease.isDraft) {
        throw "Release $tag already exists and is published at $($existingRelease.url). Use a new version or clean it manually before retrying."
    }
    Write-Host "[github] Reusing existing draft release: $tag (resuming upload)"
} else {
    Write-Host "[github] Creating draft release: $tag"
    Invoke-GitHub -Arguments @(
        "release", "create", $tag,
        "--repo", $Repo,
        "--title", "Panel Live $Version",
        "--notes", $Changelog,
        "--draft",
        "--prerelease"
    )
}

Write-Host "[github] Uploading $($assets.Count) assets..."
$uploadArgs = @(
    "release", "upload", $tag,
    "--repo", $Repo,
    "--clobber"
) + $assets
# Explicit -Arguments: array splatting (@uploadArgs) binds each element as a
# positional parameter of Invoke-GitHub instead of as [string[]]$Arguments.
Invoke-GitHub -Arguments $uploadArgs

Write-Host "[github] Verifying upload..."
Start-Sleep -Seconds 3
$info = Get-ReleaseInfo -Tag $tag
if ($null -eq $info) {
    throw "Release $tag was not found after upload."
}
$assetNames = @($info.assets | ForEach-Object { $_.name })
$expectedFiles = @((Split-Path -Leaf $exeFile), (Split-Path -Leaf $zipFile), (Split-Path -Leaf $shaFile))
foreach ($expectedFile in $expectedFiles) {
    $existsInReleaseDir = (Test-Path -Path (Join-Path $ReleaseDir $expectedFile))
    $existsInInstallerDir = (Test-Path -Path (Join-Path $installerDir $expectedFile))
    if ($existsInReleaseDir -or $existsInInstallerDir) {
        if ($assetNames -notcontains $expectedFile) {
            Write-Warning "[github] Missing expected asset from release: $expectedFile"
        }
    }
}

Write-Host "[github] Publishing release..."
Invoke-GitHub -Arguments @(
    "release", "edit", $tag,
    "--repo", $Repo,
    "--draft=false",
    "--prerelease"
)

$info = Get-ReleaseInfo -Tag $tag
Write-Host "[github] Published: $($info.url)"
Write-Host "[github] Done."
