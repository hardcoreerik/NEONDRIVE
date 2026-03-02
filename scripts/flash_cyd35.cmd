@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM15

echo [flash] Target: CYD 3.5
echo [flash] Env: firmware_cyd_3_5
echo [flash] Port: %PORT%

pio run -e firmware_cyd_3_5 -t upload --upload-port %PORT%
if errorlevel 1 (
  echo [flash] FAILED
  exit /b 1
)

echo [flash] SUCCESS
exit /b 0
