@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM3

echo [flash] Target: LilyGO T-Display-S3
echo [flash] Env: firmware_t_display_s3
echo [flash] Port: %PORT%

pio run -e firmware_t_display_s3 -t upload --upload-port %PORT%
if errorlevel 1 (
  echo [flash] FAILED
  exit /b 1
)

echo [flash] SUCCESS
exit /b 0

