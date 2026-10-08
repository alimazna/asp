@echo off
REM ASTRA Desktop — Windows build convenience script
REM Requires: CMake 3.16+, Qt 6 (MSVC or MinGW), windeployqt

set CMAKE_PREFIX_PATH=C:\Qt\6.5.0\msvc2019_64
set BUILD_DIR=build

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo === Configuring ===
cmake -S . -B "%BUILD_DIR%" -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

echo === Building ===
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b 1

echo === Deploying ===
windeployqt "%BUILD_DIR%\bin\astra_desktop.exe" --release --no-compiler-runtime --no-translations --no-system-d3d-compiler
if errorlevel 1 exit /b 1

echo === Done ===
echo Binary: %BUILD_DIR%\bin\astra_desktop.exe
pause
