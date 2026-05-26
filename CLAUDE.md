# NEONDRIVE — Codex / Claude Code Reference

This file is read automatically at the start of every Codex and Claude Code session.
Keep it accurate. When you fix a hardware bug, document the root cause here.

---

## M5Stack Tab5 — Touch Implementation

> **Read this before touching any Tab5 touch, display, or SD code.**

### How touch works on Tab5 (correct mental model)

The Tab5 uses a **Goodix GT911** capacitive touch controller connected to the
ESP32-P4 over I²C (SDA = GPIO31, SCL = GPIO32).  The GT911 is **not** wired to
any pin that application code manages directly.  Everything — PI4IOE I/O-expander
reset pulse, GT911 detection, coordinate mapping — is handled inside
`M5.begin()` via **M5GFX autodetect**.

The entire touch init is exactly two things:

```cpp
// 1. In setup(): M5.begin() does everything.
M5.begin(cfg);          // PI4IOE reset, GT911 detect, coord mapping — all here.

// 2. In loop(): pump the touch state machine once per frame.
M5.update();            // must be called before any touch read
```

Reading a touch point:

```cpp
auto td = M5.Touch.getDetail(0);   // index 0 = first finger
if (td.isPressed() || td.wasPressed()) {
    int x = td.x;
    int y = td.y;
    // ... handle tap
}
```

That is the complete implementation.  Nothing else is needed.

---

### What NOT to do

| Anti-pattern | Why it breaks things |
|---|---|
| Call `M5.Touch.begin()` manually | M5GFX already ran this inside `M5.begin()`; calling it again corrupts the state |
| Pulse the PI4IOE I/O expander manually (bit-toggle GPIO) | M5GFX uses a full 8-bit OUT\_SET register write sequence; manual bit-banging leaves LCD-reset bits floating and prevents GT911 from completing its reset |
| Call `tft.setRotation()` more than once | The first call (after `M5.begin()`) updates touch coordinate affine transform; a second call rotates the display pixels without updating touch, causing permanent coordinate drift |
| Mount the SD card with default SPI pins | **This is the bug that killed touch.** See below. |
| Add any `tab5TouchRecovery`, `tab5ForceInit`, or similar scaffolding | These have all been tried and they all fail for the same reason: M5GFX owns the hardware; fighting it re-breaks what it already set up correctly |

---

### The SD card / GPIO32 collision — root cause documented

**Bug:** Touch appeared to initialise (`M5.Touch.isEnabled() == true`,
`touch_enabled=1` in serial log) but every subsequent `M5.Touch.getDetail()`
returned `count=0, x=-1, y=-1`.

**Root cause:** `BOARD_HAS_SD` was `true` for Tab5 but all SD pin constants
were `-1`.  The call:

```cpp
sdSpi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
// expands to: sdSpi.begin(-1, -1, -1, -1)
```

On ESP32-P4, passing `-1` for every SPI pin causes the Arduino SPI driver to
fall back to the **default VSPI bus pins**, which include **MOSI = GPIO32**.
GPIO32 is simultaneously the GT911's **I²C SCL** line.  The SPI peripheral
claimed the pin, the I²C bus was silently destroyed, and the GT911 stopped
responding — all after `M5.begin()` had already confirmed touch was working.

**Fix:** `BOARD_HAS_SD = false` for `NEONDRIVE_TARGET_M5TAB5` until the
correct Tab5 SD SPI pins are identified from the Tab5 schematic and mapped
to `PIN_SD_SCLK / PIN_SD_MISO / PIN_SD_MOSI / PIN_SD_CS`.

**How to verify touch is alive after boot:**

```
[tab5] display 1280x720  touch_enabled=1
GPIO 31 : I2C_MASTER_SDA[1]    ← must appear
GPIO 32 : I2C_MASTER_SCL[1]    ← must appear (NOT SPI_MASTER_MOSI)
```

If GPIO32 shows `SPI_MASTER_MOSI` in the GPIO report, something called
`SPI.begin()` with default pins after `M5.begin()` and destroyed the I²C bus.

