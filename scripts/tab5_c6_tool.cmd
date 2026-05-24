@echo off
setlocal

if "%~1"=="" (
  echo Usage: %~nx0 ^<sdio_init^|scan_once^|scan_ui_check^> [--port COMx] [--noflash]
  exit /b 1
)

set "STEP=%~1"
shift

set "PORT=COM4"
set "NOFLASH="

:parse
if "%~1"=="" goto run
if /I "%~1"=="--port" (
  set "PORT=%~2"
  shift
  shift
  goto parse
)
if /I "%~1"=="--noflash" (
  set "NOFLASH=-NoFlash"
  shift
  goto parse
)
echo Unknown arg: %~1
exit /b 1

:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tab5_c6_tool.ps1" -Step "%STEP%" -Port "%PORT%" %NOFLASH%
exit /b %ERRORLEVEL%
