@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM10

echo [monitor] Target: CYD 2.4
echo [monitor] Port: %PORT%
echo [monitor] Baud: 115200

pio device monitor -p %PORT% -b 115200

