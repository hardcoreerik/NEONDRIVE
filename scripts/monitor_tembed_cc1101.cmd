@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM9
set "PYTHONUTF8=1"
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
  echo [monitor] ERROR: PlatformIO executable not found at:
  echo [monitor]   %PIO_EXE%
  exit /b 1
)

echo [monitor] Target: LilyGO T-Embed CC1101
echo [monitor] Port: %PORT%
echo [monitor] Baud: 115200

"%PIO_EXE%" device monitor -p %PORT% -b 115200

