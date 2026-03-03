# NEONdrive Firmware
Multi-target ESP32 firmware for CYD and LilyGO boards.

This README is written for any machine.
No hardcoded serial ports. You always use `<PORT>`.

## Hardware Targets

| Device | PlatformIO Env | Chip | Input | Status |
|---|---|---|---|---|
| CYD 2.4 (ESP32-2432S024) | `firmware_cyd_2_4` | `esp32` | Touch | ✅ Stable |
| CYD 2.8 (ESP32-2432S028) | `firmware_cyd_2_8` | `esp32` | Touch | ✅ Stable |
| CYD 3.5 (ESP32-3248S035R) | `firmware_cyd_3_5` | `esp32` | Touch | ✅ Stable |
| LilyGO T-Display-S3 | `firmware_t_display_s3` | `esp32s3` | Touch + button fallback | ⚠️ Beta |
| LilyGO T-Embed CC1101 | `firmware_t_embed_cc1101` | `esp32s3` | Encoder + buttons | 🚧 Untested |

## Ground Rule: Port Is Never Hardcoded

Use `<PORT>` in every command.
Examples:
- Windows: `COM7`
- Linux: `/dev/ttyUSB0`
- macOS: `/dev/cu.usbmodem2101`

Find your port:

```bash
python -m platformio device list
```

## Path A: Flash Precompiled Binaries (Fastest)

Prebuilt files are in releases and `Device-Bins/`.

### Device-Bins naming

`NEONDRIVE_<DEVICE_NAME>_<VERSION>.bin`

### Flash one full image (offset `0x0`)

For `esp32` targets (CYD 2.4 / CYD 3.5):

```bash
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32 --port <PORT> --baud 460800 write_flash 0x0 NEONDRIVE_CYD-3.5_<VERSION>.bin
```

For `esp32s3` targets (T-Display-S3 / T-Embed):

```bash
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port <PORT> --baud 460800 write_flash 0x0 NEONDRIVE_T-DisplayS3_<VERSION>.bin
```

## Path B: Build From Source

Install PlatformIO:

```bash
python -m pip install -U platformio
```

Build all targets:

```bash
python -m platformio run -e firmware_cyd_2_4
python -m platformio run -e firmware_cyd_3_5
python -m platformio run -e firmware_t_display_s3
python -m platformio run -e firmware_t_embed_cc1101
```

Flash by target:

```bash
python -m platformio run -e firmware_cyd_2_4 -t upload --upload-port <PORT>
python -m platformio run -e firmware_cyd_3_5 -t upload --upload-port <PORT>
python -m platformio run -e firmware_t_display_s3 -t upload --upload-port <PORT>
python -m platformio run -e firmware_t_embed_cc1101 -t upload --upload-port <PORT>
```

Monitor serial:

```bash
python -m platformio device monitor -p <PORT> -b 115200
```

## CYD 3.5 Touch Calibration

- First boot runs touch calibration wizard.
- Calibration is saved to SD as `/touch_cal_cyd35.json`.
- To force recalibration, delete `/touch_cal_cyd35.json` and reboot.

## Release Packaging (Maintainers)

Build release artifacts:

```bash
python scripts/build_release_bins.py --version vX.Y.Z
```

Build user-facing `Device-Bins` package:

```bash
python scripts/build_device_bins.py --version vX.Y.Z
```

## Troubleshooting

- If upload fails, check cable quality (data cable required).
- If board is not detected, press BOOT/RESET and retry.
- If touch is wrong on CYD 3.5, remove SD calibration file and reboot.
- If port changes after reconnect, rerun:

```bash
python -m platformio device list
```

## Docs

- [docs/INSTALL.md](docs/INSTALL.md)
- [docs/HARDWARE_TARGETS.md](docs/HARDWARE_TARGETS.md)
- [docs/RELEASES.md](docs/RELEASES.md)
