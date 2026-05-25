#!/usr/bin/env bash
# NEONDRIVE Firmware Installer — macOS / Linux
# Usage: bash install.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELPER="$SCRIPT_DIR/tools/installer/installer_helper.py"
export PYTHONUTF8=1

# ── Locate Python ─────────────────────────────────────────────────────────────
find_python() {
  if [ -x "$SCRIPT_DIR/tools/installer/python/python3" ]; then
    echo "$SCRIPT_DIR/tools/installer/python/python3"; return
  fi
  for py in python3 python; do
    if command -v "$py" &>/dev/null; then
      echo "$py"; return
    fi
  done
  echo ""
}

PY=$(find_python)
if [ -z "$PY" ]; then
  echo ""
  echo " [error] Python 3.10+ not found."
  echo "         Install from https://python.org"
  echo ""
  exit 1
fi

# ── Bootstrap venv ────────────────────────────────────────────────────────────
echo ""
echo " [setup] Checking installer toolchain..."
"$PY" "$HELPER" bootstrap

if [[ "$(uname)" == "MINGW"* || "$(uname)" == "CYGWIN"* ]]; then
  VENV_PY="$SCRIPT_DIR/.installer/venv/Scripts/python.exe"
else
  VENV_PY="$SCRIPT_DIR/.installer/venv/bin/python"
fi

if [ ! -x "$VENV_PY" ]; then
  echo " [error] venv not found at $VENV_PY"
  exit 1
fi

# ── Board definitions ─────────────────────────────────────────────────────────
# Format: "NAME:CHIP:LABEL"
BOARDS=(
  "CYD-2.4:esp32:CYD 2.4\"  ESP32 Touch Display      [Stable]"
  "CYD-2.8:esp32:CYD 2.8\"  ESP32 Touch Display      [Stable]"
  "CYD-3.5:esp32:CYD 3.5\"  ESP32 Touch Display      [Stable]"
  "M5Tab5:esp32p4:M5Stack Tab5  ESP32-P4              [Stable]"
  "T-DisplayS3:esp32s3:T-Display S3  LilyGO ESP32-S3  [Beta]"
  "T-Embed-CC1101:esp32s3:T-Embed CC1101  Sub-GHz      [Beta]"
  "Cardputer-Adv:esp32s3:Cardputer Adv  M5Stack kbd    [Alpha]"
)

BOARD=""
PORT=""

# ── Board selection ───────────────────────────────────────────────────────────
select_board() {
  clear
  echo ""
  echo " ==========================================================="
  echo "  NEONDRIVE Firmware Installer"
  echo " ==========================================================="
  echo ""
  echo "  Select Your Hardware"
  echo " -----------------------------------------------------------"
  echo ""
  local i=1
  for entry in "${BOARDS[@]}"; do
    local label="${entry#*:*:}"
    printf "   %d.  %s\n" "$i" "$label"
    ((i++))
  done
  echo "   Q.  Quit"
  echo ""
  read -rp "  Select board [1-${#BOARDS[@]}]: " BSEL
  if [[ "${BSEL,,}" == "q" ]]; then
    echo ""; echo " Bye."; echo ""; exit 0
  fi
  if ! [[ "$BSEL" =~ ^[0-9]+$ ]] || [ "$BSEL" -lt 1 ] || [ "$BSEL" -gt "${#BOARDS[@]}" ]; then
    select_board; return
  fi
  local entry="${BOARDS[$((BSEL - 1))]}"
  BOARD="${entry%%:*}"
}

# ── COM port selection ────────────────────────────────────────────────────────
select_port() {
  clear
  echo ""
  echo " ==========================================================="
  echo "  Select Serial Port  |  Board: $BOARD"
  echo " ==========================================================="
  echo ""
  echo "  Scanning for connected devices..."
  echo ""

  mapfile -t PORT_LINES < <("$VENV_PY" "$HELPER" list-ports 2>/dev/null || true)

  if [ ${#PORT_LINES[@]} -eq 0 ]; then
    echo "  [!] No serial ports detected."
    echo "      Connect your board and press Enter to retry."
    echo ""
    read -rp "  Press Enter to retry, or type Q to go back: " RETRY
    if [[ "${RETRY,,}" == "q" ]]; then select_board; fi
    select_port; return
  fi

  local i=1
  for line in "${PORT_LINES[@]}"; do
    local dev="${line%%|*}"
    local desc="${line#*|}"
    printf "   %d.  %-12s -- %s\n" "$i" "$dev" "$desc"
    ((i++))
  done
  echo ""
  echo "   R.  Refresh list"
  echo "   B.  Back to board selection"
  echo ""
  read -rp "  Select port [1-${#PORT_LINES[@]}]: " PSEL

  if [[ "${PSEL,,}" == "r" ]]; then select_port; return; fi
  if [[ "${PSEL,,}" == "b" ]]; then select_board; select_port; return; fi

  if ! [[ "$PSEL" =~ ^[0-9]+$ ]] || [ "$PSEL" -lt 1 ] || [ "$PSEL" -gt "${#PORT_LINES[@]}" ]; then
    select_port; return
  fi
  PORT="${PORT_LINES[$((PSEL - 1))]%%|*}"
}

# ── Confirm + flash ───────────────────────────────────────────────────────────
do_flash() {
  clear
  echo ""
  echo " ==========================================================="
  echo "  Ready to Flash"
  echo " ==========================================================="
  echo ""
  echo "  Board :  $BOARD"
  echo "  Port  :  $PORT"
  echo ""

  if [ "$BOARD" == "M5Tab5" ]; then
    echo " [!] Tab5 Download Mode Required:"
    echo "     Hold RESET ~2s until the green LED blinks rapidly,"
    echo "     then release.  Do this just before confirming below."
    echo ""
  fi
  if [ "$BOARD" == "Cardputer-Adv" ]; then
    echo " [i] Cardputer Adv auto-resets via DTR. No button hold needed."
    echo ""
  fi
  if [ "$BOARD" == "CYD-3.5" ]; then
    echo " [i] First boot will run touch calibration."
    echo "     Follow on-screen instructions on the device."
    echo ""
  fi

  read -rp "  Proceed with flashing? [Y/N]: " GO
  if [[ "${GO^^}" != "Y" ]]; then
    echo "  Cancelled."
    select_board
    select_port
    do_flash
    return
  fi

  if [ ! -f "$SCRIPT_DIR/Device-Bins/installer_manifest.json" ]; then
    echo ""
    echo " [error] Device-Bins/installer_manifest.json not found."
    echo "         This release package may be incomplete."
    exit 1
  fi

  echo ""
  echo " ==========================================================="
  echo "  Flashing..."
  echo " ==========================================================="
  echo ""

  "$VENV_PY" "$HELPER" flash-bin --board "$BOARD" --port "$PORT"

  echo ""
  echo " ==========================================================="
  echo "  SUCCESS -- Firmware flashed!"
  echo " ==========================================================="
  echo ""
  echo "  Disconnect and reconnect your device to boot NEONDRIVE."
  echo ""
}

# ── Run ───────────────────────────────────────────────────────────────────────
select_board
select_port
do_flash
