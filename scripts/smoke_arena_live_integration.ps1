param(
    [string]$PanelExe = "",
    [string]$GameRoot = "",
    [uint16]$UiPort = 18921,
    [int]$StartupTimeoutSeconds = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ProjectRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Resolve-PanelExecutable {
    param(
        [string]$ProjectRoot,
        [string]$RequestedExe
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedExe)) {
        if (-not (Test-Path $RequestedExe)) {
            throw "Panel executable not found at $RequestedExe"
        }
        return (Resolve-Path $RequestedExe).Path
    }

    $candidates = @(
        (Join-Path $ProjectRoot "build\release\src\platform\NisojeStudio.exe"),
        (Join-Path $ProjectRoot "dist\NisojeStudio\NisojeStudio.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "No panel executable found. Build Release or generate the portable first."
}

function Resolve-ArenaLiveRoot {
    param([string]$RequestedRoot)

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        if (-not (Test-Path $RequestedRoot)) {
            throw "Game root not found at $RequestedRoot"
        }
        return (Resolve-Path $RequestedRoot).Path
    }

    $desktop = [Environment]::GetFolderPath([Environment+SpecialFolder]::Desktop)
    if (-not [string]::IsNullOrWhiteSpace($desktop)) {
        $defaultRoot = Join-Path $desktop "Juegos\Arena Live"
    } elseif (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $defaultRoot = Join-Path $env:USERPROFILE "Desktop\Juegos\Arena Live"
    } else {
        $defaultRoot = "Juegos\Arena Live"
    }

    if (-not (Test-Path $defaultRoot)) {
        throw "Arena Live not found at $defaultRoot"
    }

    return (Resolve-Path $defaultRoot).Path
}

function Wait-Until {
    param(
        [scriptblock]$Condition,
        [int]$TimeoutSeconds,
        [int]$IntervalMilliseconds = 250
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (& $Condition) {
            return $true
        }
        Start-Sleep -Milliseconds $IntervalMilliseconds
    }

    return (& $Condition)
}

function Invoke-PanelJson {
    param(
        [string]$Method,
        [string]$Url,
        [object]$Body = $null
    )

    $invokeArgs = @{
        Method = $Method
        Uri = $Url
        ContentType = "application/json; charset=utf-8"
    }

    if ($null -ne $Body) {
        $invokeArgs.Body = ($Body | ConvertTo-Json -Depth 12)
    }

    return Invoke-RestMethod @invokeArgs
}

function Stop-StaleArenaProcesses {
    param([string]$ResolvedGameRoot)

    try {
        $targets = Get-CimInstance Win32_Process | Where-Object {
            ($_.Name -ieq "ArenaLive.exe" -and $_.CommandLine -like "*$ResolvedGameRoot*") -or
            ($_.Name -match "^python(\\.exe)?$" -and $_.CommandLine -like "*run_local_game_bridge.py*" -and $_.CommandLine -like "*$ResolvedGameRoot*")
        }
        foreach ($target in $targets) {
            Stop-Process -Id $target.ProcessId -Force -ErrorAction SilentlyContinue
        }
    } catch {
        # Best effort only.
    }
}

function Clear-SmokeArtifact {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return
    }

    try {
        Remove-Item $Path -Force -ErrorAction Stop
        return
    } catch {
        try {
            Clear-Content $Path -Force -ErrorAction Stop
        } catch {
            throw "Could not clear smoke artifact: $Path"
        }
    }
}

$projectRoot = Resolve-ProjectRoot
$resolvedPanelExe = Resolve-PanelExecutable -ProjectRoot $projectRoot -RequestedExe $PanelExe
$resolvedGameRoot = Resolve-ArenaLiveRoot -RequestedRoot $GameRoot

$env:NLP3_LOCAL_GAMES_ROOT = (Split-Path -Parent $resolvedGameRoot)
$stateFile = Join-Path $resolvedGameRoot "runtime\panel_bridge\state.json"
$bridgeInbox = Join-Path $resolvedGameRoot "runtime\panel_bridge\inbox\panel_events.jsonl"
$bridgeLog = Join-Path $resolvedGameRoot "runtime\panel_bridge\bridge.log.jsonl"
$gameStatus = Join-Path $resolvedGameRoot "runtime\status.json"
$gameInbox = Join-Path $resolvedGameRoot "runtime\inbox\events.jsonl"
$hostLog = Join-Path $resolvedGameRoot "runtime\host.log.jsonl"
$expectedEventCount = 7

Stop-StaleArenaProcesses -ResolvedGameRoot $resolvedGameRoot
Start-Sleep -Milliseconds 350

foreach ($path in @($stateFile, $bridgeInbox, $bridgeLog, $gameStatus, $gameInbox, $hostLog)) {
    Clear-SmokeArtifact -Path $path
}

$arguments = @("--ui", "--no-browser", "--ui-port", $UiPort)
$process = Start-Process -FilePath $resolvedPanelExe -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $resolvedPanelExe) -PassThru

