@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM11

echo [flash] Target: LilyGO T-Embed CC1101
echo [flash] Env: firmware_t_embed_cc1101
echo [flash] Port: %PORT%

pio run -e firmware_t_embed_cc1101 -t upload --upload-port %PORT%
if errorlevel 1 (
  echo [flash] FAILED
  exit /b 1
)

echo [flash] SUCCESS
exit /b 0

