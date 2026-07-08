param(
    [int]$LocalPort = 18913,
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
$cloudflared = Join-Path $projectRoot "tools\cloudflared\cloudflared.exe"
$stateDir = Join-Path $env:TEMP "NisojeStudio"
$tunnelUrlFile = Join-Path $stateDir "tunnel_url.txt"
$tunnelPidFile = Join-Path $stateDir "tunnel_pid.txt"
$tunnelLogFile = Join-Path $stateDir "tunnel.log"

if (-not (Test-Path $cloudflared)) {
    Write-Output "[tunnel] cloudflared.exe not found at $cloudflared"
    exit 1
}

# Kill any existing tunnel from a previous run
Get-Process -Name "cloudflared" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $tunnelUrlFile, $tunnelPidFile, $tunnelLogFile

Write-Output "[tunnel] Starting cloudflared tunnel on port $LocalPort ..."

# Lanzar cloudflared en background con stdout+stderr redirigidos al log
$proc = Start-Process -FilePath $cloudflared `
    -ArgumentList "tunnel --url http://localhost:$LocalPort" `
    -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $tunnelLogFile `
    -RedirectStandardError "$tunnelLogFile.err"

$proc.Id | Out-File -FilePath $tunnelPidFile -Encoding utf8

# Wait for the tunnel URL to appear in the log
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$tunnelUrl = ""
$tunnelErrLog = "$tunnelLogFile.err"

while ((Get-Date) -lt $deadline) {
    if ($proc.HasExited) {
        Write-Output "[tunnel] cloudflared exited unexpectedly (code: $($proc.ExitCode))"
        Get-Content $tunnelLogFile, $tunnelErrLog -ErrorAction SilentlyContinue | Select-Object -Last 5 | ForEach-Object { Write-Output $_ }
        exit 1
    }

    $content = ""
    if (Test-Path $tunnelLogFile) { $content += (Get-Content $tunnelLogFile -Raw -ErrorAction SilentlyContinue) }
    if (Test-Path $tunnelErrLog) { $content += (Get-Content $tunnelErrLog -Raw -ErrorAction SilentlyContinue) }
    
    if ($content -match 'https://[a-zA-Z0-9\-]+\.trycloudflare\.com') {
        $tunnelUrl = $Matches[0]
        break
    }

    Start-Sleep -Milliseconds 500
}

if ([string]::IsNullOrWhiteSpace($tunnelUrl)) {
    Write-Output "[tunnel] Timed out waiting for tunnel URL after ${TimeoutSeconds}s"
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Write-Output (Get-Content $tunnelLogFile -ErrorAction SilentlyContinue | Select-Object -Last 5)
    exit 1
}

# Publish the URL for the panel to consume
$tunnelUrl | Out-File -FilePath $tunnelUrlFile -Encoding utf8 -NoNewline
Write-Output "[tunnel] Tunnel ready: $tunnelUrl"
Write-Output "[tunnel] URL written to: $tunnelUrlFile"
Write-Output "[tunnel] PID: $($proc.Id)"
exit 0
