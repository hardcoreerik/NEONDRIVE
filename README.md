# NEONDRIVE

NEONDRIVE is a multi-target ESP32 firmware + companion workflow project for authorized wireless assessment labs. It supports CYD and select LilyGO hardware, reproducible PlatformIO builds, and release-ready device binaries.

## Why NEONDRIVE

- Multi-device firmware targeting one operational workflow
- Reproducible build + flash steps with explicit board environments
- Hardware-focused iteration with practical release packaging

## Feature Highlights

- CYD and LilyGO target environments in `platformio.ini`
- Prebuilt firmware distribution via `Device-Bins/` and releases
- Hardware and install references in `docs/`
- Companion tooling and protocol docs in `CYDCompanion/` and `docs/`

## Repository Structure

- `platformio.ini` - build environments and target definitions
- `Device-Bins/` - packaged binaries by hardware target
- `docs/` - install, hardware matrix, architecture, and release notes
- `CYDCompanion/` - companion app/documentation
- `test/` - test-specific docs and supporting references

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

## Hardware Support

See [docs/HARDWARE_TARGETS.md](docs/HARDWARE_TARGETS.md) for the current matrix, status, and target-specific notes.

## Security and Responsible Use

This project is intended for authorized environments only. Follow [SECURITY.md](SECURITY.md) for reporting and handling issues.

## Contributing

Please review [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes.

## Changelog

Release notes and significant project changes are tracked in [CHANGELOG.md](CHANGELOG.md).
