# Claude Handoff: Tab5 Wi-Fi Bringup (C6 SDIO)

## Current Verified State
- `firmware_m5tab5` is flashed and boots normally on Tab5.
- A direct Wi-Fi test firmware was added to isolate platform behavior:
  - env: `firmware_m5tab5_wifi_direct`
  - file: `src/tab5_wifi_direct_test.cpp`
- Direct test uses official Tab5 scan flow (`M5.begin()`, `WiFi.mode(WIFI_STA)`, `WiFi.scanNetworks()`).

## Reproduced Failure (Hardware-verified)
On `COM4`, the direct test consistently fails before scan results:
- `E ... sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107`
- `E ... sdio_wrapper: sdmmc_card_init failed`
- `E ... H_SDIO_DRV: sdio card init failed`
- `FreeRTOS Task "sdio_read" should not return, Aborting now!`

This confirms the blocker is C6 SDIO transport/firmware state, not NEONDRIVE UI scan logic.

## What Was Added for Bringup
- `include/tab5_sdkconfig_override.h`
  - Compile-time SDIO pin override to official Tab5 map:
  - CLK=12 CMD=13 D0=11 D1=10 D2=9 D3=8 (RST=15 noted)
- `platformio.ini`
  - include override in Tab5 base flags
  - add env `firmware_m5tab5_wifi_direct`
  - guard direct test with `-DTAB5_WIFI_DIRECT_TEST=1`
- `src/tab5_wifi_direct_test.cpp`
  - Direct probe firmware (isolated setup/loop)

## Task to Complete
Get Wi-Fi scan working on M5Tab5 by resolving C6 SDIO/C6 firmware readiness, then validate scan end-to-end:
1. C6 transport initializes without SDIO task abort.
2. `firmware_m5tab5_wifi_direct` returns AP count reliably.
3. `firmware_m5tab5_c6_scan` shows APs on Wi-Fi scan screen.

## Suggested Immediate Path
1. Run official M5 Tab5 C6 firmware restore/upgrade path.
2. Re-test with `firmware_m5tab5_wifi_direct` first (lowest complexity).
3. Once direct test works, re-enable/continue NEONDRIVE scan-test path (`firmware_m5tab5_c6_scan`).

