@echo off
REM ASTRA Collector - one-click launcher.
REM Double-click this file to open the collector window.
REM No administrator rights are required.

setlocal
set "HERE=%~dp0"
set "EXE=%HERE%ASTRA-Collector.exe"

if not exist "%EXE%" (
    echo.
    echo ASTRA-Collector.exe was not found next to this launcher.
    echo Keep ASTRA-Collector.exe and this file in the same folder.
    echo.
    pause
    exit /b 1
)

start "" "%EXE%"
endlocal
