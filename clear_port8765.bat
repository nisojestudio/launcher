@echo off
rem Compat: este archivo batia solo el puerto 8765.
rem Delegamos a clear_ports.bat para barrer 8765 + 8766 + 8770.
call "%~dp0clear_ports.bat"
exit /b %ERRORLEVEL%
