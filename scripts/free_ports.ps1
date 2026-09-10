#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Libera todos los puertos usados por Nisoje LivePanel 3.0.
.DESCRIPTION
    Busca y mata procesos que ocupen los puertos del panel y del bridge:
      - UI:      18913 (NisojeStudio.exe)
      - Bridge:  8765 (WebSocket externo TikTok)
      - Bridge:  8766 (WebSocket broadcast)
      - Bridge:  8770 (HTTP interno)
    Ademas mata tuneles cloudflared zombies y procesos NisojeStudio huerfanos.
.PARAMETER IncludeCloudflared
    Tambien mata procesos cloudflared.exe (por defecto: $true).
.PARAMETER Force
    Mata sin preguntar (por defecto: $true en no interactivo).
.PARAMETER Port
    Liberar solo un puerto especifico (opcional).
.EXAMPLE
    ./scripts/free_ports.ps1
    Libera todos los puertos automaticamente.
.EXAMPLE
    ./scripts/free_ports.ps1 -Port 18913
    Libera solo el puerto 18913.
#>

param(
    [switch]$IncludeCloudflared = $true,
    [switch]$Force = $true,
    [int]$Port = 0
)

$ErrorActionPreference = "Continue"

# -- Puertos del sistema --
$UI_PORTS = @(18913)
$BRIDGE_PORTS = @(8765, 8766, 8770)
$ALL_PORTS = $UI_PORTS + $BRIDGE_PORTS

if ($Port -gt 0) {
    $ALL_PORTS = @($Port)
}

# -- Funcion: buscar PIDs por puerto --
function Get-ProcessIdsOnPort {
    param([int]$PortNumber)
    $results = @()
    try {
        $lines = netstat -ano | Select-String ":$PortNumber\s"
        foreach ($line in $lines) {
            $parts = ($line -split '\s+') | Where-Object { $_ -ne '' }
            if ($parts.Count -ge 5) {
                $pid = $parts[-1] -as [int]
                if ($pid -and $pid -gt 0) {
                    $results += $pid
                }
            }
        }
    } catch {
        # netstat fallo, ignorar
    }
    return ($results | Select-Object -Unique)
}

# -- Funcion: matar proceso con informacion --
function Remove-ProcessById {
    param([int]$ProcessId, [string]$Label)
    try {
        $proc = Get-Process -Id $ProcessId -ErrorAction Stop
        $name = $proc.ProcessName
        $pid = $proc.Id
        $cmd = ""
        try {
            $cmd = ($proc.CommandLine -replace '\s+', ' ').Substring(0, [Math]::Min(120, $proc.CommandLine.Length))
        } catch { }
        Write-Host "  [kill] PID $pid ($name) - $Label" -ForegroundColor Yellow
        if ($cmd) {
            Write-Host "         $cmd" -ForegroundColor DarkGray
        }
        Stop-Process -Id $ProcessId -Force -ErrorAction Stop
        Write-Host "         -> terminado" -ForegroundColor Green
        return $true
    } catch {
        Write-Host "  [kill] PID $ProcessId - $Label -> no se pudo ($($_.Exception.Message))" -ForegroundColor DarkRed
        return $false
    }
}

# -- Funcion: verificar estado final de un puerto --
function Test-PortIsFree {
    param([int]$PortNumber)
    try {
        $listening = netstat -ano | Select-String ":$PortNumber\s" | Select-String "LISTENING"
        if ($listening) {
            return $false
        }
    } catch { }
    return $true
}

# -- Encabezado --
Write-Host ""
Write-Host "===== Liberador de Puertos - Nisoje LivePanel =====" -ForegroundColor Cyan
Write-Host ""

# -- Fase 1: Matar cloudflared --
if ($IncludeCloudflared -and ($Port -eq 0)) {
    Write-Host "--- Fase 1: Tuneles Cloudflared ---" -ForegroundColor Magenta
    $cfProcs = Get-Process -Name "cloudflared" -ErrorAction SilentlyContinue
    if ($cfProcs) {
        foreach ($p in $cfProcs) {
            Remove-ProcessById -ProcessId $p.Id -Label "cloudflared (tunel)"
        }
    } else {
        Write-Host "  No hay procesos cloudflared corriendo." -ForegroundColor DarkGray
    }
    Write-Host ""
}