---

### SD card on Tab5 — **ENABLED** (2026-05-24)

SD is physically present on the Tab5 (SPI3 bus) and is now **enabled in firmware**
(`BOARD_HAS_SD = true`).  Pins verified from the Tab5 schematic:

| Signal | GPIO |
|--------|------|
| SCK    | 43   |
| MISO   | 39   |
| MOSI   | 44   |
| CS     | 42   |

None of these conflict with GT911 I²C (SDA=GPIO31, SCL=GPIO32).

**Post-flash verification:** boot and confirm GPIO32 shows `I2C_MASTER_SCL`
(not `SPI_MASTER_MOSI`) in the GPIO report — this confirms SD init did not
clobber the touch bus.

---

### WiFi on Tab5 — **WORKING** (confirmed 2026-05-24, re-confirmed after UI refactor)

WiFi scan returns results.  The ESP32-C6 co-processor has ESP-Hosted SDIO slave
firmware v2.12.6 from the factory.  The P4 host stack is pioarduino 55.03.38-1
(Arduino 3.3.8 / ESP-IDF 5.5.4) which ships ESP-Hosted host v2.12.3.

**Working configuration:**
- Platform: `pioarduino 55.03.38-1` (required for `WiFi.setPins()`)
- `WiFi.setPins(CLK=12, CMD=13, D0=11, D1=10, D2=9, D3=8, RST=15)` is called
  inside `tab5_configure_wifi_sdio_pins_once()` in `src/neon_rf.cpp`, which runs
  unconditionally at the top of `neon_rf_init()` for all Tab5 builds.
- No C6 reset pulse needed — factory firmware handles SDIO slave init on power-up
- `delay(1500)` after `WiFi.setPins()` to give C6 time to boot

