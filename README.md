# NEONdrive // CYD

![Platform](https://img.shields.io/badge/platform-ESP32--CYD-00c2ff)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-ffb300)
![UI](https://img.shields.io/badge/UI-touch%20first-neon)
![Status](https://img.shields.io/badge/status-active%20development-00c853)

NEONdrive is a two-part system:

1. CYD firmware (on-device recon + control surface)
2. Android companion app (operator workflow + data movement)

The firmware and Android app are both core product surfaces. This project is not firmware-only in intent.

## Why NEONdrive

NEONdrive is focused on:

- fast, touch-native field workflow on CYD
- clear visual telemetry under constrained screen space
- reliable handoff between embedded capture and mobile operations
- iterative, operator-driven UI tuning

## System Architecture

### Firmware (this repo)

- Device UI and workflow engine
- WiFi scanning, target operations, recon, monitor, and automation screens
- SD-backed capture/log handling
- Web/APK endpoints and companion sync indicators

### Android Companion (first-class component)

- Operator mobile interface
- File import/sync flow from CYD
- Capture management and downstream workflow support

Note: this repo currently contains firmware and Android integration hooks. The full Android production project may be maintained in a sibling workspace repo (for example `C:\ESP32\CYDCompanion`) depending on your local setup.

## Current Feature Surface

- Home dashboard with multi-screen navigation
- WiFi Scan + target selection
- Target Ops
- Recon / Deauth Hunter
- Monitor
- Just Go automation screen
- Config + submenus
- Hypercube visual anchor and shared neon UI language
- Android companion install/download endpoint flow from SD APK path (`/CYDCompanion.apk`)

## Hardware Target

- ESP32 CYD-class board (ESP32-2432S024 family)
- 320x240 TFT landscape
- XPT2046 touch controller

## Repository Layout

```text
.
|- src/                    # firmware source
|- include/                # headers/config interfaces
|- data/                   # runtime files (config/assets)
|- docs/                   # design + architecture notes
|- scripts/                # helper scripts
|- app/                    # app tree placeholder/integration staging
|- android-mini-game/      # optional in-app/game-related module
|- platformio.ini          # build environments
`- README.md
```

## Build Firmware

```bash
python -m platformio run -e cyd
```

## Flash Firmware

```bash
python -m platformio run -e cyd -t upload --upload-port COM10
```

Replace `COM10` with your device port.

## Serial / Runtime Notes

Typical serial baud:

```text
115200
```

Debug monitor commands currently available in firmware:

- `v` toggle verbose logging
- `s` status snapshot

## Android Companion Integration Workflow

High-level expected path:

1. Build/install companion APK on Android device
2. Ensure CYD can expose/download APK path and sync endpoints
3. Transfer/import captures/logs from CYD into Android app
4. Continue analysis/export from mobile

Firmware-side Android hooks are implemented around companion/APK flows and sync badge indicators in the UI.

## UI Design Principles

- no dead screen regions
- no hidden controls under hypercube
- bottom action rows aligned and touch-safe
- consistent button hitbox/draw alignment
- operator feedback first (status + live console clarity)

## Recovery / Safety Utilities

Quick rollback scripts included:

- `quick_revert_ui.cmd`
- `quick_revert_and_flash.cmd`

Restore source baseline from:

- `.revert/main.cpp.restorepoint`

## Git / Release Workflow

Minimal push cycle:

```bash
git add .
git commit -m "your change note"
git push
```

Repo name:

- `hardcoreerik/NEONdrive-CYD`

## Naming and Branding

Project identity is now **NEONdrive**.
Legacy PorkChop naming may still exist in older docs/history/modules and should be treated as migration debt unless explicitly required.

## Legal and Responsible Use

Use only on infrastructure you own or are explicitly authorized to test.
You are responsible for lawful and ethical operation.

## Contribution Expectations

When changing UI:

- include screen-specific reasoning
- maintain touch/draw alignment
- verify no overlap with hypercube and footer bars
- preserve operator-critical data visibility

When changing workflow:

- avoid regressing Android companion interoperability
- preserve file/capture integrity paths

---

NEONdrive is built as an integrated embedded + mobile workflow, not a standalone firmware demo.
