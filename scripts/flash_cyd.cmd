@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM10
set "PYTHONUTF8=1"
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
  echo [flash] ERROR: PlatformIO executable not found at:
  echo [flash]   %PIO_EXE%
  exit /b 1
)

echo [flash] Target: CYD 2.4
echo [flash] Env: firmware_cyd_2_4
echo [flash] Port: %PORT%

"%PIO_EXE%" run -e firmware_cyd_2_4 -t upload --upload-port %PORT%
if errorlevel 1 (
  echo [flash] FAILED
  exit /b 1
)

echo [flash] SUCCESS
exit /b 0

