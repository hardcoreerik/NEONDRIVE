# NEONDRIVE

NEONDRIVE is a multi-target ESP32 firmware suite for authorized wireless assessment labs. It includes touch/button/keyboard-first UI paths, shared workflows across multiple hardware families, and release-ready binaries for operators.

## Hardware Support

| Device | PlatformIO Env | Input Mode | Status |
| --- | --- | --- | --- |
| CYD 2.4 (ESP32-2432S024 family) | `firmware_cyd_2_4` | Touch (XPT2046) | Stable |
| CYD 2.8 (ESP32-2432S028 family) | `firmware_cyd_2_8` | Touch (XPT2046 bitbang profile) | Stable |
| CYD 3.5 (ESP32-3248S035 family) | `firmware_cyd_3_5` | Touch (XPT2046 + calibration path) | Stable |
| LilyGO T-Display-S3 | `firmware_t_display_s3` | Button nav (short press left/right, long press select) | Beta |
| LilyGO T-Display-S3 Touch | `firmware_t_display_s3_touch` | Touch + button nav | Beta |
| LilyGO T-Embed CC1101 | `firmware_t_embed_cc1101` | Button/encoder-style nav (target profile) | Untested |
| M5Stack Tab5 (ESP32-P4 + C6) | `firmware_m5tab5` | Touch (GT911 via M5GFX) | Active |
| M5Stack Cardputer Advanced | `firmware_cardputer_adv` | Physical keyboard-first UX | Alpha |

See [docs/HARDWARE_TARGETS.md](docs/HARDWARE_TARGETS.md) for target-specific notes and limits.

## Core Functions

NEONDRIVE is organized around a set of functional modules that share common target/workflow data:

- **WiFi Scanner / Connect**
  - Scan nearby APs, select target candidates, view SSID/BSSID/channel/RSSI/auth, and connect when needed.
  - Includes touch keyboard paths on touchscreen targets and physical-keyboard UX on Cardputer.
- **Packet Lab**
  - Unified attack-ops UI with action cards and target selection workflow.
  - Includes modules surfaced in UI as `PUSH!T`, `SP3CTER`, `Y0INK`, and `JAMM!T` plus classic packet actions.
- **Y0INK**
  - Handshake/PMKID-focused capture flow with automated state progression and telemetry.
- **SP3CTER**
  - Recon/analysis view module used inside Packet Lab workflow.
- **PUSH!T**
  - Broadcast/probe-style pressure workflow integrated into Packet Lab.
- **JAMM!T**
  - Deauth-heavy pressure mode in controlled authorized tests.
- **Web + Data Ops**
  - On-device web endpoints, status/telemetry views, and optional sync integrations (for example WPA-SEC/Dropbox paths when configured).

## Typical Workflow

1. **Pick hardware target**
   - Choose the matching PlatformIO env from the table above.
2. **Flash firmware**
   - Use helper scripts in `scripts\flash_*.cmd` or raw PlatformIO upload.
3. **Scan and select**
   - Open WiFi scanner, find candidate APs, set a target profile.
4. **Operate from Packet Lab**
   - Launch the needed function (`Y0INK`, `SP3CTER`, `PUSH!T`, `JAMM!T`, etc.) based on your authorized test goal.
5. **Review telemetry/results**
   - Use on-device monitor/status views, web endpoints, and capture outputs for analysis/reporting.

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

Windows helper scripts:

```bat
scripts\flash_cyd.cmd COM10
scripts\flash_cyd35.cmd COM15
scripts\flash_tdisplay_s3_touch.cmd COM7
scripts\flash_m5tab5.cmd COM17
scripts\flash_cardputer_adv.cmd COM5
```

Use `python -m platformio device list` to discover COM ports.

## Tab5 C6 Maintenance Workflow

Tab5 uses a split architecture (ESP32-P4 app/UI + ESP32-C6 WiFi coprocessor). For C6 ESP-Hosted maintenance and transport verification:

- Quick helper: `scripts\tab5_c6_update.cmd COM4`
- Detailed guide: [docs/TAB5_C6_FIRMWARE_UPDATE.md](docs/TAB5_C6_FIRMWARE_UPDATE.md)

This workflow updates/verifies C6 state and does not change NEONDRIVE firmware source logic.

## T-Display-S3 USB Composite Mode (CDC + MSC)

For `firmware_t_display_s3` and `firmware_t_display_s3_touch`:

- `CDC` stays available for serial logs and command input.
- `MSC` exposes TF shield SD card as removable storage on Windows.
- Runtime status fields include:
  - `usb_msc_active`
  - `sd_host_mounted`
  - `sd_app_locked`

## Precompiled Binaries

Release-ready device binaries are packaged in `Device-Bins/` and release artifacts.

See [docs/RELEASES.md](docs/RELEASES.md) for naming conventions and full artifact mapping.

## Security and Responsible Use

NEONDRIVE is for authorized environments only. Review [SECURITY.md](SECURITY.md) before use and for reporting guidance.

## Project Docs

- [docs/INSTALL.md](docs/INSTALL.md) - install/flash workflows
- [docs/HARDWARE_TARGETS.md](docs/HARDWARE_TARGETS.md) - per-target architecture and constraints
- [docs/RELEASES.md](docs/RELEASES.md) - packaging and release artifact rules
- [CHANGELOG.md](CHANGELOG.md) - release and feature history
- [CONTRIBUTING.md](CONTRIBUTING.md) - contributor workflow

---

Developed in collaboration with AI tools.
