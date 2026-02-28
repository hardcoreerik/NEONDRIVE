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
