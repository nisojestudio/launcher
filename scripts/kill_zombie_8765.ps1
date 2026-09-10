Write-Host "=== Zombie Killer for Port 8765 ==="
$connections = Get-NetTCPConnection -LocalPort 8765 -ErrorAction SilentlyContinue
foreach ($c in $connections) {
    $procId = $c.OwningProcess
    $proc = Get-Process -Id $procId -ErrorAction SilentlyContinue
    if ($proc) {
        Write-Host "Killing PID $procId ($($proc.ProcessName))"
        Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host "PID $procId is a zombie (process does not exist)"
    }
}

Start-Sleep -Seconds 3
$remaining = Get-NetTCPConnection -LocalPort 8765 -ErrorAction SilentlyContinue
if ($remaining) {
    Write-Host "Port 8765 still in use after cleanup"
    $remaining | Format-Table LocalAddress,LocalPort,State,OwningProcess -AutoSize
} else {
    Write-Host "Port 8765 is now FREE"
}
