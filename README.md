# NEONdrive // CYD

NEONdrive is a CYD-first ESP32 firmware project with a touch-driven neon UI for network recon workflows.

## What This Repo Contains

- CYD firmware (`PlatformIO`, environment: `cyd`)
- Unified UI system with hypercube visual anchor
- Core screens:
  - Home
  - WiFi Scan
  - Target Ops
  - Recon / Deauth Hunter
  - Monitor
  - Just Go
  - Config + submenus
- SD/log/capture handling and companion integration paths

## Target Hardware

- ESP32-2432S024 / CYD-class board
- 320x240 TFT (landscape)
- XPT2046 touch controller

## Build

```bash
python -m platformio run -e cyd
```

## Flash

```bash
python -m platformio run -e cyd -t upload --upload-port COM10
```

Replace `COM10` with your board’s serial port.

## Key Paths

- Firmware entry: `src/main.cpp`
- Build config: `platformio.ini`
- Headers: `include/`
- Docs: `docs/`

## Quick Revert Helpers

- `quick_revert_ui.cmd`
- `quick_revert_and_flash.cmd`

These restore `src/main.cpp` from:

- `.revert/main.cpp.restorepoint`

## Safety

Use only on networks and devices you own or are explicitly authorized to test.
