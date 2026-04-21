param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string[]]$ArtifactPaths = @(),

    [string]$ProjectRoot = "",

    [string]$OutputDir = "",

    [switch]$AllowDirty
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ProjectRoot {
    param([string]$RequestedRoot)

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return (Resolve-Path -LiteralPath $RequestedRoot).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
}

function Invoke-Git {
    param(
        [string]$Root,
        [string[]]$Arguments
    )

    $output = & git -C $Root @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        return ""
    }
    return ($output | Out-String).Trim()
}

function Resolve-ArtifactKind {
    param([string]$PathValue)

    $name = [System.IO.Path]::GetFileName($PathValue).ToLowerInvariant()
    if ($name.EndsWith(".exe")) { return "installer" }
    if ($name.EndsWith(".zip")) { return "portable_zip" }
    if ($name -eq "sha256sums.txt") { return "checksum" }
    if ($name.EndsWith(".json")) { return "manifest" }
    if ($name.EndsWith(".md")) { return "release_notes" }
    return "artifact"
}

function Resolve-Platform {
    param([string]$PathValue)

    $name = [System.IO.Path]::GetFileName($PathValue).ToLowerInvariant()
    if ($name -match "win|windows") { return "windows" }
    if ($name -match "linux|appimage") { return "linux" }
    if ($name -match "macos|darwin|osx") { return "macos" }
    return "unknown"
}

function Resolve-Architecture {
    param([string]$PathValue)

    $name = [System.IO.Path]::GetFileName($PathValue).ToLowerInvariant()
    if ($name -match "x64|amd64") { return "x64" }
    if ($name -match "arm64|aarch64") { return "arm64" }
    return "unknown"
}

$resolvedProjectRoot = Resolve-ProjectRoot -RequestedRoot $ProjectRoot
$commit = Invoke-Git -Root $resolvedProjectRoot -Arguments @("rev-parse", "--short", "HEAD")
$branch = Invoke-Git -Root $resolvedProjectRoot -Arguments @("branch", "--show-current")
$dirtyStatus = Invoke-Git -Root $resolvedProjectRoot -Arguments @("status", "--short")
$isDirty = -not [string]::IsNullOrWhiteSpace($dirtyStatus)

if ($isDirty -and -not $AllowDirty) {
    throw "Working tree is dirty. Commit or stash changes, or pass -AllowDirty for draft manifests."
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $resolvedProjectRoot ("dist\releases\{0}" -f $Version)
}
$resolvedOutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$artifactEntries = @()
foreach ($artifactPath in $ArtifactPaths) {
    if ([string]::IsNullOrWhiteSpace($artifactPath)) {
        continue
    }

    $resolvedArtifactPath = if ([System.IO.Path]::IsPathRooted($artifactPath)) {
        [System.IO.Path]::GetFullPath($artifactPath)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $resolvedProjectRoot $artifactPath))
    }

    if (-not (Test-Path -LiteralPath $resolvedArtifactPath)) {
        throw "Artifact not found: $resolvedArtifactPath"
    }

    $item = Get-Item -LiteralPath $resolvedArtifactPath
    $artifactEntries += [ordered]@{
        path = $resolvedArtifactPath
        fileName = $item.Name
        platform = Resolve-Platform -PathValue $resolvedArtifactPath
        architecture = Resolve-Architecture -PathValue $resolvedArtifactPath
        kind = Resolve-ArtifactKind -PathValue $resolvedArtifactPath
        sizeBytes = [int64]$item.Length
        sha256 = (Get-FileHash -LiteralPath $resolvedArtifactPath -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}

$manifest = [ordered]@{
    product = "Panel Live"
    version = $Version
    generatedAt = (Get-Date).ToString("o")
    git = [ordered]@{
        commit = $commit
        branch = $branch
        dirty = $isDirty
    }
    artifacts = $artifactEntries
    releaseNotes = "CHANGELOG.md"
    validation = [ordered]@{
        build = "pending"
        tests = "pending"
        installer = "pending"
        backup = "pending"
    }
    knownRisks = @()
}

$manifestPath = Join-Path $resolvedOutputDir ("release-manifest-{0}.json" -f $Version)
$utf8 = New-Object System.Text.UTF8Encoding($false)
$json = $manifest | ConvertTo-Json -Depth 10
[System.IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine, $utf8)

Write-Host "[release] manifest: $manifestPath"
Write-Host "[release] artifacts: $($artifactEntries.Count)"
