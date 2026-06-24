param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$Changelog = "",

    [string]$SitioRoot = "",

    [string]$LauncherRepo = "nisojestudio/launcher",

    [switch]$SkipBuild,

    [switch]$SkipGitHubRelease,

    [switch]$SkipSitioUpdate,

    [switch]$DryRun
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

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    if ($DryRun) {
        Write-Host "[dry-run] $FilePath $($Arguments -join ' ')"
        return
    }

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $FilePath $($Arguments -join ' ')"
    }
}

function Update-InstallerUrlInFile {
    param(
        [string]$FilePath,
        [string]$NewVersion
    )

    if (-not (Test-Path $FilePath)) {
        Write-Warning "[deploy] File not found, skipping: $FilePath"
        return $false
    }

    $oldContent = Get-Content $FilePath -Raw
    $urlRegex = [regex]'https://github\.com/nisojestudio/launcher/releases/download/v[\d.]+/panel-live-[\d.]+-win-x64\.exe'
    $newUrl = "https://github.com/nisojestudio/launcher/releases/download/v$NewVersion/panel-live-$NewVersion-win-x64.exe"

    $newContent = $urlRegex.Replace($oldContent, $newUrl)

    if ($newContent -ne $oldContent) {
        if ($DryRun) {
            Write-Host "[deploy] Would update: $FilePath"
            Write-Host "[deploy]   new URL: $newUrl"
            return $true
        }
        Set-Content -LiteralPath $FilePath -Value $newContent -NoNewline
        Write-Host "[deploy] Updated: $FilePath"
        return $true
    }

    Write-Host "[deploy] URL already up-to-date: $FilePath"
    return $false
}

# --- validate ---
Assert-SemVer -Value $Version
$projectRoot = Resolve-ProjectRoot
$semVerRegex = '^\d+\.\d+\.\d+$'

if ($Version -notmatch $semVerRegex) {
    throw "Version must use MAJOR.MINOR.PATCH format. Received: $Version"
}

# resolve sitio root
if ([string]::IsNullOrWhiteSpace($SitioRoot)) {
    $SitioRoot = Join-Path $projectRoot "..\panel-live-master\sitio"
}
$SitioRoot = [System.IO.Path]::GetFullPath($SitioRoot)

if (-not (Test-Path $SitioRoot)) {
    throw "Sitio root not found: $SitioRoot.`nExpected at: $projectRoot\..\panel-live-master\sitio`nPass -SitioRoot to override."
}

Write-Host "================================"
Write-Host " DEPLOY LAUNCHER v$Version"
Write-Host "================================"
Write-Host ""

# === STEP 1: Prepare release (build + tests + backup + installer + manifest) ===
Write-Host "[1/6] Preparing release (build, tests, installer, backup)..."
$prepareScript = Join-Path $projectRoot "scripts\release\prepare_release.ps1"
Invoke-Checked -FilePath "powershell" -Arguments @(
    "-ExecutionPolicy", "Bypass",
    "-File", $prepareScript,
    "-Version", $Version,
    "-BackupMode", "code"
)

# === STEP 2: GitHub Release ===
Write-Host "[2/6] Publishing GitHub Release..."
$githubScript = Join-Path $projectRoot "scripts\release\github_release.ps1"
$releaseDir = Join-Path $projectRoot "dist\releases\$Version"
Invoke-Checked -FilePath "powershell" -Arguments @(
    "-ExecutionPolicy", "Bypass",
    "-File", $githubScript,
    "-Version", $Version,
    "-Changelog", $Changelog,
    "-Repo", $LauncherRepo,
    "-ReleaseDir", $releaseDir
)

# === STEP 3: Update INSTALLER_URL in sitio files ===
Write-Host "[3/6] Updating INSTALLER_URL in sitio files..."

$filesUpdated = 0
$envFile = Join-Path $SitioRoot ".env"
$ciFile = Join-Path $SitioRoot ".github\workflows\deploy-pages.yml"
$workerCfg = Join-Path $SitioRoot "wrangler.api.jsonc"

if (Update-InstallerUrlInFile -FilePath $workerCfg -NewVersion $Version) { $filesUpdated++ }
if (Update-InstallerUrlInFile -FilePath $envFile -NewVersion $Version) { $filesUpdated++ }
if (Update-InstallerUrlInFile -FilePath $ciFile -NewVersion $Version) { $filesUpdated++ }

Write-Host "[deploy] Files updated: $filesUpdated"

# === STEP 4: Commit + push sitio changes ===
Write-Host "[4/6] Committing and pushing sitio changes..."
if ($DryRun) {
    Write-Host "[dry-run] Would commit and push in: $SitioRoot"
} else {
    Push-Location $SitioRoot
    try {
        git add .github/workflows/deploy-pages.yml wrangler.api.jsonc
        $status = git status --short
        if (-not [string]::IsNullOrWhiteSpace($status)) {
            git commit -m "Update INSTALLER_URL to v$Version"
            git push origin main
            Write-Host "[deploy] Commit pushed"
        } else {
            Write-Host "[deploy] No changes to commit in sitio"
        }
    } finally {
        Pop-Location
    }
}

# === STEP 5: Deploy Worker to Cloudflare ===
Write-Host "[5/6] Deploying Cloudflare Worker..."
Push-Location $SitioRoot
try {
    if ($DryRun) {
        Write-Host "[dry-run] Would run: npx wrangler deploy --config wrangler.api.jsonc"
    } else {
        npx wrangler deploy --config wrangler.api.jsonc
    }
} finally {
    Pop-Location
}

# === STEP 6: Verify ===
Write-Host "[6/6] Verifying production endpoint..."
if ($DryRun) {
    Write-Host "[dry-run] Would verify: https://nisoje.com/api/version/latest"
} else {
    Write-Host "[deploy] Waiting for propagation (10s)..."
    Start-Sleep -Seconds 10
    try {
        $response = Invoke-RestMethod -Uri "https://nisoje.com/api/version/latest"
        Write-Host "[verify] Response:"
        Write-Host "  latest_version: $($response.latest_version)"
        Write-Host "  installer_url:  $($response.installer_url)"
        if ($response.latest_version -eq "v$Version") {
            Write-Host "[verify] OK - version matches"
        } else {
            Write-Warning "[verify] Version mismatch: expected v$Version, got $($response.latest_version)"
        }
    } catch {
        Write-Warning "[verify] Failed to reach endpoint: $_"
    }
}

Write-Host ""
Write-Host "================================"
Write-Host " DEPLOY COMPLETE: v$Version"
Write-Host "================================"
