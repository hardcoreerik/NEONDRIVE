@echo off
setlocal

set "PORT=%~1"
set "PYTHONUTF8=1"
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
  echo [flash] ERROR: PlatformIO executable not found at:
  echo [flash]   %PIO_EXE%
  exit /b 1
)

if "%PORT%"=="" (
  echo [flash] No COM port argument provided.
  echo [flash] Available serial devices:
  echo.
  "%PIO_EXE%" device list
  echo.
  set /p PORT=[flash] Enter COM port to flash ^(example COM5^): 
)

if "%PORT%"=="" (
  echo [flash] ERROR: no COM port provided.
  exit /b 1
)

echo [flash] Target: M5Stack Cardputer Advanced ^(ESP32-S3^)
echo [flash] Env: firmware_cardputer_adv
echo [flash] Port: %PORT%
echo.
echo [flash] Tip: Cardputer Adv usually auto-resets via DTR.
echo.

"%PIO_EXE%" run -e firmware_cardputer_adv -t upload --upload-port %PORT%
if errorlevel 1 (
  echo [flash] FAILED
  exit /b 1
)

echo [flash] SUCCESS
exit /b 0
