# M5Cardputer Advanced + M5 Launcher Plan

This document defines how we should package NEONdrive when the M5Cardputer Advanced port is added.

## Goal

Allow users to pick a NEONdrive `.bin` from the M5 Launcher SD-card app list and launch/install it without custom tooling.

## Current Status

- M5Cardputer Advanced target is not implemented yet in this repo.
- This is the packaging/integration plan to follow once that target exists.

## Planned Firmware Target

Add a dedicated PlatformIO environment (example name):

- `firmware_m5cardputer_adv`

Do not reuse CYD or T-Display-S3 binaries for M5 hardware.

## Planned Release Artifacts

When the target exists, publish at minimum:

- `neondrive_<version>_m5cardputer_adv_app.bin`
- `neondrive_<version>_m5cardputer_adv_fullflash.bin`

Optional launcher-focused bundle:

- `neondrive_<version>_m5cardputer_adv_launcher.zip`

## Launcher Compatibility Strategy

The Launcher SD install flow expects a firmware binary users can browse/select from SD storage.

Plan:

1. Keep a dedicated M5 binary filename pattern (`*_m5cardputer_adv_*.bin`).
2. Document exact SD path convention in `docs/INSTALL.md` once verified on real hardware.
3. Validate both methods on device:
   - launcher SD install path
   - direct serial flash path
4. Keep offset guidance explicit:
   - app-only binary at `0x10000`
   - fullflash binary at `0x0`

## Required Validation Before Public Release

1. Flash and boot test on M5Cardputer Advanced.
2. Input profile test (keyboard/buttons/touch/trackball as applicable).
3. Launcher SD selection test from a clean SD card.
4. Upgrade test (`app.bin`) over previous release.
5. Clean install test (`fullflash.bin`) from blank flash.

## Why This Matters

Keeping a separate target + naming convention avoids user confusion and makes launcher-based installation discoverable for non-developer users.
