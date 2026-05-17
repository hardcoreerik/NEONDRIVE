# NEONDRIVE

NEONDRIVE is a multi-target ESP32 firmware + companion workflow project for authorized wireless assessment labs. It supports CYD and select LilyGO hardware, reproducible PlatformIO builds, and release-ready device binaries.

## What Makes This Different

- One repo supports multiple real hardware families (CYD + LilyGO) through explicit PlatformIO environments.
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
