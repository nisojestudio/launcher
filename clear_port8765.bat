@echo off
rem == clear_port8765.bat ======================================================
rem Limpia SOLO el puerto 8765 (compatibilidad hacia atras).
rem Ahora delega en free_ports.ps1 --port 8765
rem ============================================================================

set "SCRIPT_DIR=%~dp0"
set "FREE_PORTS_PS1=%SCRIPT_DIR%scripts\free_ports.ps1"

if exist "%FREE_PORTS_PS1%" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%FREE_PORTS_PS1%" -Port 8765
    exit /b %ERRORLEVEL%
)

echo [clear_port8765] ERROR: No se encontro scripts\free_ports.ps1 en %SCRIPT_DIR%
