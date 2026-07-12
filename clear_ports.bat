@echo off
setlocal EnableDelayedExpansion

set "BRIDGE_PORTS=8765 8766 8770"

echo [clear_ports] Buscando PIDs ocupando puertos del bridge TikTok: %BRIDGE_PORTS%

set "FOUND_ANY=0"
for %%P in (%BRIDGE_PORTS%) do (
    for /f "tokens=5" %%A in ('netstat -ano ^| findstr /R /C:":%%P "') do (
        if not "%%A"=="" (
            if not "%%A"=="0" (
                echo [clear_ports] Puerto %%P -> PID %%A. Terminando...
                taskkill /F /PID %%A 2>nul
                if !ERRORLEVEL! EQU 0 (
                    set "FOUND_ANY=1"
                ) else (
                    echo [clear_ports] No se pudo terminar PID %%A en puerto %%P ^(puede ser SYSTEM^).
                )
            )
        )
    )
)

echo [clear_ports] Estado final:
for %%P in (%BRIDGE_PORTS%) do (
    set "LINE="
    for /f "tokens=*" %%L in ('netstat -ano ^| findstr /R /C:":%%P " ^| findstr LISTENING') do (
        set "LINE=%%L"
    )
    if defined LINE (
        echo [clear_ports]   Puerto %%P AUN^> EN USO: !LINE!
    ) else (
        echo [clear_ports]   Puerto %%P libre.
    )
)

endlocal
exit /b 0
