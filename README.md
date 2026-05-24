# NEONDRIVE

NEONDRIVE is a multi-target ESP32 firmware + companion workflow project for authorized wireless assessment labs. It supports CYD and select LilyGO hardware, reproducible PlatformIO builds, and release-ready device binaries.

## What Makes This Different

| Device | PlatformIO Env | Input Mode |
| --- | --- | --- |
| CYD 2.4 (ESP32-2432S024 family) | `firmware_cyd_2_4` | Touch (XPT2046) |
| CYD 3.5 (ESP32-3248S035) | `firmware_cyd_3_5` | Touch (XPT2046) |
| LilyGO T-Display-S3 | `firmware_t_display_s3` | Touch (CST816/CST328) + hardware buttons fallback |
| M5Stack Tab5 (ESP32-P4 + C6) | `firmware_m5tab5` | Touch (GT911 via M5GFX) |

- One repo supports multiple real hardware families through explicit PlatformIO environments.
- Firmware releases are packaged for operators, not just developers (`Device-Bins/` plus release artifacts).
- Documentation includes target matrix, install guides, telemetry notes, and companion protocol references.

## Core Engineering Surface

- Multi-target build orchestration in `platformio.ini`
- Hardware-specific documentation in `docs/HARDWARE_TARGETS.md`
- Release packaging in `Device-Bins/`
- Companion protocol and sync references in `docs/` and `CYDCompanion/`

## Quick Start

### Build

```powershell
python -m pip install -U platformio
python -m platformio run -e firmware_cyd_3_5
```

### Flash

```powershell
python -m platformio run -e firmware_cyd_3_5 -t upload --upload-port COM5
python -m platformio run -e firmware_m5tab5 -t upload --upload-port COM17
```

Or use the helper scripts:

```bat
scripts\flash_m5tab5.cmd COM17
scripts\monitor_m5tab5.cmd COM17
```

Use `python -m platformio device list` to discover serial ports.

## Supported Targets

See [docs/HARDWARE_TARGETS.md](docs/HARDWARE_TARGETS.md) for the current target matrix and support status.

## CI and Quality Bar

The GitHub Actions workflow builds all major firmware environments on pull requests to prevent target regressions before merge.

## Security and Responsible Use

This project is intended for authorized environments only. Follow [SECURITY.md](SECURITY.md) for reporting and handling issues.

## Contributing

Please review [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes.

## Changelog

Release notes and significant project changes are tracked in [CHANGELOG.md](CHANGELOG.md).

---

Developed in collaboration with AI tools.