try {
    $healthUrl = "http://127.0.0.1:$UiPort/health"
    $stateUrl = "http://127.0.0.1:$UiPort/api/state"
    $gameStartUrl = "http://127.0.0.1:$UiPort/api/game/start"
    $gameTriggerUrl = "http://127.0.0.1:$UiPort/api/game/trigger"

    if (-not (Wait-Until -TimeoutSeconds $StartupTimeoutSeconds -Condition {
        try {
            $health = Invoke-RestMethod -Uri $healthUrl -Method Get
            return $health.ok -eq $true
        } catch {
            return $false
        }
    })) {
        throw "Panel health endpoint did not become ready."
    }

    $initialState = Invoke-RestMethod -Uri $stateUrl -Method Get
    if (-not ($initialState.catalog.items | Where-Object { $_.gameId -eq "arena_live" })) {
        throw "Arena Live was not discovered from $resolvedGameRoot"
    }

    $startResult = Invoke-PanelJson -Method Post -Url $gameStartUrl -Body @{ gameId = "arena_live" }
    if (-not $startResult.ok) {
        throw "Panel could not activate Arena Live: $($startResult.message)"
    }

    if (-not (Wait-Until -TimeoutSeconds $StartupTimeoutSeconds -Condition {
        try {
            $state = Invoke-RestMethod -Uri $stateUrl -Method Get
            return $state.snapshot.externalGame.active `
                -and $state.snapshot.externalGame.bridgeRunning `
                -and (Test-Path $stateFile)
        } catch {
            return $false
        }
    })) {
        throw "Arena Live bridge did not become active."
    }

    $sampleAvatar = "https://example.com/nisoje-smoke-avatar.png"
    $events = @(
        @{
            kind = "join"
            actorId = "smoke-join"
            actorName = "Smoke Join"
            avatarUrl = $sampleAvatar
            message = "viewer_join"
        },
        @{
            kind = "chat"
            actorId = "smoke-chat"
            actorName = "Smoke Chat"
            avatarUrl = $sampleAvatar
            message = "hola arena"
        },
        @{
            kind = "follow"
            actorId = "smoke-follow"
            actorName = "Smoke Follow"
            avatarUrl = $sampleAvatar
            magnitude = 1
        },
        @{
            kind = "share"
            actorId = "smoke-share"
            actorName = "Smoke Share"
            avatarUrl = $sampleAvatar
            magnitude = 1
        },
        @{
            kind = "like"
            actorId = "smoke-like"
            actorName = "Smoke Like"
            avatarUrl = $sampleAvatar
            magnitude = 5
        },
        @{
            kind = "avatar"
            actorId = "smoke-avatar"
            actorName = "Smoke Avatar"
            avatarUrl = $sampleAvatar
            message = "avatar update"
        },
        @{
            kind = "gift"
            actorId = "smoke-gift"
            actorName = "Smoke Gift"
            avatarUrl = $sampleAvatar
            giftName = "Rose"
            quantity = 3
            value = 30
        }
    )

    foreach ($event in $events) {
        $triggerResult = Invoke-PanelJson -Method Post -Url $gameTriggerUrl -Body $event
        if (-not $triggerResult.ok) {
            throw "Panel could not inject $($event.kind): $($triggerResult.message)"
        }
    }

    if (-not (Wait-Until -TimeoutSeconds $StartupTimeoutSeconds -Condition {
        if (-not (Test-Path $stateFile)) {
            return $false
        }

        try {
            $bridgeState = Get-Content $stateFile -Raw | ConvertFrom-Json
            $gameInboxLines = if (Test-Path $gameInbox) { @(Get-Content $gameInbox) } else { @() }
            $bridgeInboxLines = if (Test-Path $bridgeInbox) { @(Get-Content $bridgeInbox) } else { @() }
            $bridgeLogs = if (Test-Path $bridgeLog) { @(Get-Content $bridgeLog) } else { @() }
            $hasForwardedLog = @($bridgeLogs | Where-Object { $_ -match '"message":"forwarded_event"' }).Count -ge $expectedEventCount
            return $bridgeState.runtime.lastStatusType `
                -and $bridgeState.process.running `
                -and $bridgeInboxLines.Count -ge $expectedEventCount `
                -and $gameInboxLines.Count -ge $expectedEventCount `
                -and $hasForwardedLog
        } catch {
            return $false
        }
    })) {
        throw "Arena Live did not publish runtime bridge state after receiving events."
    }

    $finalState = Invoke-RestMethod -Uri $stateUrl -Method Get
    $bridgeState = Get-Content $stateFile -Raw | ConvertFrom-Json

    Write-Host "[smoke] panel health ok"
    Write-Host "[smoke] active game: $($finalState.snapshot.externalGame.gameId)"
    Write-Host "[smoke] bridge running: $($finalState.snapshot.externalGame.bridgeRunning)"
    Write-Host "[smoke] runner state file: $stateFile"
    Write-Host "[smoke] bridge python: $($finalState.snapshot.externalGame.recentLogs | Select-Object -First 1)"
    Write-Host "[smoke] runtime status type: $($bridgeState.runtime.lastStatusType)"
    Write-Host "[smoke] bridge inbox lines: $(@(Get-Content $bridgeInbox).Count)"
    Write-Host "[smoke] game inbox lines: $(@(Get-Content $gameInbox).Count)"
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
}
