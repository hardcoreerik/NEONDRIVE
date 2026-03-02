# CYD 3.5 Port Plan and Implementation Notes

## What Changed
- Added an opt-in CYD 3.5 build environment: `firmware_cyd_3_5`.
- Kept existing CYD 2.4 build (`firmware_cyd_2_4`) unchanged in behavior and pin mapping.
- Added a CYD variant layer and compile-time routing:
  - `include/board_variant.h`
  - `include/variants/cyd24/board_variant.h`
  - `include/variants/cyd35/board_variant.h`
  - `include/variants/cyd35/cyd35_hw_config.h` (supports alternate 3.5 revisions)
- Added a dedicated TFT setup for CYD 3.5:
  - `include/User_Setup_cyd35.h`
- Added CYD 3.5 device profile:
  - `include/device_profiles/profile_cyd_3_5.h`
- Refactored shared init flow in `src/main.cpp` to use HAL-style entry points:
  - `display_init()`
  - `touch_init()`
  - `display_get_width()` / `display_get_height()`
  - `touch_read_point()`
- Added serial boot variant banner:
  - `[variant] active=CYD24` or `[variant] active=CYD35`
- Added optional validation overlay/test at boot:
  - Enabled by compile flag `ND_TOUCH_TEST_SCREEN=1`
  - Shows a simple screen and prints mapped/raw touch coords to serial.

## Build Commands
- CYD 2.4 (existing):
  - `pio run -e firmware_cyd_2_4`
- CYD 3.5 (new):
  - `pio run -e firmware_cyd_3_5`

## Flash Commands
- CYD 2.4:
  - `python -m platformio run -e firmware_cyd_2_4 -t upload --upload-port <COM>`
- CYD 3.5:
  - `python -m platformio run -e firmware_cyd_3_5 -t upload --upload-port <COM>`

## Variant Configuration Locations
- CYD 2.4 pins/touch params:
  - `include/variants/cyd24/board_variant.h`
- CYD 3.5 pins/touch params:
  - `include/variants/cyd35/board_variant.h`
- CYD 3.5 alternate hardware revision switch:
  - `include/variants/cyd35/cyd35_hw_config.h`
  - Use compile flag `-DNEONDRIVE_CYD35_REV_B` for alternate mapping.

## Serial Test Procedure
1. Build and flash `firmware_cyd_3_5`.
2. Open monitor: `python -m platformio device monitor -p <COM> -b 115200`.
3. Confirm boot prints:
   - device profile block
   - `[variant] active=CYD35`
4. If `ND_TOUCH_TEST_SCREEN=1` is enabled, tap the panel and verify:
   - on-screen coordinate updates
   - serial lines like: `[touch-test] x=... y=... raw=(..., ..., ...)`
5. If touch axis/inversion is wrong, adjust only CYD35 values in:
   - `include/variants/cyd35/board_variant.h`

## CYD 3.5 Flashing Notes (Post-Port)
- Resolved CYD35 touch clipping issue by fixing swapped-axis scaling in `touch_read_point()`:
  - when `swapXY` is enabled, raw axes are swapped before scaling so X still maps to 480 and Y to 320.
- CYD35 startup calibration is persisted to SD:
  - file: `/touch_cal_cyd35.json`
  - first boot runs wizard if file is missing.
  - to force recalibration: delete file and reboot.
