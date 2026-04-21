param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [ValidateSet("none", "code", "full")]
    [string]$BackupMode = "code",

    [string]$OutputRoot = "",

    [switch]$SkipBuild,

    [switch]$SkipTests,

    [switch]$AllowDirty,

    [switch]$DryRun,

    [string]$InnoCompiler = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ProjectRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
}

function Assert-SemVer {
    param([string]$Value)

    if ($Value -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version must use MAJOR.MINOR.PATCH format, for example 0.2.0. Received: $Value"
    }
}

function Invoke-Git {
    param(
        [string]$Root,
        [string[]]$Arguments
    )

    $output = & git -C $Root @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git -C $Root $($Arguments -join ' ')`n$output"
    }

    return ($output | Out-String).Trim()
}

function Assert-CleanWorkingTree {
    param([string]$Root)

    $dirtyStatus = Invoke-Git -Root $Root -Arguments @("status", "--short")
    if (-not [string]::IsNullOrWhiteSpace($dirtyStatus)) {
        throw "Working tree is dirty. Commit or stash changes before release, or pass -AllowDirty for a draft release."
    }
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [string[]]$Arguments = @()
    )

    $display = "$FilePath $($Arguments -join ' ')".Trim()
    if ($DryRun) {
        Write-Host "[dry-run] $display"
        return
    }

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $display"
    }
}

Assert-SemVer -Value $Version
$projectRoot = Resolve-ProjectRoot
$releaseRoot = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    Join-Path $projectRoot ("dist\releases\{0}" -f $Version)
} else {
    [System.IO.Path]::GetFullPath($OutputRoot)
}

Push-Location $projectRoot
try {
    if (-not $AllowDirty) {
        Assert-CleanWorkingTree -Root $projectRoot
    }

    Write-Host "[release] version: $Version"
    Write-Host "[release] output:  $releaseRoot"

    if (-not $DryRun) {
        New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
    }

    $backupStatus = "skipped"
    if ($BackupMode -ne "none") {
        $backupScript = Join-Path $projectRoot "scripts\backup\create_project_backup.ps1"
        Write-Host "[release] backup mode: $BackupMode"
        Invoke-External `
            -FilePath "powershell" `
            -Arguments @(
                "-ExecutionPolicy", "Bypass",
                "-File", $backupScript,
                "-Mode", $BackupMode
            )
        $backupStatus = if ($DryRun) { "unknown" } else { "passed" }
    } else {
        Write-Host "[release] backup skipped"
    }

    $testsStatus = "skipped"
    if (-not $SkipTests) {
        if (-not $SkipBuild) {
            Write-Host "[release] building release preset for validation"
            Invoke-External -FilePath "cmake" -Arguments @("--preset", "release")
            Invoke-External -FilePath "cmake" -Arguments @("--build", "--preset", "release")
        }

        Write-Host "[release] running C++ tests"
        Invoke-External -FilePath "ctest" -Arguments @("--preset", "release", "--output-on-failure")

        Write-Host "[release] running Python bridge tests"
        Invoke-External `
            -FilePath "python" `
            -Arguments @(
                "-m", "unittest",
                "discover",
                "-s", "tools/bridge_py/tests",
                "-t", "tools/bridge_py",
                "-v"
            )
        $testsStatus = if ($DryRun) { "unknown" } else { "passed" }
    } else {
        Write-Host "[release] tests skipped"
    }

    $installerScript = Join-Path $projectRoot "scripts\build_windows_installer.ps1"
    $installerArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", $installerScript,
        "-Version", $Version,
        "-OutputRoot", $releaseRoot,
        "-TestsStatus", $testsStatus,
        "-BackupStatus", $backupStatus
    )

    if ($SkipBuild) {
        $installerArgs += "-SkipBuild"
    }

    if ($AllowDirty) {
        $installerArgs += "-AllowDirtyManifest"
    }

    if (-not [string]::IsNullOrWhiteSpace($InnoCompiler)) {
        $installerArgs += @("-InnoCompiler", $InnoCompiler)
    }

    Write-Host "[release] building versioned installer and manifest"
    Invoke-External -FilePath "powershell" -Arguments $installerArgs

    Write-Host "[release] complete: $releaseRoot"
} finally {
    Pop-Location
}
