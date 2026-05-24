# Tab5 RF Stabilization Plan

## Architecture

M5Tab5 uses:
- **ESP32-P4**: application/UI CPU
- **ESP32-C6**: Wi-Fi/BLE co-processor

Because of this split, direct local `esp_wifi_*` call patterns from CYD-local implementations are unsafe on Tab5 unless explicitly hosted/backed by C6 transport.

## Stabilization Rules

1. Route RF capabilities through `neon_rf`.
2. On Tab5, unsupported RF operations must fail safely with clear logs.
3. Keep production Tab5 RF behavior conservative until test env bringup passes.
4. Keep CYD/local ESP32 behavior intact behind compile guards.

## Bringup Phases

1. Guard unsafe local RF calls in Tab5-reachable paths.
2. Add diagnostics:
   - reset reason
   - selected backend
   - capability summary
   - last RF action marker
3. Add host-side regression tests to prevent boundary regression.
4. Add scan-first C6 test flow in dedicated test envs.
5. Expand capabilities only after scan path is stable.

## Non-Goals (Current Phase)

- No custom C6 firmware implementation in this phase.
- No offensive feature expansion.
- No hardware-required tests for regression suite.

