@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM17

echo [monitor] Target: M5Stack Tab5 (ESP32-P4 + ESP32-C6)
echo [monitor] Port: %PORT% @ 115200
echo [monitor] Serial commands: 'v' toggle verbose, 's' status snapshot
echo.

pio device monitor -p %PORT% -b 115200
