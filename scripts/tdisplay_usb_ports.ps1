param(
  [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

Write-Host "[ports] Enumerating serial ports..." -ForegroundColor Cyan
python -m platformio device list

Write-Host ""
Write-Host "[hint] T-Display composite firmware may re-enumerate to a new COM port after reboot." -ForegroundColor Yellow
Write-Host "[hint] Flash (example):  python -m platformio run -e firmware_t_display_s3 -t upload --upload-port COM6"
Write-Host "[hint] Monitor (example): python -m platformio device monitor --port COM6 --baud $Baud"
