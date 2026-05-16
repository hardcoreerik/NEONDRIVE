# NEONdrive Install Guide

This guide gives users two install paths:

1. Flash a prebuilt release `.bin` (recommended).
2. Build from source and flash with PlatformIO.

## 1) Prerequisites

- USB data cable
- Python 3.10+ (for `python -m platformio`)
- PlatformIO Core:

```bash
python -m pip install -U platformio
```

Find your COM port:

```bash
python -m platformio device list
```

## 2) Choose Your Target

| Hardware | Target Key | Chip | Fullflash Offset | App Offset |
| --- | --- | --- | --- | --- |
| CYD 2.4 | `cyd_2_4` | `esp32` | `0x0` | `0x10000` |
| LilyGO T-Display-S3 | `t_display_s3` | `esp32s3` | `0x0` | `0x10000` |
| M5Stack Tab5 | `m5tab5` | `esp32p4` | `0x0` | `0x10000` |

Release filenames:

- `neondrive_<version>_<target>_fullflash.bin`
- `neondrive_<version>_<target>_app.bin`

## 3) Flash Prebuilt Binaries (Recommended)

### CYD 2.4

Full install:

```bash
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32 --port COM10 --baud 460800 write_flash 0x0 neondrive_<version>_cyd_2_4_fullflash.bin
```

Upgrade only:

```bash
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32 --port COM10 --baud 460800 write_flash 0x10000 neondrive_<version>_cyd_2_4_app.bin
```

### LilyGO T-Display-S3

Full install:

```bash
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash 0x0 neondrive_<version>_t_display_s3_fullflash.bin
```

Upgrade only:

```bash
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash 0x10000 neondrive_<version>_t_display_s3_app.bin
```

### M5Stack Tab5

Boot mode: hold **RESET** ~2 s until internal green LED blinks rapidly, then release.

Full install:

```bash
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32p4 --port COM17 --baud 1500000 write_flash 0x0 neondrive_<version>_m5tab5_fullflash.bin
```

Upgrade only:

```bash
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32p4 --port COM17 --baud 1500000 write_flash 0x10000 neondrive_<version>_m5tab5_app.bin
```

## 4) Build and Flash From Source

Build:

```bash
python -m platformio run -e firmware_cyd_2_4
python -m platformio run -e firmware_t_display_s3
python -m platformio run -e firmware_m5tab5
```

Flash:

```bash
python -m platformio run -e firmware_cyd_2_4 -t upload --upload-port COM10
python -m platformio run -e firmware_t_display_s3 -t upload --upload-port COM3
python -m platformio run -e firmware_m5tab5 -t upload --upload-port COM17
```

Windows helper scripts:

```bat
scripts\flash_cyd.cmd COM10
scripts\flash_tdisplay_s3.cmd COM3
scripts\flash_m5tab5.cmd COM17
```

## 5) Serial Monitor

Baud rate: `115200`

```bash
python -m platformio device monitor -p COM10 -b 115200
python -m platformio device monitor -p COM3 -b 115200
```

Runtime debug commands:

- `v` toggles verbose logging
- `s` prints status snapshot

## 6) Boot Mode Notes

- If flashing fails with connection/reset errors, put the board in download mode and retry.
- T-Display-S3 typically uses `BOOT` + `RST` to enter ROM download mode.
- Keep board-specific USB drivers up to date on Windows.
