@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM11

echo [monitor] Target: LilyGO T-Embed CC1101
echo [monitor] Port: %PORT%
echo [monitor] Baud: 115200

pio device monitor -p %PORT% -b 115200

