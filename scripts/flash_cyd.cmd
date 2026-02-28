@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM10

echo [flash] Target: CYD 2.4
echo [flash] Env: firmware_cyd_2_4
echo [flash] Port: %PORT%

pio run -e firmware_cyd_2_4 -t upload --upload-port %PORT%
if errorlevel 1 (
  echo [flash] FAILED
  exit /b 1
)

echo [flash] SUCCESS
exit /b 0

