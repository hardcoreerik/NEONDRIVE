# Tab5 RF Inspection Report

## Summary

NEONDRIVE was originally built for CYD-class ESP32 hardware where the application CPU owns the local Wi-Fi radio.  
M5Tab5 is different: it uses an **ESP32-P4** for UI/app logic and an **ESP32-C6** co-processor for Wi-Fi/BLE.

Direct local `esp_wifi_*` usage from the P4 path is unsafe for Tab5 RF features until hosted transport and C6 firmware compatibility are validated.

## Findings

- High-risk shared files contain legacy/local RF behavior and must remain guarded for Tab5:
  - `src/main.cpp`
  - `src/yoink_engine.cpp`
  - `src/wifiscan.cpp`
  - `src/deauth_hunter.cpp`
- Tab5-safe behavior requires routing through `neon_rf` (or explicit guarded stubs), returning unsupported where backend capability is unavailable.
- Crash/reboot signatures observed during early hosted bringup:
  - `H_SDIO_DRV: sdio card init failed`
  - `Task "sdio_read" should not return`

## Required Boundary

On Tab5:
- P4 must not assume direct local radio ownership for RF operations.
- Unsupported paths must log and return unsupported-safe outcomes.
- C6 bringup must be explicit and test-gated, not enabled silently in production.

On CYD:
- Existing local Wi-Fi behavior can remain when properly compile-gated.

## Current Direction

- `neon_rf` is the RF capability abstraction boundary.
- Production Tab5 behavior remains blocked/safe for unsupported paths.
- Bringup proceeds incrementally in test envs, starting with scan-first validation before channel/promisc/raw TX.

