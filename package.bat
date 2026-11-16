@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1"
if errorlevel 1 (
  echo Packaging failed.
  pause
  exit /b 1
)
echo Packaging complete. See the dist folder.
pause
