# NEONDRIVE — LilyGO T-Embed CC1101 Plus HAL Review

**Branch:** `claude/t-embed-hal-bringup`  
**Date:** 2026-05-25  
**Reviewer:** Claude Code (automated, surgical review — no firmware changes in this pass)  
**Official source:** https://github.com/Xinyuan-LilyGO/T-Embed-CC1101  
**Canonical pin file:** `examples/factory_test/utilities.h` (no `pin_config.h` exists in the repo)

---

## 1. Executive Summary

### Current status

The T-Embed CC1101 target compiles but will **not function correctly on hardware**.  The pin
definitions in `User_Setup_tembed_cc1101.h` and in the `NEONDRIVE_TARGET_TEMBED` block of
`main.cpp` are **completely wrong** — they appear to have been guessed or copied from a different
board rather than sourced from the official LILYGO repository.  Every TFT SPI pin is incorrect,
and the encoder A/B pins are mapped to the IR TX-enable and IR RX GPIOs.

### What already exists (good foundation)

| Area | Status |
|---|---|
| `platformio.ini` env chain | Structurally sound; `extends env:cyd` is reasonable for now |
| `device_profile_select.h` routing | Correct — `NEONDRIVE_TARGET_T_EMBED_CC1101` maps to `profile_t_embed_cc1101.h` |
| `profile_t_embed_cc1101.h` — display dimensions | **Correct** — `ND_DISPLAY_W 320 / ND_DISPLAY_H 170` (landscape) |
| `profile_t_embed_cc1101.h` — touch/encoder/CC1101 | Correct flags — no touch, encoder present, CC1101 present |
| `main.cpp` encoder read path | `PIN_ENCODER_A`/`B` are wired to the encoder read block |
| `main.cpp` encoder navigation | Side key and encoder button mapped (logic is correct, pins are wrong) |
| `hal_cyd.cpp` WiFi/keyboard stubs | Safe no-ops; ESP32-S3 on-chip WiFi works identically to CYD |

### What is likely correct (needs hardware confirmation)

- `TFT_BL = 21` — matches official `DISPLAY_BL = GPIO 21` ✓
- `PIN_NAV_NEXT = 6` (`BOARD_USER_KEY`) — matches official GPIO 6 ✓
- `PIN_NAV_SELECT = 0` (`ENCODER_KEY`) — matches official GPIO 0 ✓ (shared with BOOT button)
- `TFT_INVERSION_ON` — matches official `TFT_INVERSION_ON` ✓
- `BOARD_HAS_TOUCH = false` ✓
- `BOARD_TFT_INVERT = true` ✓

### What is wrong / suspicious

| Item | Problem |
|---|---|
| `TFT_MOSI = 42` | Should be GPIO 9 — currently maps to `BOARD_MIC_DATA` |
| `TFT_SCLK = 40` | Should be GPIO 11 — currently maps to `BOARD_VOICE_LRCLK` |
| `TFT_CS = 45` | Should be GPIO 41 (`DISPLAY_CS`) |
| `TFT_DC = 41` | Should be GPIO 16 |
| `TFT_RST = 47` | Should be GPIO 40 (`DISPLAY_RST`) — currently maps to `BOARD_LORA_SW0` |
| `PIN_TFT_SCLK/MOSI/CS` in main.cpp | Same wrong values as User_Setup; will init SPI on audio/antenna GPIOs |
| `PIN_ENCODER_A = 1` | Should be GPIO 4 — currently maps to `BOARD_IR_RX` |
| `PIN_ENCODER_B = 2` | Should be GPIO 5 — currently maps to `BOARD_IR_EN` (TX enable) |
| `SPI_FREQUENCY = 40 MHz` | Official uses 80 MHz |
| `BOARD_HAS_SD = false` | SD card IS present on hardware (CS = GPIO 13); should be enabled |
| `PIN_SD_*` all `-1` | Should be SCK=11, MISO=10, MOSI=9, CS=13 (shared bus) |
| `hal_cyd.cpp` stale size check | Dead branch `#elif defined(NEONDRIVE_TARGET_TEMBED)` returns 240×135; macro only exists inside main.cpp, never seen by hal_cyd.cpp |
| Missing USB serial flags | `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE=1` absent — serial monitor may not work |
| Missing `RadioLib` in lib_deps | CC1101 driver not in the env; `ND_HW_CC1101 1` has nothing to call |
| Missing encoder library | `mathertel/RotaryEncoder` not in lib_deps; raw GPIO polling exists but a debounce library is advisable |
| `BOARD_PWR_EN` (GPIO 15) never set | Must be HIGH to power CC1101 + WS2812 LEDs; no init code exists |

