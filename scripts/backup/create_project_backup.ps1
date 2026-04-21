param(
    [ValidateSet("code", "full")]
    [string]$Mode = "code",

    [string]$ProjectRoot = "",

    [string]$DestinationRoot = "",

    [switch]$Zip
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

function Resolve-DestinationRoot {
    param([string]$RequestedRoot)

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return [System.IO.Path]::GetFullPath($RequestedRoot)
    }

    return [System.IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath("Desktop")) "PanelLiveBackups"))
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

function Get-TreeStats {
    param([string]$Root)

    $files = Get-ChildItem -LiteralPath $Root -Recurse -File -Force -ErrorAction SilentlyContinue
    $count = 0
    [int64]$bytes = 0
    foreach ($file in $files) {
        $count += 1
        $bytes += [int64]$file.Length
    }

    return [pscustomobject]@{
        FileCount = $count
        ByteCount = $bytes
    }
}

function Write-BackupManifest {
    param(
        [string]$ManifestPath,
        [object]$Payload
    )

    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $json = $Payload | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($ManifestPath, $json + [Environment]::NewLine, $utf8)
}

$resolvedProjectRoot = Resolve-ProjectRoot -RequestedRoot $ProjectRoot
$resolvedDestinationRoot = Resolve-DestinationRoot -RequestedRoot $DestinationRoot
New-Item -ItemType Directory -Path $resolvedDestinationRoot -Force | Out-Null

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$projectName = Split-Path -Leaf $resolvedProjectRoot
$backupName = "{0}-{1}-{2}" -f $projectName, $Mode, $timestamp
$backupPath = Join-Path $resolvedDestinationRoot $backupName

if (Test-Path -LiteralPath $backupPath) {
    throw "Backup destination already exists: $backupPath"
}

$commit = Invoke-Git -Root $resolvedProjectRoot -Arguments @("rev-parse", "--short", "HEAD")
$branch = Invoke-Git -Root $resolvedProjectRoot -Arguments @("branch", "--show-current")
$dirtyStatus = Invoke-Git -Root $resolvedProjectRoot -Arguments @("status", "--short")
$isDirty = -not [string]::IsNullOrWhiteSpace($dirtyStatus)

if ($Mode -eq "code") {
    git clone --local $resolvedProjectRoot $backupPath
    if ($LASTEXITCODE -ne 0) {
        throw "git clone backup failed."
    }
} else {
    New-Item -ItemType Directory -Path $backupPath -Force | Out-Null
    $robocopyOutput = & robocopy $resolvedProjectRoot $backupPath /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /XD ".codex_tmp" "ctest_temp" /XF "*.tmp"
    $robocopyCode = $LASTEXITCODE
    if ($robocopyCode -ge 8) {
        throw "robocopy backup failed with exit code $robocopyCode.`n$($robocopyOutput | Out-String)"
    }
}

$stats = Get-TreeStats -Root $backupPath
$manifest = [ordered]@{
    product = "Panel Live"
    generatedAt = (Get-Date).ToString("o")
    mode = $Mode
    sourcePath = $resolvedProjectRoot
    backupPath = $backupPath
    git = [ordered]@{
        commit = $commit
        branch = $branch
        dirty = $isDirty
    }
    fileCount = $stats.FileCount
    byteCount = $stats.ByteCount
    exclusions = @(".codex_tmp", "ctest_temp", "*.tmp")
    zip = $null
}

if ($Zip) {
    $zipPath = $backupPath + ".zip"
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -LiteralPath $backupPath -DestinationPath $zipPath
    $zipItem = Get-Item -LiteralPath $zipPath
    $manifest.zip = [ordered]@{
        path = $zipPath
        sizeBytes = [int64]$zipItem.Length
        sha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}

$manifestPath = Join-Path $backupPath "backup-manifest.json"
Write-BackupManifest -ManifestPath $manifestPath -Payload $manifest

Write-Host "[backup] mode: $Mode"
Write-Host "[backup] path: $backupPath"
Write-Host "[backup] manifest: $manifestPath"
if ($Zip -and $manifest.zip) {
    Write-Host "[backup] zip: $($manifest.zip.path)"
}
