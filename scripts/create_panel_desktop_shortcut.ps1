param()

$ErrorActionPreference = "Stop"

function Resolve-PanelIconPath {
    param(
        [string]$ProjectRoot
    )

    $candidates = @(
        (Join-Path $ProjectRoot "dist\NisojeStudio\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "build\release\src\platform\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "build\src\platform\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "build\release_pack\src\platform\NisojeStudio.exe")
    )

    $ranked = @()
    for ($index = 0; $index -lt $candidates.Count; $index++) {
        $candidate = $candidates[$index]
        if (-not (Test-Path $candidate)) {
            continue
        }

        $item = Get-Item -LiteralPath $candidate
        $ranked += [pscustomobject]@{
            Path = $item.FullName
            LastWriteTicks = $item.LastWriteTimeUtc.Ticks
            Preference = ($candidates.Count - $index)
        }
    }

    if ($ranked.Count -gt 0) {
        return ($ranked |
            Sort-Object LastWriteTicks, Preference -Descending |
            Select-Object -First 1).Path
    }

    return $null
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
$desktopPath = [Environment]::GetFolderPath("Desktop")
$shortcutPath = Join-Path $desktopPath "Panel Live 3.0.lnk"
$launcherScript = Join-Path $scriptRoot "start_panel_live_hidden.ps1"
$iconPath = Resolve-PanelIconPath -ProjectRoot $projectRoot

if (-not (Test-Path $launcherScript)) {
    throw "No se encontro el launcher oculto en $launcherScript"
}

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$shortcut.Arguments = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$launcherScript`""
$shortcut.WorkingDirectory = $projectRoot
$shortcut.WindowStyle = 7
$shortcut.Description = "Abre Panel Live 3.0 listo para lanzar juegos desde el panel."
if ($iconPath) {
    $shortcut.IconLocation = "$iconPath,0"
}
$shortcut.Save()

Write-Host "[shortcut] created: $shortcutPath"
if ($iconPath) {
    Write-Host "[shortcut] icon:    $iconPath"
}