### What is missing for Plus / nRF24 support

- `ND_HW_NRF24` capability flag (nRF24 CE=43, CS=44, shared SPI bus)
- `ND_HW_IR_TX` / `ND_HW_IR_RX` capability flags (TX-enable=GPIO 2, RX=GPIO 1)
- `ND_HW_WS2812` / LED ring support (8× WS2812B on GPIO 14)
- No HAL entry point for power-enable initialization
- SPI bus mutex/CS management (display + CC1101 + SD + nRF24 all share GPIO 9/10/11)

---

## 2. Current NEONDRIVE Path Map

### PlatformIO env chain

```
[env:firmware_t_embed_cc1101]
  └─ extends [env:lilygo_t_embed_cc1101]
       └─ extends [env:cyd]
            ├─ platform = espressif32@6.12.0
            ├─ board = lilygo-t-display-s3      ← ESP32-S3 board descriptor, acceptable
            ├─ board_build.partitions = default_16MB.csv
            ├─ lib_deps = TFT_eSPI, XPT2046_Touchscreen, ArduinoJson
            └─ build_flags:
                 -DNEONDRIVE_TARGET_T_EMBED_CC1101=1
                 -DBOARD_HAS_PSRAM=1
                 -include include/User_Setup_tembed_cc1101.h
                 (inherits -Os, -DFEATURE_ANDROID_OFFLOAD=1, -DCORE_DEBUG_LEVEL=0 etc)
```

**Missing from env:**
- `ARDUINO_USB_CDC_ON_BOOT=1`
- `ARDUINO_USB_MODE=1`
- `RadioLib` library
- `mathertel/RotaryEncoder` (optional)
- `build_src_filter` exclusions (same issue as other non-CYD targets — `wsl_bypasser.cpp`,
  `bruce_wifi.cpp` may pull in CYD-only SPI paths)

### Compile flag chain for T-Embed

```
NEONDRIVE_TARGET_T_EMBED_CC1101=1       ← from platformio.ini
  → main.cpp line 26-27: #define NEONDRIVE_TARGET_TEMBED 1   (local alias)
  → NEONDRIVE_TARGET_BUTTON_NAV=1      ← main.cpp line 17-18
  → NEONDRIVE_TARGET_CYD is NOT set    ← correctly excluded (lines 33-35)
```

### Profile selection path

```
device_profile_select.h
  → #elif defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
  → include "device_profiles/profile_t_embed_cc1101.h"
```

### TFT setup path

```
platformio.ini  -include include/User_Setup_tembed_cc1101.h
  → TFT_eSPI reads this at compile time for SPI pin/driver config
  → USER_SETUP_LOADED=1 must be set (inherited from env:cyd base ✓)
```

### HAL / setup files involved

| File | Role for T-Embed |
|---|---|
| `src/hal/hal_cyd.cpp` | Compiled for T-Embed (catch-all `#else` condition line 14) |
| `src/hal/neon_hal.h` | Interface; `NeonKey` enum used for encoder |
| `src/main.cpp` lines 247–283 | Pin constants block for `NEONDRIVE_TARGET_TEMBED` |
| `src/main.cpp` lines 4909–4920 | Encoder read (raw GPIO, no library) |

### UI / input files involved

| File | Relevance |
|---|---|
| `src/main.cpp` — `NEONDRIVE_TARGET_BUTTON_NAV` blocks | Navigation input (encoder maps here) |
| `src/main.cpp` — touch read path (guarded by `!NEONDRIVE_TARGET_BUTTON_NAV`) | Skipped for T-Embed ✓ |
| `src/hal/hal_cyd.cpp` — `neon_hal_display_size()` | Returns 240×320 for T-Embed (wrong — see §5) |

---

## 3. Verified Hardware Facts

