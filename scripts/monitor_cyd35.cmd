@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM15

echo [monitor] Target: CYD 3.5
echo [monitor] Port: %PORT%
echo [monitor] Baud: 115200

pio device monitor -p %PORT% -b 115200
