# NEONdrive Firmware (CYD + T-Display-S3)

NEONdrive maintains separate firmware targets per device so users can flash the correct build without guessing.

## Supported Hardware

| Device | PlatformIO Env | Input Mode |
| --- | --- | --- |
| CYD 2.4 (ESP32-2432S024 family) | `firmware_cyd_2_4` | Touch (XPT2046) |
| LilyGO T-Display-S3 | `firmware_t_display_s3` | Touch (CST816/CST328) + hardware buttons fallback |
| M5Stack Tab5 (ESP32-P4 + C6) | `firmware_m5tab5` | Touch (GT911 via M5GFX) |
| M5Cardputer Advanced | planned | planned |

T-Display-S3 fallback controls:
- `BUTTON_1` = Next focus
- `BUTTON_2` = Select focused item

## Quick Install (Prebuilt `.bin` Files)

For public users, this is the easiest flow:

1. Open GitHub Releases.
2. Download the two files for your target:
   - `neondrive_<version>_<target>_app.bin`
   - `neondrive_<version>_<target>_fullflash.bin`
3. Flash one of these:
   - `fullflash.bin` for first install / clean install.
   - `app.bin` for upgrade at `0x10000`.

Detailed commands are in [docs/INSTALL.md](docs/INSTALL.md).

## Build and Flash From Source

Build:

```bash
python -m platformio run -e firmware_cyd_2_4
python -m platformio run -e firmware_t_display_s3
```

Flash:

```bash
python -m platformio run -e firmware_cyd_2_4 -t upload --upload-port COM10
python -m platformio run -e firmware_t_display_s3 -t upload --upload-port COM3
python -m platformio run -e firmware_m5tab5 -t upload --upload-port COM17
```

Helper scripts:

```bat
scripts\flash_cyd.cmd COM10
scripts\flash_tdisplay_s3.cmd COM3
scripts\flash_m5tab5.cmd COM17
scripts\monitor_cyd.cmd COM10
scripts\monitor_tdisplay_s3.cmd COM3
scripts\monitor_m5tab5.cmd COM17
```

## Release Artifacts (Maintainers)

Build release bins for all supported targets:

```bash
python scripts/build_release_bins.py --version v0.1.0
```

Outputs are written to:

```text
dist/releases/<version>/
```

That folder contains per-target `app.bin`, `fullflash.bin`, `release_manifest.json`, and `release_sha256.txt`.

Full release workflow is documented in [docs/RELEASES.md](docs/RELEASES.md).

## M5Cardputer Advanced / Launcher Plan

Planned launcher-compatible packaging approach is documented in:

- [docs/M5CARDPUTER_ADV_LAUNCHER_PLAN.md](docs/M5CARDPUTER_ADV_LAUNCHER_PLAN.md)

## Docs Index

- [docs/INSTALL.md](docs/INSTALL.md) - end-user install and flash commands
- [docs/HARDWARE_TARGETS.md](docs/HARDWARE_TARGETS.md) - target matrix
- [docs/RELEASES.md](docs/RELEASES.md) - maintainer release process
- [docs/memory.md](docs/memory.md) - DRAM/memory notes

## Legal

Use only on infrastructure you own or are explicitly authorized to test.
