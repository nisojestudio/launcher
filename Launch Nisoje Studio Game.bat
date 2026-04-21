@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Root del repo actual.
set "PANEL_ROOT=%~dp0"
if "%PANEL_ROOT:~-1%"=="\" set "PANEL_ROOT=%PANEL_ROOT:~0,-1%"

set "TOOLS_ROOT=%PANEL_ROOT%\tools\bridge_py"
if defined USERPROFILE (
    set "GAMES_ROOT=%USERPROFILE%\Desktop\Juegos"
) else (
    set "GAMES_ROOT=%PANEL_ROOT%\Juegos"
)
set "VENV_PYTHON=%TOOLS_ROOT%\.venv\Scripts\python.exe"
set "DEFAULT_UI_PORT=18913"

if not exist "%TOOLS_ROOT%" (
    echo [error] No se encontro tools\bridge_py en:
    echo         %TOOLS_ROOT%
    goto :fail
)

rem Usa siempre el Python del virtualenv del bridge.
if not exist "%VENV_PYTHON%" (
    echo [error] No se encontro el virtualenv del bridge en:
    echo         %VENV_PYTHON%
    echo.
    echo Para crearlo:
    echo   cd /d "%TOOLS_ROOT%"
    echo   python -m venv .venv
    echo   .\.venv\Scripts\activate
    echo   pip install -r requirements.txt
    goto :fail
)

if not exist "%GAMES_ROOT%" (
    echo [error] No existe la carpeta de juegos:
    echo         %GAMES_ROOT%
    goto :fail
)

call :detect_launcher_script
if errorlevel 1 goto :fail

call :load_game_catalog
if errorlevel 1 goto :fail

echo.
echo ===========================
echo   Nisoje Studio Launcher
echo ===========================
echo.
echo Python del bridge:
echo   %VENV_PYTHON%
echo.
echo Script detectado:
echo   %LAUNCHER_SCRIPT%
echo.
echo Juegos encontrados en:
echo   %GAMES_ROOT%
echo.

for /L %%N in (1,1,!GAME_COUNT!) do (
    if /I "!GAME_VALID[%%N]!"=="OK" (
        echo   [%%N] !GAME_NAME[%%N]! ^(listo^)
        echo        EXE: !GAME_EXE[%%N]!
    ) else (
        echo   [%%N] !GAME_NAME[%%N]! ^(invalido: !GAME_REASON[%%N]!^)
    )
)

echo.
:prompt_game
set "GAME_CHOICE="
set /p "GAME_CHOICE=Selecciona un juego por numero: "
if not defined GAME_CHOICE (
    echo [error] Debes seleccionar un juego.
    goto :prompt_game
)

echo(%GAME_CHOICE%| findstr /R "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [error] La seleccion debe ser numerica.
    goto :prompt_game
)

if !GAME_CHOICE! LSS 1 (
    echo [error] El numero debe ser mayor a cero.
    goto :prompt_game
)

if !GAME_CHOICE! GTR !GAME_COUNT! (
    echo [error] No existe la opcion !GAME_CHOICE!.
    goto :prompt_game
)

set "SELECTED_GAME_VALID="
set "SELECTED_GAME_REASON="
call set "SELECTED_GAME_VALID=%%GAME_VALID[%GAME_CHOICE%]%%"
call set "SELECTED_GAME_REASON=%%GAME_REASON[%GAME_CHOICE%]%%"

if /I not "%SELECTED_GAME_VALID%"=="OK" (
    echo [error] El juego seleccionado no esta listo:
    echo         %SELECTED_GAME_REASON%
    goto :prompt_game
)

call set "SELECTED_GAME_PATH=%%GAME_PATH[%GAME_CHOICE%]%%"
call set "SELECTED_GAME_NAME=%%GAME_NAME[%GAME_CHOICE%]%%"

echo.
:prompt_port
set "UI_PORT="
set /p "UI_PORT=Puerto UI [%DEFAULT_UI_PORT%]: "
if not defined UI_PORT set "UI_PORT=%DEFAULT_UI_PORT%"

