@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%start_local_game_bridge.ps1" %*
exit /b %ERRORLEVEL%