| Subsystem | Value | Source | Confidence |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1 | README.md, LILYGO repo | Verified |
| Flash | 16 MB | utilities.h, README | Verified |
| PSRAM | 8 MB OPI | utilities.h, README | Verified |
| Display | ST7789 | Setup214, factory_test | Verified |
| Display native resolution | 170 × 320 (portrait) | Setup214, TFT_WIDTH/HEIGHT | Verified |
| Display landscape | 320 × 170 | Profile matches rotation=1 | Verified |
| Touch | None | README, no touch code | Verified |
| Sub-GHz radio | CC1101 (not LoRa) | factory_test, cc1101_recv.ino | Verified |
| Rotary encoder | Present | encode_test.ino, utilities.h | Verified |
| IR TX/RX | Present | factory_test, infrared_*.ino | Verified |
| WS2812 RGB LEDs | 8× on GPIO 14 | ws2812_test.ino | Verified |
| SD card | Present (SPI, CS=GPIO 13) | tf_card_test.ino | Verified |
| nRF24L01 | External module (Plus variant) | extend_nrf2401 examples | Verified |
| NFC (PN532) | Present (I2C 0x24) | pn532_test.ino | Verified |
| Battery | 3.7V 1300 mAh | README | Verified |
| Battery management | BQ25896 charger (I2C 0x6B) + BQ27220 gauge (I2C 0x55) | utilities.h | Verified |
| Battery ADC GPIO | None — data via BQ27220 I2C | utilities.h (no BAT_ADC define) | Verified |
| Microphone | I2S PDM (DATA=42, CLK=39) | utilities.h | Verified |
| Speaker / I2S out | BCLK=46, LRCLK=40, DIN=7 | utilities.h | Verified |
| Power enable | GPIO 15 HIGH = CC1101+WS2812 on | utilities.h `BOARD_PWR_EN` | Verified |

---

## 4. Pin Map

All GPIOs confirmed from `examples/factory_test/utilities.h` and
`lib/TFT_eSPI/User_Setups/Setup214_LilyGo_T_Embed_PN532.h` in the official repo.

### Display (ST7789)

| Function | GPIO | Source | In NEONDRIVE? | Fix needed? |
|---|---|---|---|---|
| TFT MOSI | 9 | utilities.h `BOARD_SPI_MOSI` | **NO** (has 42) | **YES** |
| TFT MISO | 10 (shared, no display read) | utilities.h `BOARD_SPI_MISO` | OK (-1 in User_Setup) | Minor: shared bus note |
| TFT SCLK | 11 | utilities.h `BOARD_SPI_SCK` | **NO** (has 40) | **YES** |
| TFT CS | 41 | utilities.h `DISPLAY_CS` | **NO** (has 45) | **YES** |
| TFT DC | 16 | Setup214 `TFT_DC` | **NO** (has 41) | **YES** |
| TFT RST | 40 | utilities.h `DISPLAY_RST` | **NO** (has 47) | **YES** |
| TFT BL | 21 | utilities.h `DISPLAY_BL` | Yes (21) | No |
| SPI frequency | 80 MHz | Setup214 `SPI_FREQUENCY` | **NO** (40 MHz) | **YES** |

### CC1101 Sub-GHz Radio

| Function | GPIO | Source | In NEONDRIVE? | Fix needed? |
|---|---|---|---|---|
| CC1101 CS | 12 | utilities.h `BOARD_LORA_CS` | No (not defined) | Yes — for radio driver |
| CC1101 GDO0 (IRQ) | 3 | utilities.h `BOARD_LORA_IO0` | No | Yes |
| CC1101 GDO2 | 38 | utilities.h `BOARD_LORA_IO2` | No | Yes |
| CC1101 MOSI | 9 | utilities.h `BOARD_SPI_MOSI` | No (wrong MOSI) | Yes (same fix as display) |
| CC1101 MISO | 10 | utilities.h `BOARD_SPI_MISO` | No | Yes |
| CC1101 SCK | 11 | utilities.h `BOARD_SPI_SCK` | No | Yes |
| Antenna SW1 | 47 | utilities.h `BOARD_LORA_SW1` | No | Yes — needed for band select |
| Antenna SW0 | 48 | utilities.h `BOARD_LORA_SW0` | No | Yes |
| Power enable | 15 | utilities.h `BOARD_PWR_EN` | **No** | **Yes** — must set HIGH at boot |

### nRF24L01 Plus Expansion Module

| Function | GPIO | Source | In NEONDRIVE? | Fix needed? |
|---|---|---|---|---|
| nRF24 CE | 43 | utilities.h `BOARD_NRF24_CE` | No | Yes (Phase 3) |
| nRF24 CS (CSN) | 44 | utilities.h `BOARD_NRF24_CS` | No | Yes (Phase 3) |
| nRF24 IRQ | -1 (not connected) | utilities.h `BOARD_NRF24_IRQ = -1` | N/A | No |
| nRF24 MOSI | 9 | shared SPI bus | No | Yes (Phase 3) |
| nRF24 MISO | 10 | shared SPI bus | No | Yes (Phase 3) |
| nRF24 SCK | 11 | shared SPI bus | No | Yes (Phase 3) |

### Rotary Encoder

