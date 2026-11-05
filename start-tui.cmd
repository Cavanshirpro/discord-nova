@echo off
setlocal
set "taskNovaExe="
if exist "%~dp0bin\discord-nova-tui.exe" set "taskNovaExe=%~dp0bin\discord-nova-tui.exe"
for %%B in (adopted-native-mingw64 mingw64 ucrt64 clang64 msvc default) do (
  if not defined taskNovaExe if exist "%~dp0build\%%B\discord-nova-tui.exe" (
    set "taskNovaExe=%~dp0build\%%B\discord-nova-tui.exe"
    if "%%B"=="adopted-native-mingw64" set "PATH=C:\msys64\mingw64\bin;%PATH%"
    if "%%B"=="mingw64" set "PATH=C:\msys64\mingw64\bin;%PATH%"
    if "%%B"=="ucrt64" set "PATH=C:\msys64\ucrt64\bin;%PATH%"
    if "%%B"=="clang64" set "PATH=C:\msys64\clang64\bin;%PATH%"
  )
)
if not defined taskNovaExe (
  echo Build Nova first. See docs\BUILDING.md.
  exit /b 1
)
if "%~1"=="--smoke-test" (
  "%taskNovaExe%" --demo --smoke-test
  if errorlevel 1 exit /b 1
  exit /b 0
)
if "%~1"=="--demo" (
  start "NOVA - Offline Demo" "%taskNovaExe%" --demo
  exit /b 0
)
if not exist "%~dp0config.json" copy "%~dp0config.example.json" "%~dp0config.json" >nul
if not exist "%~dp0test_token" (
  echo Create a local test_token file containing your token. See docs\TUI.md.
  exit /b 1
)
if not exist "%~dp0logs" mkdir "%~dp0logs"
start "NOVA - Discord C++" "%taskNovaExe%" "%~dp0config.json" "%~dp0test_token" "%~dp0logs\nova-events.jsonl"
