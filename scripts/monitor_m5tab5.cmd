@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM17
set "PYTHONUTF8=1"
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
  echo [monitor] ERROR: PlatformIO executable not found at:
  echo [monitor]   %PIO_EXE%
  exit /b 1
)

echo [monitor] Target: M5Stack Tab5 (ESP32-P4 + ESP32-C6)
echo [monitor] Port: %PORT% @ 115200
echo [monitor] Serial commands: 'v' toggle verbose, 's' status snapshot
echo.

"%PIO_EXE%" device monitor -p %PORT% -b 115200