| Function | GPIO | Source | In NEONDRIVE? | Fix needed? |
|---|---|---|---|---|
| Encoder A | 4 | utilities.h `ENCODER_INA` | **NO** (has 1) | **YES** |
| Encoder B | 5 | utilities.h `ENCODER_INB` | **NO** (has 2) | **YES** |
| Encoder button | 0 | utilities.h `ENCODER_KEY` | Yes (0) | No |
| Side user key | 6 | utilities.h `BOARD_USER_KEY` | Yes (6) | No |

### IR Transceiver

| Function | GPIO | Source | In NEONDRIVE? | Fix needed? |
|---|---|---|---|---|
| IR TX enable | 2 | utilities.h `BOARD_IR_EN` | **NO** — used as `PIN_ENCODER_B=2` | **YES** (encoder pin fix frees it) |
| IR RX | 1 | utilities.h `BOARD_IR_RX` | **NO** — used as `PIN_ENCODER_A=1` | **YES** |

### SD Card

| Function | GPIO | Source | In NEONDRIVE? | Fix needed? |
|---|---|---|---|---|
| SD CS | 13 | utilities.h `BOARD_SD_CS` | **NO** (has -1) | Yes (Phase 2) |
| SD MOSI | 9 | shared SPI bus | **NO** | Yes |
| SD MISO | 10 | shared SPI bus | **NO** | Yes |
| SD SCK | 11 | shared SPI bus | **NO** | Yes |

### I2C Bus

| Function | GPIO | Source | In NEONDRIVE? | Fix needed? |
|---|---|---|---|---|
| I2C SDA | 8 | utilities.h `BOARD_I2C_SDA` | No | Phase 3+ |
| I2C SCL | 18 | utilities.h `BOARD_I2C_SCL` | No | Phase 3+ |

### Other Notable Pins

| Function | GPIO | Source | In NEONDRIVE? | Fix needed? |
|---|---|---|---|---|
| WS2812 data | 14 | utilities.h `WS2812_DATA_PIN` | No | Phase 5+ |
| Microphone DATA | 42 | utilities.h `BOARD_MIC_DATA` | **COLLISION** — NEONDRIVE uses 42 for TFT_MOSI | **YES** (TFT pin fix resolves) |
| Microphone CLK | 39 | utilities.h `BOARD_MIC_CLK` | No | N/A for NEONDRIVE |
| Speaker BCLK | 46 | utilities.h `BOARD_VOICE_BCLK` | No | N/A |
| Speaker LRCLK | 40 | utilities.h `BOARD_VOICE_LRCLK` | **COLLISION** — NEONDRIVE uses 40 for TFT_SCLK | **YES** (TFT pin fix resolves) |

---

## 5. Existing NEONDRIVE Profile Comparison

### `include/device_profiles/profile_t_embed_cc1101.h`

| Field | NEONDRIVE current | Official / Required | Status |
|---|---|---|---|
| `ND_DISPLAY_W` | 320 | 320 (landscape) | ✓ Correct |
| `ND_DISPLAY_H` | 170 | 170 (landscape) | ✓ Correct |
| `ND_HW_TOUCH` | 0 | 0 | ✓ Correct |
| `ND_HW_BUTTON_NAV` | 1 | 1 (side key GPIO 6) | ✓ Correct |
| `ND_HW_ENCODER` | 1 | 1 | ✓ Correct |
| `ND_HW_CC1101` | 1 | 1 | ✓ Correct |
| `ND_HW_SD` | **0** | **1** (SD present, CS=13) | ✗ Wrong |
| `ND_FLASH_MB` | 16 | 16 | ✓ Correct |
| `ND_EXPECT_PSRAM` | 1 | 1 (8 MB OPI) | ✓ Correct |
| `ND_FEATURE_SUBGHZ_TOOLKIT` | 1 | 1 | ✓ Correct |
| `ND_HW_NRF24` | not defined | Plus: 1 (CE=43, CS=44) | ✗ Missing |
| `ND_HW_IR_TX` | not defined | 1 (GPIO 2) | ✗ Missing |
| `ND_HW_IR_RX` | not defined | 1 (GPIO 1) | ✗ Missing |
| `ND_HW_WS2812` | not defined | 1 (8× GPIO 14) | ✗ Missing |
| `ND_HW_ROTARY_BUTTON` | not defined | 1 (GPIO 0) | ✗ Missing (ENTER implicit) |

### `include/User_Setup_tembed_cc1101.h`

Every SPI pin is wrong. Pins in this file appear to have been guessed rather than sourced from
the official repository.

