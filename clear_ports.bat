@echo off
setlocal EnableDelayedExpansion

rem == clear_ports.bat =========================================================
rem Libera todos los puertos del sistema Nisoje LivePanel 3.0:
rem   - UI:  18913
rem   - Bridge: 8765, 8766, 8770
rem   - Tuneles cloudflared zombies
rem   - Procesos NisojeStudio / nlp3_app huerfanos
rem ============================================================================

set "SCRIPT_DIR=%~dp0"
set "FREE_PORTS_PS1=%SCRIPT_DIR%scripts\free_ports.ps1"

if exist "%FREE_PORTS_PS1%" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%FREE_PORTS_PS1%"
    exit /b %ERRORLEVEL%
)

echo [clear_ports] ERROR: No se encontro scripts\free_ports.ps1 en %SCRIPT_DIR%

endlocal