# -- Fase 2: Matar NisojeStudio / nlp3_app zombies --
if ($Port -eq 0) {
    Write-Host "--- Fase 2: Procesos del Panel ---" -ForegroundColor Magenta
    $panelProcs = @()
    try {
        $panelProcs += Get-Process -Name "NisojeStudio" -ErrorAction SilentlyContinue
    } catch { }
    try {
        $panelProcs += Get-Process -Name "nlp3_app" -ErrorAction SilentlyContinue
    } catch { }

    if ($panelProcs) {
        foreach ($p in $panelProcs) {
            Remove-ProcessById -ProcessId $p.Id -Label "NisojeStudio / nlp3_app"
        }
    } else {
        Write-Host "  No hay procesos del panel corriendo." -ForegroundColor DarkGray
    }
    Write-Host ""
}

# -- Fase 3: Liberar puertos especificos --
Write-Host "--- Fase 3: Puertos ---" -ForegroundColor Magenta

$totalKilled = 0
$portStatus = @{}

foreach ($p in $ALL_PORTS) {
    $label = switch ($p) {
        18913 { "UI del Panel" }
        8765  { "Bridge WS externo (TikTok)" }
        8766  { "Bridge WS broadcast" }
        8770  { "Bridge HTTP interno" }
        default { "puerto $p" }
    }

    $pids = Get-ProcessIdsOnPort -PortNumber $p
    if ($pids.Count -eq 0) {
        Write-Host "  [$p] $label -> libre" -ForegroundColor DarkGray
        $portStatus[$p] = "libre"
        continue
    }

    Write-Host "  [$p] $label -> ocupado por PID(s): $($pids -join ', ')" -ForegroundColor Yellow
    $killedAny = $false
    foreach ($pid in $pids) {
        $ok = Remove-ProcessById -ProcessId $pid -Label "Puerto $p ($label)"
        if ($ok) { $killedAny = $true; $totalKilled++ }
    }
    Start-Sleep -Milliseconds 200
    if (Test-PortIsFree -PortNumber $p) {
        Write-Host "         -> puerto $p liberado" -ForegroundColor Green
        $portStatus[$p] = "libre"
    } else {
        # Re-intentar con mas fuerza
        try {
            $remaining = Get-ProcessIdsOnPort -PortNumber $p
            if ($remaining.Count -gt 0) {
                foreach ($pid in $remaining) {
                    Remove-ProcessById -ProcessId $pid -Label "Puerto $p (reintento)"
                }
                Start-Sleep -Milliseconds 300
            }
        } catch { }
        if (Test-PortIsFree -PortNumber $p) {
            Write-Host "         -> puerto $p liberado (reintento exitoso)" -ForegroundColor Green
            $portStatus[$p] = "libre"
        } else {
            Write-Host "         -> ATENCION: puerto $p sigue ocupado" -ForegroundColor Red
            $portStatus[$p] = "OCUPADO"
        }
    }
}

Write-Host ""

# -- Resumen --
Write-Host "--- Resumen Final ---" -ForegroundColor Cyan
$allFree = $true
foreach ($p in $ALL_PORTS) {
    $label = switch ($p) {
        18913 { "UI del Panel" }
        8765  { "Bridge WS externo (TikTok)" }
        8766  { "Bridge WS broadcast" }
        8770  { "Bridge HTTP interno" }
        default { "puerto $p" }
    }
    if ($portStatus[$p] -eq "libre") {
        Write-Host "  [OK] Puerto $p ($label): $($portStatus[$p])" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] Puerto $p ($label): $($portStatus[$p])" -ForegroundColor Red
        $allFree = $false
    }
}

if ($totalKilled -gt 0) {
    Write-Host ""
    Write-Host "  Se terminaron $totalKilled proceso(s) en total." -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "  No fue necesario terminar ningun proceso." -ForegroundColor DarkGray
}

Write-Host ""

if ($allFree) {
    Write-Host "Todos los puertos estan libres. Ya puedes iniciar el panel." -ForegroundColor Green
    Write-Host "  > .\scripts\start_panel_live.ps1" -ForegroundColor Cyan
    Write-Host "  > .\Launch Panel Live 3.0.bat" -ForegroundColor Cyan
} else {
    Write-Host "Algunos puertos siguen ocupados. Probablemente los tiene un proceso SYSTEM." -ForegroundColor Red
    Write-Host "Para identificar el proceso dueno, ejecuta:" -ForegroundColor Yellow
    Write-Host "  > netstat -ano | findstr :<PUERTO>" -ForegroundColor Gray
    Write-Host "  > tasklist /FI ""PID eq <PID>""" -ForegroundColor Gray
}

Write-Host ""