| Field | NEONDRIVE current | Official | Status |
|---|---|---|---|
| Driver | `ST7789_DRIVER` | ST7789 | ✓ |
| `TFT_WIDTH` | 170 | 170 | ✓ |
| `TFT_HEIGHT` | 320 | 320 | ✓ |
| `TFT_MISO` | -1 | -1 (MISO not used for display) | ✓ |
| `TFT_MOSI` | **42** | **9** (`BOARD_SPI_MOSI`) | ✗ Wrong |
| `TFT_SCLK` | **40** | **11** (`BOARD_SPI_SCK`) | ✗ Wrong |
| `TFT_CS` | **45** | **41** (`DISPLAY_CS`) | ✗ Wrong |
| `TFT_DC` | **41** | **16** | ✗ Wrong |
| `TFT_RST` | **47** | **40** (`DISPLAY_RST`) | ✗ Wrong |
| `TFT_BL` | 21 | 21 (`DISPLAY_BL`) | ✓ |
| `TFT_BACKLIGHT_ON` | HIGH | HIGH | ✓ |
| `TFT_INVERSION_ON` | defined | defined | ✓ |
| `TFT_RGB_ORDER TFT_RGB` | TFT_RGB | needs verification | Inferred |
| `CGRAM_OFFSET` | defined | defined (Setup214 omits this; verify) | Inferred |
| `SPI_FREQUENCY` | **40 MHz** | **80 MHz** | ✗ Wrong |
| `INIT_SEQUENCE_3` | defined | not in Setup214 (official omits this) | Suspicious |

### `platformio.ini` — `env:lilygo_t_embed_cc1101`

| Item | Current | Required | Status |
|---|---|---|---|
| `board` | `lilygo-t-display-s3` | Same or `esp32-s3-devkitc-1` | Acceptable (both ESP32-S3 16MB) |
| `ARDUINO_USB_CDC_ON_BOOT=1` | Missing | Required for USB serial | ✗ Missing |
| `ARDUINO_USB_MODE=1` | Missing | Required for USB serial | ✗ Missing |
| `RadioLib` in lib_deps | Missing | `jgromes/RadioLib@6.5.0` | ✗ Missing |
| `mathertel/RotaryEncoder` | Missing | Recommended | ✗ Missing |
| `build_src_filter` | Missing | Should exclude `wsl_bypasser.cpp` / `bruce_wifi.cpp` | ✗ Missing |

### `src/hal/hal_cyd.cpp` — display size bug

The `neon_hal_display_size()` function has a dead branch:

```cpp
#elif defined(NEONDRIVE_TARGET_TEMBED)
    // LilyGO T-Embed-CC1101 — ST7789 240×135
    if (width)  *width  = 240;
    if (height) *height = 135;
```

