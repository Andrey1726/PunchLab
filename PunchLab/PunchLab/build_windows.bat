@echo off
rem ======================================================================
rem  PunchLab: сборка и установка VST3 на Windows одним запуском.
rem  Нужно: Visual Studio 2022 или 2026 (бесплатная Community) с набором
rem  "Разработка классических приложений на C++" (в нём уже есть CMake).
rem  Запускайте правой кнопкой -> "Запуск от имени администратора",
rem  чтобы плагин сам скопировался в папку VST3.
rem ======================================================================
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if not errorlevel 1 goto :build

rem CMake не в PATH: ищем Visual Studio и подключаем её окружение
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :novs
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
where cmake >nul 2>nul
if errorlevel 1 goto :novs

:build
echo.
echo [1/3] Configuring (first run downloads JUCE, takes a few minutes)...
cmake -B build -A x64
if errorlevel 1 goto :fail

echo.
echo [2/3] Building PunchLab VST3...
cmake --build build --config Release --target PunchLab_VST3 --parallel
if errorlevel 1 goto :fail

set "OUT=%~dp0build\PunchLab_artefacts\Release\VST3\PunchLab.vst3"
set "DEST=%CommonProgramFiles%\VST3\PunchLab.vst3"
echo.
echo [3/3] Installing to "%DEST%"...
xcopy "%OUT%" "%DEST%\" /E /I /Y /Q >nul
if errorlevel 1 (
    echo Could not copy automatically ^(run this file as administrator^).
    echo Copy this folder manually into C:\Program Files\Common Files\VST3 :
    echo   %OUT%
) else (
    echo Installed. In Ableton Live: Preferences - Plug-Ins - Rescan.
)
echo.
pause
exit /b 0

:novs
echo.
echo Visual Studio with C++ tools was not found.
echo Install Visual Studio Community (free): https://visualstudio.microsoft.com/
echo and select the workload "Desktop development with C++", then run this file again.
echo.
pause
exit /b 1

:fail
echo.
echo Build failed. Scroll up to see the error.
pause
exit /b 1
