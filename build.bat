@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
  exit /b
)
if not exist build mkdir build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-sensors.ps1"
if errorlevel 1 exit /b 1
rc /nologo /fo build\resources.res resources.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /EHsc /MT /utf-8 MiniPulse.cpp TemperatureReader.cpp HardwareSensors.cpp TaskbarWidget.cpp build\resources.res /Fe:MiniPulse.exe /Fo:build\ /link /SUBSYSTEM:WINDOWS /MANIFEST:NO
if errorlevel 1 exit /b 1
echo Built: %CD%\MiniPulse.exe