This branch is **unreachable** — `NEONDRIVE_TARGET_TEMBED` is only `#define`d inside
`main.cpp` as a local alias; it is never set as a compiler flag, so `hal_cyd.cpp` never
sees it.  As a result, the T-Embed falls into the `#else` branch and returns **240×320**
(CYD 2.4" dimensions), not 320×170.

Even if the macro were set correctly, the value **240×135** is wrong for the T-Embed
(1.9" panel native resolution is 170×320, landscape is 320×170).

Fix: add a proper `NEONDRIVE_TARGET_T_EMBED_CC1101` branch in `neon_hal_display_size()`
returning 320×170, or create `src/hal/hal_t_embed_cc1101.cpp` (preferred for Phase 4).

---

## 6. Missing Capability Flags

### Recommended additions to `include/device_profiles/profile_t_embed_cc1101.h`

```cpp
// ── Plus variant additions ────────────────────────────────────────────────────
#define ND_HW_NRF24         1   // nRF24L01 expansion module (CE=43, CS=44, shared SPI 9/10/11)
#define ND_HW_IR_TX         1   // IR transmitter enable (GPIO 2)
#define ND_HW_IR_RX         1   // IR receiver (GPIO 1)
#define ND_HW_WS2812        1   // 8× WS2812B RGB LED ring (GPIO 14)
#define ND_HW_ROTARY_BUTTON 1   // Encoder push button (GPIO 0 = ENCODER_KEY = BOOT)

// Battery state available via BQ27220 fuel gauge over I2C — no direct ADC pin.
// Feature code should use I2C 0x55 reads, not analogRead().
#define ND_HW_BATTERY_ADC   0
```

### Safety defaults to add to `include/device_profile_select.h`

```cpp
#ifndef ND_HW_NRF24
#define ND_HW_NRF24 0
#endif
#ifndef ND_HW_IR_TX
#define ND_HW_IR_TX 0
#endif
#ifndef ND_HW_IR_RX
#define ND_HW_IR_RX 0
#endif
#ifndef ND_HW_WS2812
#define ND_HW_WS2812 0
#endif
#ifndef ND_HW_ROTARY_BUTTON
#define ND_HW_ROTARY_BUTTON 0
#endif
#ifndef ND_HW_BATTERY_ADC
#define ND_HW_BATTERY_ADC 0
#endif
```

### Profile naming — Plus vs base

Keep the profile file as `profile_t_embed_cc1101.h` (no rename) but set `ND_HW_NRF24=1`
in it.  The nRF24 is an add-on expansion module; the base CC1101 firmware is a strict
superset of the non-Plus firmware.  If NEONDRIVE ever needs to target the base variant
(without nRF24) separately, introduce a `ND_HW_NRF24=0` override in platformio.ini via
a second env, not by renaming the profile file.

---

## 7. HAL Integration Recommendation

### Current structure (T-Embed falls into hal_cyd.cpp)

```
hal_cyd.cpp  guards:  !M5TAB5 && !M5CARDPUTER && !TDISPLAY_S3
  ├─ neon_hal_display_size()  → returns 240×320 for T-Embed (WRONG)
  ├─ neon_hal_ui_metrics()    → returns CYD 2.4/2.8 values (undersized for 320×170)
  ├─ neon_hal_wifi_init()     → WiFi.mode(STA)  ← correct, no change needed
  ├─ neon_hal_touch_get()     → returns {false} stub  ← correct
  └─ neon_hal_key_get()       → returns NONE stub  ← needs encoder integration
```

### Recommended path (minimal, surgical)

**Option A — dedicated HAL file (preferred):**

Create `src/hal/hal_t_embed_cc1101.cpp` with the T-Embed-specific overrides:
- Correct `neon_hal_display_size()` → 320×170
- Correct `neon_hal_ui_metrics()` → compact 320×170 values
- `neon_hal_key_get()` → poll encoder A/B/button GPIOs (or RotaryEncoder library)
- WiFi, touch stubs are identical to CYD — can delegate or copy

Update `hal_cyd.cpp` guard to also exclude T-Embed:

```cpp
// Current:
#if !defined(NEONDRIVE_TARGET_M5TAB5) && !defined(NEONDRIVE_TARGET_M5CARDPUTER) && !defined(NEONDRIVE_TARGET_TDISPLAY_S3)

// Proposed:
#if !defined(NEONDRIVE_TARGET_M5TAB5) && !defined(NEONDRIVE_TARGET_M5CARDPUTER) \
 && !defined(NEONDRIVE_TARGET_TDISPLAY_S3) && !defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
```

**Option B — fix hal_cyd.cpp in place (minimal diff):**

Add `NEONDRIVE_TARGET_T_EMBED_CC1101` branches inside each function in `hal_cyd.cpp`.
Smaller diff, no new file, but hal_cyd.cpp grows to cover two unrelated boards.
Acceptable short-term since the CYD and T-Embed share the same WiFi stack path.

### Where board-specific init belongs

T-Embed needs a one-time power-enable pulse before any SPI peripheral works:

```cpp
// In setup(), before SPI.begin() / tft.init():
#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
  pinMode(BOARD_PWR_EN, OUTPUT);
  digitalWrite(BOARD_PWR_EN, HIGH);  // powers CC1101 + WS2812
  delay(50);
#endif
```

This does not exist anywhere in NEONDRIVE today.  Without it, the CC1101 will be
unpowered and the WS2812 will be dead.  Add this to the existing board-init block in
`setup()` around the other `NEONDRIVE_TARGET_TEMBED` guards.

### Where display init happens

`tft.init()` and `tft.setRotation()` are called in `setup()` guarded under
`TFT_USES_SPI_BUS`.  The T-Embed has `BOARD_HAS_SD = false` currently so the SD SPI
init block is skipped — after pin fixes this will need `BOARD_HAS_SD = true` and
correct SD pins to enable logging.

### Where radio setup belongs

In `src/neon_rf.cpp` / `neon_rf_init()`.  The CC1101 init should be gated on
`ND_HW_CC1101 == 1` (already the profile flag).  The RadioLib `CC1101` instance needs
the antenna switch pins (GPIO 47/48) set before `radio.begin()`.

### SPI bus sharing — critical note

All four SPI devices (ST7789 CS=41, CC1101 CS=12, SD CS=13, nRF24 CS=44) share
GPIO 9/10/11.  The official LILYGO examples use a `board_spi_deselect_all()` pattern
that drives all CS lines HIGH before any `SPI.begin()` or peripheral swap.  NEONDRIVE
currently has no such guard.  Failure to do this can cause the display init to corrupt
CC1101 registers or vice versa.  This must be addressed in Phase 2.

---

## 8. UI Adaptation Notes

### 320×170 landscape constraints

The T-Embed display is **narrower than any current NEONDRIVE target** in the vertical
axis (170 px vs 240 px on CYD).  The existing CYD 2.4/2.8 HAL UI values delivered
via `neon_hal_ui_metrics()` are:

```
header_h = 30, bottom_bar_h = 36, btn_h = 30, row_h = 16
```

Total reserved vertical space: 30 + 36 = 66 px, leaving only 104 px for content.
This is tight but workable at `text_size=1` (8 px per line).  A dedicated T-Embed
`neon_hal_ui_t` should use:

```cpp
static const neon_hal_ui_t s_tembed_ui = {
    /* safe_margin  */ 4,
    /* top_gap      */ 4,
    /* header_h     */ 20,
    /* bottom_bar_h */ 20,
    /* btn_h        */ 20,
    /* btn_gap      */ 4,
    /* row_h        */ 12,
    /* pad          */ 3,
    /* border_r     */ 4,
    /* fonts        */ nullptr (all),
    /* text_size_sm */ 1, /* text_size_md */ 1, /* text_size_lg */ 2,
    ...
};
```

### Rotary-first navigation

All menu navigation is via encoder rotation (prev/next) and button (select), with the
side key (GPIO 6) as back/cancel.  The existing `NEONDRIVE_TARGET_BUTTON_NAV` code path
in `main.cpp` drives navigation via `PIN_NAV_NEXT` and `PIN_NAV_SELECT` — the T-Embed
should map through this same path.  Encoder A/B rotation maps to repeated `PIN_NAV_NEXT`
/ `PIN_NAV_PREV` fire events (not currently abstracted in HAL).

`NeonKey::UP` / `NeonKey::DOWN` should be returned from `neon_hal_key_get()` as the
encoder turns; `NeonKey::ENTER` for encoder button press; `NeonKey::BACK` for the side
key.  This follows the same model as the Cardputer keyboard HAL.

### No-touch behavior

T-Embed has `ND_HW_TOUCH=0` and `BOARD_HAS_TOUCH=false`.  The XPT2046 header is
excluded by the `NEONDRIVE_TARGET_BUTTON_NAV` guard at line 48–50 in main.cpp.
No changes needed here.

### Screens needing compact layout

The following screens are likely to overflow 170 px vertical at current CYD metrics
and will need layout review when porting:

- WiFi scan list (row height × many networks)
- Config screen (large form items)
- Web UI QR code overlay (fixed-size QR will be too large)
- Home screen menu (may need smaller icons or single-column)

### Screens that reuse existing behavior

- Network detail view, deauth hunter, PMKID/yoink screens, port scanner — all use list
  rendering that will adapt to reduced row_h automatically.
- Serial log / diagnostic screen — text_size=1 works fine at any height.

---

## 9. Build / Test Plan

### Regression builds (must stay green before and after any changes)

```powershell
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_cyd_2_4
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_cyd_2_8
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_cyd_3_5
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_t_display_s3
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_m5tab5
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_cardputer_adv
```

### T-Embed build (Phase 2 target)

```powershell
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_t_embed_cc1101
```

### T-Embed flash

```powershell
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_t_embed_cc1101 -t upload --upload-port COMx
```

### Boot verification checklist (after Phase 2 hardware flash)

```
[serial] 115200 baud, USB CDC (requires ARDUINO_USB_CDC_ON_BOOT=1)
Expected boot log:
  [tembed] GPIO 15: BOARD_PWR_EN=HIGH
  [tembed] SPI bus: MOSI=9, MISO=10, SCK=11
  [tembed] display CS=41, CC1101 CS=12, SD CS=13
  [tembed] display init OK
  [tembed] encoder A=4 B=5 BTN=0 KEY=6
  [tembed] CC1101 init ... OK / FAIL (not in NEONDRIVE yet, stub logs acceptable)
```

---

## 10. Proposed Implementation Order

### Phase 1 — Verify pins (this document) ✅ Complete

- [x] Cross-reference official LILYGO repository
- [x] Identify all wrong pin values in NEONDRIVE
- [x] Document shared SPI bus conflict
- [x] Document missing power-enable init
- [x] Produce this report

### Phase 2 — Fix profile and setup definitions

**Files to edit:**

1. `include/User_Setup_tembed_cc1101.h`
   - TFT_MOSI: 42 → **9**
   - TFT_SCLK: 40 → **11**
   - TFT_CS: 45 → **41**
   - TFT_DC: 41 → **16**
   - TFT_RST: 47 → **40**
   - SPI_FREQUENCY: 40000000 → **80000000**
   - Remove `INIT_SEQUENCE_3` (not in official Setup214; likely causes wrong init)

2. `src/main.cpp` — `NEONDRIVE_TARGET_TEMBED` pin block (lines ~247–283)
   - PIN_TFT_SCLK: 40 → **11**
   - PIN_TFT_MOSI: 42 → **9**
   - PIN_TFT_CS: 45 → **41**
   - PIN_ENCODER_A: 1 → **4**
   - PIN_ENCODER_B: 2 → **5**
   - BOARD_HAS_SD: false → **true**
   - PIN_SD_SCK: -1 → **11**
   - PIN_SD_MISO: -1 → **10**
   - PIN_SD_MOSI: -1 → **9**
   - PIN_SD_CS: -1 → **13**
   - Add `PIN_PWR_EN = 15` (new)
   - Add `PIN_CC1101_CS = 12`, `PIN_CC1101_GDO0 = 3`, `PIN_CC1101_GDO2 = 38` (new)
   - Add `PIN_ANT_SW1 = 47`, `PIN_ANT_SW0 = 48` (new)

3. `src/main.cpp` — setup() board init block
   - Add power-enable HIGH pulse before SPI.begin() for T-Embed

4. `platformio.ini` — `env:lilygo_t_embed_cc1101`
   - Add `ARDUINO_USB_CDC_ON_BOOT=1`, `ARDUINO_USB_MODE=1` to build_flags
   - Add `jgromes/RadioLib@6.5.0` to lib_deps

5. `include/device_profiles/profile_t_embed_cc1101.h`
   - `ND_HW_SD`: 0 → **1**

6. `src/hal/hal_cyd.cpp` — `neon_hal_display_size()`
   - Fix dead branch: change `NEONDRIVE_TARGET_TEMBED` to `NEONDRIVE_TARGET_T_EMBED_CC1101`
   - Fix wrong dimensions: 240×135 → **320×170**

### Phase 3 — Add nRF24 capability flag and safe stubs

- Add `ND_HW_NRF24` to `profile_t_embed_cc1101.h` (= 1)
- Add safety default `#ifndef ND_HW_NRF24` to `device_profile_select.h`
- Add PIN_NRF24_CE=43, PIN_NRF24_CS=44 to main.cpp T-Embed block
- Add compile-guarded stub in `neon_rf.cpp` for nRF24 init (`#if ND_HW_NRF24`)
- No actual nRF24 feature code yet — stubs only

### Phase 4 — Add rotary encoder input through existing HAL

- Add `ND_HW_IR_TX`, `ND_HW_IR_RX`, `ND_HW_WS2812`, `ND_HW_ROTARY_BUTTON` flags
- Create `src/hal/hal_t_embed_cc1101.cpp`
  - Implement `neon_hal_display_size()` → 320×170
  - Implement `neon_hal_ui_metrics()` → compact T-Embed values
  - Implement `neon_hal_key_get()` → encoder A/B/button polling → NeonKey::UP/DOWN/ENTER/BACK
  - Stub WiFi (identical to hal_cyd.cpp — can share or copy)
  - Stub touch (return {false})
- Update `hal_cyd.cpp` guard to exclude T-Embed
- Add `mathertel/RotaryEncoder@^1.5.3` to platformio.ini lib_deps

### Phase 5 — Add compact no-touch UI behavior

- Tune T-Embed HAL ui_metrics values (header_h=20, row_h=12 etc.)
- Test all main menu screens on physical hardware at 320×170
- Identify any overflow screens and add compact layout branches
- Verify encoder navigation through full menu tree

### Phase 6 — Enable radio feature screens behind capability flags

- Enable sub-GHz toolkit UI under `ND_FEATURE_SUBGHZ_TOOLKIT=1` (already set)
- Wire `PIN_CC1101_CS`, `GDO0`, `GDO2`, `ANT_SW0/1` into `neon_rf_init()` for T-Embed
- Implement antenna band-select logic (SW1/SW0 control per official examples)
- Test CC1101 receive at 433/868/915 MHz on hardware
- nRF24 feature screen under `ND_HW_NRF24` guard (if user requests)
- IR feature screen under `ND_HW_IR_TX`/`ND_HW_IR_RX` guards (if user requests)

---

*End of review — no firmware changes were made in this pass.*
