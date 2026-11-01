@echo off
where pwsh >nul 2>nul
if %errorlevel% equ 0 (
  pwsh -NoProfile -File "%~dp0build.ps1" %*
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
)
exit /b %errorlevel%
