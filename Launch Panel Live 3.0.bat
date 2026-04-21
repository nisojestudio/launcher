@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Launcher interno para diagnostico manual de Panel Live 3.0.
rem Detecta un Python utilizable del bridge o del sistema y arranca
rem dentro de tools\bridge_py y arranca el panel con rutas seguras aunque
rem el repo o los juegos tengan espacios en el nombre.

set "PANEL_ROOT=%~dp0"
if "%PANEL_ROOT:~-1%"=="\" set "PANEL_ROOT=%PANEL_ROOT:~0,-1%"

set "TOOLS_ROOT=%PANEL_ROOT%\tools\bridge_py"
set "PACKAGED_PYTHON=%TOOLS_ROOT%\python_runtime\python.exe"
set "VENV_DIR=%TOOLS_ROOT%\.venv"
set "VENV_PYTHON=%VENV_DIR%\Scripts\python.exe"
set "LAUNCHER_PYTHON="
if defined USERPROFILE (
    set "DEFAULT_GAMES_ROOT=%USERPROFILE%\Desktop\Juegos"
) else (
    set "DEFAULT_GAMES_ROOT=%PANEL_ROOT%\Juegos"
)

if not exist "%TOOLS_ROOT%" (
    echo [error] No se encontro tools\bridge_py en:
    echo         %TOOLS_ROOT%
    goto :fail
)

if exist "%PACKAGED_PYTHON%" (
    set "LAUNCHER_PYTHON=%PACKAGED_PYTHON%"
) else if exist "%VENV_PYTHON%" (
    set "LAUNCHER_PYTHON=%VENV_PYTHON%"
) else (
    for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$python = Get-Command python.exe -ErrorAction SilentlyContinue; if ($python -and $python.Source) { $python.Source } else { $py = Get-Command py.exe -ErrorAction SilentlyContinue; if ($py -and $py.Source) { $py.Source } }"`) do (
        set "LAUNCHER_PYTHON=%%~fI"
    )
)

if not defined LAUNCHER_PYTHON (
    echo [error] No se encontro un Python utilizable para el launcher.
    echo.
    echo Opciones revisadas:
    echo   %PACKAGED_PYTHON%
    echo   %VENV_PYTHON%
    echo   python.exe o py.exe en PATH
    goto :fail
)

call :detect_launcher_script
if errorlevel 1 goto :fail

echo [launcher] Python del bridge:
echo            %LAUNCHER_PYTHON%
echo [launcher] Script del panel:
echo            %LAUNCHER_SCRIPT%
echo [launcher] Juegos:
echo            %DEFAULT_GAMES_ROOT%
echo.

"%LAUNCHER_PYTHON%" "%LAUNCHER_SCRIPT%" --wait-until-ready --restart-if-running --games-root "%DEFAULT_GAMES_ROOT%" %*
set "LAUNCH_EXIT=%ERRORLEVEL%"

echo.
if not "%LAUNCH_EXIT%"=="0" (
    echo [error] El launcher termino con exit code %LAUNCH_EXIT%.
    goto :fail_no_pause_check
)

echo [ok] Panel Live 3.0 iniciado correctamente.
if /I not "%NLP3_SKIP_PAUSE%"=="1" pause
exit /b 0

:detect_launcher_script
set "LAUNCHER_SCRIPT="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$root = Join-Path $env:PANEL_ROOT 'tools\bridge_py'; $files = Get-ChildItem -Path $root -Filter *.py -File -Recurse | Where-Object { $_.FullName -notmatch '\\\.venv\\' }; $candidate = $files | Where-Object { $content = Get-Content $_.FullName -Raw; $content -match 'NLP3_PANEL_DESKTOP_LAUNCHER_SIGNATURE' } | Sort-Object FullName | Select-Object -First 1; if ($candidate) { $candidate.FullName }"`) do (
    set "LAUNCHER_SCRIPT=%%~fI"
)

if not defined LAUNCHER_SCRIPT (
    echo [error] No se encontro ningun launcher Python del panel en tools\bridge_py.
    exit /b 1
)
exit /b 0

:fail
if /I not "%NLP3_SKIP_PAUSE%"=="1" pause
exit /b 1

:fail_no_pause_check
if /I not "%NLP3_SKIP_PAUSE%"=="1" pause
exit /b %LAUNCH_EXIT%