for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$port = 0; if ([int]::TryParse($env:UI_PORT, [ref]$port) -and $port -ge 1 -and $port -le 65535) { $port }"`) do (
    set "UI_PORT=%%I"
)

echo(%UI_PORT%| findstr /R "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [error] El puerto debe estar entre 1 y 65535.
    goto :prompt_port
)

echo.
echo [launcher] juego seleccionado: %SELECTED_GAME_NAME%
echo [launcher] game-root: %SELECTED_GAME_PATH%
echo [launcher] ui-port: %UI_PORT%
echo.

"%VENV_PYTHON%" "%LAUNCHER_SCRIPT%" --ui-port "%UI_PORT%" --game-root "%SELECTED_GAME_PATH%" %*
set "LAUNCH_EXIT=%ERRORLEVEL%"

echo.
if not "%LAUNCH_EXIT%"=="0" (
    echo [error] El launcher termino con exit code %LAUNCH_EXIT%.
) else (
    echo [ok] El launcher termino correctamente.
)

if /I not "%NLP3_SKIP_PAUSE%"=="1" pause
exit /b %LAUNCH_EXIT%

:detect_launcher_script
set "LAUNCHER_SCRIPT="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$root = Join-Path $env:PANEL_ROOT 'tools\bridge_py'; $files = Get-ChildItem -Path $root -Filter *.py -File -Recurse | Where-Object { $_.FullName -notmatch '\\\.venv\\' }; $candidate = $files | Where-Object { $content = Get-Content $_.FullName -Raw; $content -match 'NLP3_PANEL_GAME_LAUNCHER_SIGNATURE' -and $content -match '--ui-port' -and $content -match '--game-root' } | Sort-Object FullName | Select-Object -First 1; if ($candidate) { $candidate.FullName }"`) do (
    set "LAUNCHER_SCRIPT=%%~fI"
)

if not defined LAUNCHER_SCRIPT (
    echo [error] No se encontro ningun script Python de launcher para panel+juego dentro de tools\bridge_py.
    exit /b 1
)
exit /b 0

:load_game_catalog
set "GAME_COUNT=0"
for /f "usebackq tokens=1-6 delims=|" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$gamesRoot = $env:GAMES_ROOT; $index = 0; Get-ChildItem -Path $gamesRoot -Directory | Sort-Object Name | ForEach-Object { $index += 1; $releaseDir = Join-Path $_.FullName 'build\Release'; $exe = Get-ChildItem -Path $releaseDir -Filter *.exe -File -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -First 1; $loader = Join-Path $releaseDir 'WebView2Loader.dll'; $manifest = Join-Path $_.FullName 'module_manifest.json'; $issues = New-Object System.Collections.Generic.List[string]; if (-not (Test-Path $releaseDir)) { [void]$issues.Add('missing build\\Release') }; if (-not $exe) { [void]$issues.Add('missing .exe in build\\Release') }; if (-not (Test-Path $loader)) { [void]$issues.Add('missing WebView2Loader.dll') }; if (-not (Test-Path $manifest)) { [void]$issues.Add('missing module_manifest.json') }; $status = if ($issues.Count -eq 0) { 'OK' } else { 'INVALID' }; $exePath = if ($exe) { $exe.FullName } else { '' }; $reason = if ($issues.Count -eq 0) { 'ready' } else { [string]::Join(', ', $issues) }; Write-Output ($index.ToString() + '|' + $status + '|' + $_.Name + '|' + $_.FullName + '|' + $exePath + '|' + $reason) }"`) do (
    set "GAME_COUNT=%%A"
    set "GAME_VALID[%%A]=%%B"
    set "GAME_NAME[%%A]=%%C"
    set "GAME_PATH[%%A]=%%D"
    set "GAME_EXE[%%A]=%%E"
    set "GAME_REASON[%%A]=%%F"
)

if "%GAME_COUNT%"=="0" (
    echo [error] No se encontraron carpetas de juegos dentro de %GAMES_ROOT%.
    exit /b 1
)
exit /b 0

:fail
if /I not "%NLP3_SKIP_PAUSE%"=="1" pause
exit /b 1
