@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"
set "ROOT=%cd%"
set "HELPER=%ROOT%\tools\installer\installer_helper.py"

if not exist "%HELPER%" (
  echo.
  echo  [error] Missing: %HELPER%
  echo          Is this the correct installer folder?
  echo.
  pause & exit /b 1
)

:: ── Locate Python ────────────────────────────────────────────────────────────
set "PY="
if exist "%ROOT%\tools\installer\python\python.exe" (
  set "PY=%ROOT%\tools\installer\python\python.exe"
) else (
  where py >nul 2>nul && set "PY=py -3"
)
if not defined PY (
  where python >nul 2>nul && set "PY=python"
)
if not defined PY (
  echo.
  echo  [error] Python 3.10+ not found.
  echo          Install from https://python.org or place a Python runtime at:
  echo            tools\installer\python\python.exe
  echo.
  pause & exit /b 1
)

:: ── Bootstrap venv (esptool + pyserial only) ─────────────────────────────────
set PYTHONUTF8=1
echo.
echo  [setup] Checking installer toolchain...
%PY% "%HELPER%" bootstrap
if errorlevel 1 (
  echo.
  echo  [error] Bootstrap failed. Check your internet connection.
  pause & exit /b 1
)

set "VENV_PY=%ROOT%\.installer\venv\Scripts\python.exe"
if not exist "%VENV_PY%" (
  echo  [error] venv not created at %VENV_PY%
  pause & exit /b 1
)

:: ── Main menu ─────────────────────────────────────────────────────────────────
:MAIN_MENU
cls
echo.
echo  ===========================================================
echo   NEONDRIVE Firmware Installer
echo  ===========================================================
echo.
echo   1. Flash firmware to device
echo   2. Exit
echo.
set /p "MODE=  Choose [1-2]: "
if "%MODE%"=="1" goto SELECT_BOARD
if "%MODE%"=="2" exit /b 0
goto MAIN_MENU

:: ── Board selection ───────────────────────────────────────────────────────────
:SELECT_BOARD
cls
echo.
echo  ===========================================================
echo   Select Your Hardware
echo  ===========================================================
echo.
echo    1.  CYD 2.4"          ESP32 Touch Display      [Stable]
echo    2.  CYD 2.8"          ESP32 Touch Display      [Stable]
echo    3.  CYD 3.5"          ESP32 Touch Display      [Stable]
echo    4.  M5Stack Tab5      ESP32-P4                 [Stable]
echo    5.  T-Display S3      LilyGO ESP32-S3          [Beta]
echo    6.  T-Embed CC1101    LilyGO + Sub-GHz radio   [Beta]
echo    7.  Cardputer Adv     M5Stack ESP32-S3 kbd     [Alpha]
echo    8.  Back
echo.
set "BOARD=" & set "CHIP="
set /p "BSEL=  Select board [1-8]: "
if "%BSEL%"=="1" set "BOARD=CYD-2.4"         & set "CHIP=esp32"
if "%BSEL%"=="2" set "BOARD=CYD-2.8"         & set "CHIP=esp32"
if "%BSEL%"=="3" set "BOARD=CYD-3.5"         & set "CHIP=esp32"
if "%BSEL%"=="4" set "BOARD=M5Tab5"           & set "CHIP=esp32p4"
if "%BSEL%"=="5" set "BOARD=T-DisplayS3"      & set "CHIP=esp32s3"
if "%BSEL%"=="6" set "BOARD=T-Embed-CC1101"   & set "CHIP=esp32s3"
if "%BSEL%"=="7" set "BOARD=Cardputer-Adv"    & set "CHIP=esp32s3"
if "%BSEL%"=="8" goto MAIN_MENU
if not defined BOARD goto SELECT_BOARD

:: ── COM port selection ────────────────────────────────────────────────────────
:SELECT_PORT
set "PORT=" & set "PORT_COUNT=0"
cls
echo.
echo  ===========================================================
echo   Select COM Port  ^|  Board: %BOARD%
echo  ===========================================================
echo.
echo   Scanning for connected devices...
echo.

for /f "tokens=1,* delims=|" %%A in (`"%VENV_PY%" "%HELPER%" list-ports`) do (
  set /a PORT_COUNT+=1
  set "PORT_DEV_!PORT_COUNT!=%%A"
  echo    !PORT_COUNT!.  %%A  --  %%B
)

if %PORT_COUNT%==0 (
  echo   [!] No COM ports detected.
  echo       Connect your board and press any key to retry.
  echo.
  pause >nul
  goto SELECT_PORT
)
echo.
echo    R.  Refresh port list
echo    B.  Back to board selection
echo.
set /p "PSEL=  Select port [1-%PORT_COUNT%]: "
if /I "%PSEL%"=="R" goto SELECT_PORT
if /I "%PSEL%"=="B" goto SELECT_BOARD
if "%PSEL%"=="" goto SELECT_PORT

set /a "PSEL_N=%PSEL%" 2>nul
if %PSEL_N% LSS 1 goto SELECT_PORT
if %PSEL_N% GTR %PORT_COUNT% goto SELECT_PORT
call set "PORT=%%PORT_DEV_%PSEL_N%%%"
if not defined PORT goto SELECT_PORT

:: ── Pre-flash confirmation ────────────────────────────────────────────────────
cls
echo.
echo  ===========================================================
echo   Ready to Flash
echo  ===========================================================
echo.
echo   Board :  %BOARD%
echo   Port  :  %PORT%
echo.

if /I "%BOARD%"=="M5Tab5" (
  echo  [^!] Tab5 Download Mode Required:
  echo      Hold RESET ~2s until the green LED blinks rapidly,
  echo      then release.  Do this just before confirming below.
  echo.
)
if /I "%BOARD%"=="Cardputer-Adv" (
  echo  [i] Cardputer Adv auto-resets via DTR.
  echo      No button hold required.
  echo.
)
if /I "%BOARD%"=="CYD-3.5" (
  echo  [i] First boot will run touch calibration.
  echo      Follow on-screen instructions on the device.
  echo.
)

set /p "GO=  Proceed with flashing? [Y/N]: "
if /I not "%GO%"=="Y" goto MAIN_MENU

:: ── Flash ─────────────────────────────────────────────────────────────────────
echo.
echo  ===========================================================
echo   Flashing...
echo  ===========================================================
echo.

if not exist "%ROOT%\Device-Bins\installer_manifest.json" (
  echo  [error] Device-Bins\installer_manifest.json not found.
  echo          This release package may be incomplete.
  pause & goto MAIN_MENU
)

"%VENV_PY%" "%HELPER%" flash-bin --board "%BOARD%" --port "%PORT%"
if errorlevel 1 (
  echo.
  echo  ===========================================================
  echo   FLASH FAILED
  echo  ===========================================================
  echo.
  echo   Troubleshooting:
  echo   - Make sure the device is in download mode
  echo   - Try a different USB cable or port
  echo   - Check Windows Device Manager for driver issues
  echo   - For Tab5: hold RESET longer before releasing
  echo.
  pause
  goto MAIN_MENU
)

echo.
echo  ===========================================================
echo   SUCCESS -- Firmware flashed!
echo  ===========================================================
echo.
echo   Disconnect and reconnect your device to boot NEONDRIVE.
echo.
pause
goto MAIN_MENU