**Critical implementation rule — `WiFi.setPins()` must NOT be build-guarded:**
The sdkconfig macro override (`include/tab5_sdkconfig_override.h`) does **not**
reach the SDMMC slot config in the pre-compiled `libespressif__esp_hosted.a`.
`WiFi.setPins()` is the only path that works (sdio_pin_config_t, PR #11513).
It must be called unconditionally for every Tab5 build — not only under
`TAB5_TEST_C6_SCAN` or similar guards.  `tab5_configure_wifi_sdio_pins_once()`
has a one-shot guard so it is safe to call from multiple code paths.

**Verified boot sequence:**
```
[neon_rf] WiFi.setPins: clk=12 cmd=13 d0=11 d1=10 d2=9 d3=8 rst=15
[neon_rf] C6 boot wait: 1500 ms (no RST pulse)
[esp32-hal-hosted.c] SDIO pins: clk=12, cmd=13, d0=11, d1=10, d2=9, d3=8, rst=15
[esp32-hal-hosted.c] ESP-Hosted initialized!
[esp32-hal-hosted.c] Slave firmware version: 2.12.6
```

---

### WiFi on Tab5 — historical crash (pioarduino 54.03.20, now obsolete)

> This section documents the crash that occurred with the old platform.
> It is no longer relevant for any active build.

With pioarduino `54.03.20` (ESP-IDF 5.3.x), `WiFi.setPins()` did not exist.
The SDIO bus defaulted to wrong pins, the C6 never responded to CMD5, and the
P4 host stack crashed with:

```
E sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107
FreeRTOS: FreeRTOS Task "sdio_read" should not return, Aborting now!
```

**Fix:** Upgrade to pioarduino 55.03.38-1 and call `WiFi.setPins()`.  No firmware
reflash of the C6 is needed — the factory firmware already has SDIO slave support.

---

### M5GFX + Arduino 3.3.x (ESP-IDF 5.5) — I²C bus corruption (FIXED 2026-05-24)

**Symptoms:** `Load access fault` crash during `M5.begin()` with pioarduino
55.03.xx.  Address decoder shows crash inside M5GFX `i2c::init()`.

**Root cause:** ESP-IDF 5.4 introduced a new I²C master driver API
(`i2c_new_master_bus` / `i2c_del_master_bus` instead of the old `i2c_driver_install`).
M5GFX 0.2.21 calls `Wire.end()` + `Wire.begin()` (via `release()` then `init()`)
multiple times per autodetect cycle.  On IDF 5.5, repeated `i2c_del_master_bus` +
`i2c_new_master_bus` cycles corrupt the driver's internal linked-list state;
after ~5 cycles `bus_handle` becomes NULL and the next I²C write crashes.

**Fix (applied to all 4 Tab5 libdeps M5GFX copies):**

In `.pio/libdeps/<env>/M5GFX/src/lgfx/v1/platforms/esp32/common.cpp`:

1. **`release(port)` — skip `Wire.end()` and pin reset on IDF ≥ 5.4:**
   ```cpp
   #if defined (ESP_IDF_VERSION_VAL) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0))
       // Keep the NG bus alive; only reset the M5GFX initialized flag.
   #else
       // ... existing Wire.end() + pinMode() teardown ...
   #endif
   ```

2. **`init(port)` — call `Wire.begin()` exactly once per port:**
   ```cpp
   #if defined (ESP_IDF_VERSION_VAL) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0))
       { static bool s_wire_init[I2C_NUM_MAX] = {};
         if (!s_wire_init[i2c_port]) {
           s_wire_init[i2c_port] = true;
           twowire->begin(...);
         }
       }
   #else
       twowire->begin(...);  // old path unchanged
   #endif
   ```

**Warning:** these patches are in `.pio/libdeps/` which PlatformIO can regenerate.
If libdeps are wiped, re-apply the patches from the commit history in
`codex/tab5-compile` branch.

---

### `tft` reference — static initialisation order fiasco (FIXED 2026-05-24)

**Symptom:** After M5.begin() completes cleanly, `tft.setRotation(1)` immediately
crashes with `Load access fault` at `LGFXBase::setRotation` line 61:
`lw a0,8(a0)` — `this = NULL`.

**Root cause:** `static M5GFX& tft = M5.Lcd;` at file scope.
`M5.Lcd` is a **reference member** of `M5Unified` (`M5GFX &Lcd = Display`).
Reference members are stored internally as pointers, initialised when the class
constructor runs.  If `main.cpp`'s statics initialise before the global
`M5Unified M` object is constructed (undefined order across translation units),
`M5.Lcd`'s internal pointer is still zero → `tft = nullptr` → crash.

**Fix (in `src/main.cpp` line ~307):**
```cpp
// BAD — reference member, depends on M5Unified constructor having run:
static M5GFX& tft = M5.Lcd;

// GOOD — value member, address is a link-time constant, safe at any init order:
static M5GFX& tft = M5.Display;
```

`M5.Display` is a plain `M5GFX` value member; `&M5.Display` is deterministic at
link time regardless of whether `M5Unified::M` has been constructed yet.

**Guarded builds:**
- `firmware_m5tab5` (production): no WiFi calls; boots cleanly; touch/display work.
- `firmware_m5tab5_c6_scan`: tests SDIO+WiFi scan; use `TAB5_TEST_C6_SCAN` guard.
- Both verified working with pioarduino 55.03.38-1 as of 2026-05-24.

---

## HAL UI Metrics — Tab5 layout and fonts (added 2026-05-24)

### Overview

All UI layout dimensions and font selections go through the HAL, not `#ifdef` blocks in feature code.
`neon_hal_ui_metrics()` returns a `const neon_hal_ui_t *` for the current target.
`initUiMetrics()` in `main.cpp` reads the struct once at boot and populates the global layout variables.

**Files:**
- `src/hal/neon_hal.h` — `neon_hal_ui_t` struct + `neon_hal_ui_metrics()` declaration
- `src/hal/hal_m5tab5.cpp` — Tab5 values: proportional to 1280×720, named M5GFX fonts
- `src/hal/hal_cyd.cpp` — CYD/T-Display values: pixel constants matching original hardcoded layout, all font pointers `nullptr`

### How to draw text on Tab5

Never call `tft.setTextSize()` directly in feature code. Use the four helper functions:

```cpp
applyFontSm();   // Montserrat 20pt — body text, labels, data rows
applyFontMd();   // Montserrat 28pt — buttons, section headers
applyFontLg();   // Montserrat 36pt — screen/panel titles
applyFontMono(); // FreeMono 18pt   — hex dumps, raw packet data, addresses
```

On CYD these fall back to `tft.setTextSize(1/2/3)` automatically — no `#ifdef` needed.

### How to use layout constants

Read dimensions from the globals set by `initUiMetrics()`:

```cpp
UI_SAFE_MARGIN   // outer padding from screen edge  (Tab5: 24, CYD: 8)
UI_TOP_GAP       // gap above header                (Tab5: 16, CYD: 6)
UI_BOTTOM_BAR_H  // bottom nav bar height           (Tab5: 112, CYD: 36)
UI_BUTTON_H      // standard tap target height      (Tab5: 72, CYD: 30)
UI_BUTTON_GAP    // gap between buttons             (Tab5: 20, CYD: 6)
```

For row height, padding, border radius, and header height use `g_ui`:
```cpp
g_ui->row_h      // list row height     (Tab5: 48, CYD: 16)
g_ui->pad        // inner padding       (Tab5: 16, CYD: 4)
g_ui->border_r   // rounded rect radius (Tab5: 12, CYD: 6)
g_ui->header_h   // header band height  (Tab5: 80, CYD: 30)
```

### Font symbols — correct namespace

M5GFX fonts live in `lgfx::fonts::`, not `lgfx::`:

```cpp
// CORRECT
&lgfx::fonts::lv_font_montserrat_20
&lgfx::fonts::lv_font_montserrat_28
&lgfx::fonts::lv_font_montserrat_36
&lgfx::fonts::FreeMono18pt7b

// WRONG — 'lv_font_montserrat_20' is not a member of 'lgfx'
&lgfx::lv_font_montserrat_20
```

Available Montserrat sizes: 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48.
Available FreeMono sizes: 9pt, 12pt, 18pt, 24pt (plus Bold and Oblique variants).
Include `<lgfx/v1/lgfx_fonts.hpp>` to use them.

### Tuning Tab5 layout values

Edit `s_tab5_ui` in `src/hal/hal_m5tab5.cpp`.  All values are in pixels on a 1280×720 canvas.
Do not touch `hal_cyd.cpp` values — they are calibrated to match the original CYD layout exactly.

### Anti-patterns

| Pattern | Why it's wrong |
|---|---|
| `tft.setTextSize(2)` in feature code | Bypasses HAL; renders at wrong size on Tab5 |
| `#if defined(NEONDRIVE_TARGET_M5TAB5)` for font/size selection | HAL exists for this; keep feature code target-agnostic |
| Adding new pixel constants as `constexpr int` in `main.cpp` | Add to `neon_hal_ui_t` instead and provide values in both HAL files |
| Calling `tft.setFont(...)` directly | Use `applyFontSm/Md/Lg/Mono()` so CYD fallback still works |

---

## M5Stack Cardputer Advanced Port (added 2026-05-24)

### Hardware profile

| Item | Value |
|------|-------|
| MCU | ESP32-S3FN8 (8 MB flash, no PSRAM) |
| Display | 240×135 ST7789 (via M5Cardputer library / M5GFX) |
| Keyboard | 56-key mechanical (TCA8418 I²C, polled via M5Cardputer.Keyboard) |
| WiFi | On-chip ESP32-S3 (standard WiFi.h, same pattern as CYD) |
| SD | SPI — SCK=40, MISO=39, MOSI=14, CS=12 |
| Touch | None |
| Sub-GHz radio | None |

### Keyboard navigation model

The Cardputer has no touch screen. Navigation is handled by the focus ring system:

- `NeonKey` enum + `neon_key_t` struct declared in `src/hal/neon_hal.h`
- `neon_hal_key_get()` polls `M5Cardputer.Keyboard.isChange()` and maps keys:
  - `0xB5`=UP, `0xB6`=DOWN, `0xB4`=LEFT, `0xB7`=RIGHT
  - `\n` / `\r` → `NeonKey::ENTER`
  - `0x08` / `0x7F` / `0x1B` → `NeonKey::BACK`
  - All other printable → `NeonKey::CHAR` + `ch`
- CYD and Tab5 return `{NeonKey::NONE, 0}` (stubs in `hal_cyd.cpp`, `hal_m5tab5.cpp`)
- Call `neon_hal_key_get()` in `loop()` guarded by `#if ND_HW_KEYBOARD`

### HAL values for Cardputer (240×135)

Defined in `src/hal/hal_cardputer_adv.cpp`. All fonts are `nullptr` (M5GFX `setTextSize`):

| Field | Value |
|-------|-------|
| `safe_margin` | 4 |
| `header_h` | 18 |
| `row_h` | 12 |
| `btn_h` | 20 |
| `btn_gap` | 3 |
| `bottom_bar_h` | 20 |
| `pad` | 3 |
| `border_r` | 4 |
| `text_size_sm/md` | 1 (GLCD 6×8) |
| `text_size_lg` | 2 (12×16, titles only) |

### NEONDRIVE_USES_M5GFX umbrella macro

`neon_hal.h` defines `NEONDRIVE_USES_M5GFX` when either `NEONDRIVE_TARGET_M5TAB5`
or `NEONDRIVE_TARGET_M5CARDPUTER` is defined. Use this macro for any code that is
M5GFX-generic (tft binding, clip rect, M5.begin, M5.update, applyFont).
Use `#ifdef NEONDRIVE_TARGET_M5TAB5` only for Tab5-specific things (GT911 touch,
1280×720 layout, SDIO WiFi, Tab5 rotation tick, screenshot overlay).

### No-op stubs for excluded translation units

`hypercube_widget.cpp`, `bruce_wifi.cpp`, and `wsl_bypasser.cpp` are excluded from
the Cardputer build via `build_src_filter`. Their symbols are provided as inline
stubs directly in the respective headers, guarded by `NEONDRIVE_TARGET_M5CARDPUTER`.
When adding new symbols to these files, add matching inline stubs to their headers.

### Flash command (Windows)

```powershell
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_cardputer_adv -t upload --upload-port COMx
```

Cardputer Adv auto-resets via DTR — no hold-RESET procedure required.

---

## LilyGO T-Embed CC1101 Plus Hardware Bringup (2026-05-25)

### Hardware profile

| Item | Value |
|------|-------|
| MCU | ESP32-S3 (16 MB flash, 8 MB PSRAM) |
| Display | 170×320 ST7789 (landscape via rotation=1 → 320×170) |
| Input | Rotary encoder (GPIO4/5) + encoder button (GPIO0/BOOT) + side key (GPIO6) |
| WiFi | On-chip ESP32-S3 (standard WiFi.h) |
| Sub-GHz | CC1101 (SPI CS=12, GDO0=3, GDO2=38) |
| Antenna switch | SW1=GPIO47, SW0=GPIO48 |
| nRF24L01 | CE=43, CS=44 (expansion module) |
| IR TX | Enable=GPIO2, RX=GPIO1 |
| WS2812B | 8 LEDs on GPIO14 |
| SD card | SPI3: SCK=11, MISO=10, MOSI=9, CS=13 |
| Power enable | GPIO15 (must be HIGH before CC1101 or WS2812) |

**All pins verified from LILYGO official repo: `examples/factory_test/utilities.h`.**
There is NO `pin_config.h` in the official LILYGO T-Embed CC1101 repo.

### Shared SPI bus

Display, CC1101, SD card, and nRF24 all share the same MOSI/MISO/SCK bus (GPIO 9/10/11).
Each device has its own CS pin. Firmware deasserts all CS lines in `setup()` before calling
`SPI.begin()` to prevent bus contention during init.

### Encoder navigation model

- `mathertel/RotaryEncoder` with `LatchMode::TWO03` — one event per physical detent
- `NeonKey::UP/DOWN` → rotate encoder counter-clockwise/clockwise
- `NeonKey::ENTER` → press encoder button (GPIO0, active-low)
- `NeonKey::BACK` → press side key (GPIO6, active-low)
- Full implementation in `src/hal/hal_t_embed_cc1101.cpp`

### Boot verification checklist

Expected serial output at 115200 baud:

```
[tembed] PWR_EN=HIGH, all SPI CS deasserted
[neon_rf] backend=tembed-s3  reset=POWERON
[neon_rf] caps: scan=1  ch_ctrl=1  promisc=1  raw_tx=1  coprocessor=0
[neon_rf] T-Embed CC1101 Plus radio map:
[neon_rf]   CC1101: CS=12 GDO0=3 GDO2=38  ANT_SW1=47 ANT_SW0=48
[neon_rf]   nRF24L01: CE=43 CS=44 IRQ=-1 (expansion module)
[neon_rf]   CC1101 RadioLib driver: Phase 6 (not yet active)
tft.width()=320 tft.height()=170
[input] encoder + button navigation enabled (no touch)
[fs] LittleFS mounted.
```

### Known issues / Phase 6 TODO

- CC1101 RadioLib driver not yet wired into neon_rf — antenna switch LOW (radio disabled)
- nRF24L01 not yet initialised
- HypercubeWidget not rendered (stub only) — top-right corner is blank

### Flash / monitor commands

```cmd
scripts\flash_tembed_cc1101.cmd COM9
scripts\monitor_tembed_cc1101.cmd COM9
```

---

## PlatformIO Environments

| Environment | Board | Status |
|---|---|---|
| `firmware_cyd_2_4` | CYD 2.4" (ESP32) | ✅ Stable |
| `firmware_cyd_2_8` | CYD 2.8" (ESP32) | ✅ Stable |
| `firmware_cyd_3_5` | CYD 3.5" (ESP32) | ✅ Stable |
| `firmware_t_display_s3` | LilyGO T-Display-S3 | ⚠️ Beta |
| `firmware_t_embed_cc1101` | LilyGO T-Embed CC1101 | ✅ Compiles + flashes (Phase 5 hardware validation in progress) |
| `firmware_m5tab5` | M5Stack Tab5 (ESP32-P4) | ✅ Display + touch working, WiFi working |
| `firmware_cardputer_adv` | M5Stack Cardputer Adv (ESP32-S3) | 🚧 Alpha — compiles, untested on hardware |

Flash command: `pio run -e <env> -t upload --upload-port <COMx>`

Tab5 download mode: hold RESET ~2 s until green LED blinks rapidly, then release.

**Tab5 / Cardputer flash on Windows** — esptool crashes with `UnicodeEncodeError` unless UTF-8 is forced:
```powershell
$env:PYTHONUTF8=1; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e firmware_m5tab5 -t upload --upload-port COM4
```

**Tab5 esptool version note** — pioarduino requires its own patched esptool (hyphen args: `--flash-mode`,
`--flash-freq`). The correct version is auto-installed by pioarduino's `configure_default_packages` via
`uv pip install -e ~/.platformio/tools/tool-esptoolpy`. If `pip install esptool` is run manually, it will
install the official PyPI version (underscore args) and break Tab5 builds. Restore with:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\uv.exe" pip install -e "$env:USERPROFILE\.platformio\tools\tool-esptoolpy" --python "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
```

---

## Known Hardware MACs

| MAC suffix | Board |
|---|---|
| `a9:1b:58` | CYD 3.5" — use `firmware_cyd_3_5` |
| `b4:95:8c` | CYD 2.4" — use `firmware_cyd_2_4` |
| `e2:e6:95` | M5Stack Tab5 — use `firmware_m5tab5` |
| `90:70:69:0c:b9:e0` | LilyGO T-Embed CC1101 Plus — use `firmware_t_embed_cc1101`, COM9 |

Always confirm the board before flashing.  Ask if not specified.
