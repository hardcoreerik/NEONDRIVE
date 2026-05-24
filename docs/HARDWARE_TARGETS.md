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

### CYD 2.8 (ESP32-2432S028 family)

- PlatformIO env: `firmware_cyd_2_8`
- Flash: `pio run -e firmware_cyd_2_8 -t upload --upload-port <PORT>`
- Monitor: `pio device monitor --port <PORT> --baud 115200`
- Input support in firmware:
  - XPT2046 touch (bitbang SPI on dedicated pins)

### CYD 3.5 (ESP32-3248S035R family)

- PlatformIO env: `firmware_cyd_3_5`
- Flash script: `scripts\flash_cyd35.cmd [COM_PORT]`
- Monitor script: `scripts\monitor_cyd35.cmd [COM_PORT]`
- Typical default port in this workspace: `COM15`
- Input support in firmware:
  - XPT2046 touch
  - one-time startup calibration saved to SD (`/touch_cal_cyd35.json`)

### LilyGO T-Display-S3 ⚠️ BETA

> **Status:** Beta — compiles and boots on hardware; not all features fully validated.
> Use at your own risk and please report issues.

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

Display: 5" 1280×720 IPS MIPI-DSI, driver initialised by M5GFX autodetect (`ARDUINO_M5STACK_TAB5` board define).
Touch: Goodix GT911 capacitive multi-touch (I²C: SDA=GPIO31, SCL=GPIO32), fully managed by M5GFX.

#### Touch implementation — authoritative notes

Touch on Tab5 is owned entirely by M5GFX. The correct implementation is:

```cpp
// setup():
M5.begin(cfg);          // PI4IOE reset + GT911 detect + coord mapping — all here

// loop():
M5.update();            // pump GT911 state machine

// read a tap:
auto td = M5.Touch.getDetail(0);
if (td.isPressed() || td.wasPressed()) { /* td.x, td.y */ }
```

**Do not** add manual PI4IOE pulsing, `M5.Touch.begin()` calls, or touch
recovery scaffolding — M5GFX owns the hardware at a lower level than any of
those approaches can reach.

**Critical:** `BOARD_HAS_SD` must stay `false` until Tab5 SD SPI pins are
mapped. Calling `sdSpi.begin(-1,-1,-1,-1)` assigns the ESP32-P4 default
VSPI MOSI to GPIO32, which is GT911 I²C SCL. This silently destroys the
I²C bus after M5.begin() completes, leaving touch dead for the session.
See `CLAUDE.md` for the full root-cause writeup.

#### Known Limitations (Tab5 vs CYD/T-Display-S3)

- **Raw frame injection** (`wsl_bypasser`, `bruce_wifi`) — disabled. `esp_wifi_80211_tx` and `ieee80211_raw_frame_sanity_check` are not available via the C6 co-processor bridge. All WSLBypasser and BruceWiFi call sites compile to no-ops.
- **Deauth/Beacon/Probe flood** — no-ops on Tab5 for the same reason.
- **WiFi** — crashes on init (`H_SDIO_DRV: sdio card init failed`). The ESP32-P4→C6 SDIO link is not yet stable. All WiFi-dependent features are non-functional until resolved.
- **SD card** — hardware present (SPI3) but disabled in firmware. Must not use default SPI pins (GPIO32 conflicts with GT911 I²C SCL). Re-enable only after verifying correct pin mapping from the Tab5 schematic.

#### Flash Entry (Boot Mode)

1. Connect USB-C data cable.
2. Hold **RESET** button ~2 seconds until the internal green LED blinks rapidly.
3. Release RESET — device enters ROM download mode.
4. Run flash script or `pio run -e firmware_m5tab5 -t upload --upload-port COM17`.

### LilyGO T-Embed CC1101 🚧 UNTESTED

> **Status:** Untested — firmware compiles but has not been validated on physical hardware.
> Flash at your own risk. Hardware-specific features (CC1101, encoder) are unverified.

- PlatformIO env: `firmware_t_embed_cc1101`
- Flash script: `scripts\flash_tembed_cc1101.cmd [COM_PORT]`
- Monitor script: `scripts\monitor_tembed_cc1101.cmd [COM_PORT]`
- Typical port (example): `COM11`
- Input support in firmware:
  - rotary encoder turn = Next/Previous
  - encoder press = Select
  - side key (`KEY`) = Next

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

1. Name artifacts by hardware target (`cyd_2_4`, `cyd_2_8`, `cyd_3_5`, `t_display_s3`, etc).
2. Keep one flashing command/script per target.
3. Avoid a single "generic" firmware filename for different boards.
4. Include exact COM/boot instructions for each board family.
5. Generate checksums and a release manifest.

Release process:
- [docs/RELEASES.md](RELEASES.md)
- `python scripts/build_release_bins.py --version vX.Y.Z`
- `python scripts/build_device_bins.py --version vX.Y.Z`
