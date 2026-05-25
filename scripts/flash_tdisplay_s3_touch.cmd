@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM7

echo [flash] Target: LilyGO T-Display-S3 Touch
echo [flash] Env: firmware_t_display_s3_touch
echo [flash] Port: %PORT%

python -m platformio run -e firmware_t_display_s3_touch -t upload --upload-port %PORT%
exit /b %errorlevel%
