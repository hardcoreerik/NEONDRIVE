@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM7

echo [monitor] Target: LilyGO T-Display-S3 Touch
echo [monitor] Port: %PORT%
echo [monitor] Baud: 115200

python -m platformio device monitor --port %PORT% --baud 115200
exit /b %errorlevel%
