# NEONdrive Hardware Targets

This repo now maintains separate firmware targets so users can flash the correct build for their device.

Primary install guide:
- [docs/INSTALL.md](INSTALL.md)

## Supported Now

### CYD 2.4 (ESP32-2432S024 family)

- PlatformIO env: `firmware_cyd_2_4`
- Flash script: `scripts\flash_cyd.cmd [COM_PORT]`
- Monitor script: `scripts\monitor_cyd.cmd [COM_PORT]`
- Typical default port in this workspace: `COM10`

### LilyGO T-Display-S3

- PlatformIO env: `firmware_t_display_s3`
- Flash script: `scripts\flash_tdisplay_s3.cmd [COM_PORT]`
- Monitor script: `scripts\monitor_tdisplay_s3.cmd [COM_PORT]`
- Typical default port in this workspace: `COM3`
- Input support in firmware:
  - capacitive touch auto-detect (if touch controller is populated)
  - hardware button fallback: `BUTTON_1` = Next, `BUTTON_2` = Select

### M5Stack Tab5

- PlatformIO env: `firmware_m5tab5`
- Flash script: `scripts\flash_m5tab5.cmd [COM_PORT]`
- Monitor script: `scripts\monitor_m5tab5.cmd [COM_PORT]`
- Default port: `COM17`
- Platform: [pioarduino](https://github.com/pioarduino/platform-espressif32) (Arduino-ESP32 with P4 support)

#### Dual-Processor Architecture

The Tab5 uses two chips working in tandem:

| Chip | Role | Specs |
| --- | --- | --- |
| **ESP32-P4** | Main CPU / Display / Application | Dual-core RISC-V @ 400 MHz, 32 MB PSRAM, 16 MB Flash |
| **ESP32-C6** | WiFi 6 + Bluetooth 5 co-processor | Handles all RF; exposed to P4 via standard `WiFi.h` API |

Display: 5" 1280×720 IPS MIPI-DSI, driver initialized by M5GFX (`ARDUINO_M5STACK_TAB5` board define).
Touch: Goodix GT911 multi-touch (I²C), read via `tft.getTouch()`.

#### Known Limitations (Tab5 vs CYD/T-Display-S3)

- **Raw frame injection** (`wsl_bypasser`, `bruce_wifi`) — disabled. `esp_wifi_80211_tx` and `ieee80211_raw_frame_sanity_check` are not available via the C6 co-processor bridge. All WSLBypasser and BruceWiFi call sites compile to no-ops.
- **Deauth/Beacon/Probe flood** — no-ops on Tab5 for the same reason.
- **SD card** — hardware present (SPI3); requires pin verification against Tab5 schematic before enabling.

#### Flash Entry (Boot Mode)

1. Connect USB-C data cable.
2. Hold **RESET** button ~2 seconds until the internal green LED blinks rapidly.
3. Release RESET — device enters ROM download mode.
4. Run flash script or `pio run -e firmware_m5tab5 -t upload --upload-port COM17`.

## Planned Next

### M5Cardputer Advanced

- Status: planned, not implemented in this repo yet
- Future work:
  - add dedicated PlatformIO environment
  - add dedicated UI/input profile (keyboard + trackball/touch if applicable)
  - add dedicated flash/monitor scripts
  - add launcher-compatible release artifact set

Plan document:
- [docs/M5CARDPUTER_ADV_LAUNCHER_PLAN.md](M5CARDPUTER_ADV_LAUNCHER_PLAN.md)

## Release Guidance

When publishing binaries/releases:

1. Name artifacts by hardware target (`cyd_2_4`, `t_display_s3`, etc).
2. Keep one flashing command/script per target.
3. Avoid a single "generic" firmware filename for different boards.
4. Include exact COM/boot instructions for each board family.
5. Generate checksums and a release manifest.

Release process:
- [docs/RELEASES.md](RELEASES.md)
- `python scripts/build_release_bins.py --version vX.Y.Z`
