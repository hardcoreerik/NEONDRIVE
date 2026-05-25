@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM17
set "PYTHONUTF8=1"
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
  echo [flash] ERROR: PlatformIO executable not found at:
  echo [flash]   %PIO_EXE%
  exit /b 1
)

echo [flash] Target: M5Stack Tab5 (ESP32-P4 + ESP32-C6)
echo [flash] Env: firmware_m5tab5
echo [flash] Port: %PORT%
echo.
echo [flash] Boot-mode hint: hold RESET ~2 s until internal green LED blinks
echo [flash]   rapidly, then release. Device enters ROM download mode.
echo.

"%PIO_EXE%" run -e firmware_m5tab5 -t upload --upload-port %PORT%
if errorlevel 1 (
  echo [flash] FAILED
  exit /b 1
)

echo [flash] SUCCESS
exit /b 0
