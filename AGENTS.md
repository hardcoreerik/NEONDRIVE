# NEONDRIVE — Codex / Codex Reference

This file is read automatically at the start of every Codex and Codex session.
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

### WiFi on Tab5 — **WORKING** (confirmed 2026-05-24)

WiFi scan returns results.  The ESP32-C6 co-processor has ESP-Hosted SDIO slave
firmware v2.12.6 from the factory.  The P4 host stack is pioarduino 55.03.38-1
(Arduino 3.3.8 / ESP-IDF 5.5.4) which ships ESP-Hosted host v2.12.3.

**Working configuration:**
- Platform: `pioarduino 55.03.38-1` (required for `WiFi.setPins()`)
- Call `WiFi.setPins(CLK=12, CMD=13, D0=11, D1=10, D2=9, D3=8, RST=15)` **before**
  `WiFi.mode()` in `neon_rf_init()`
- No C6 reset pulse needed — factory firmware handles SDIO slave init on power-up
- `delay(1500)` before `WiFi.setPins()` to give C6 time to boot

**Verified boot sequence:**
```
[esp32-hal-hosted.c] SDIO pins: clk=12, cmd=13, d0=11, d1=10, d2=9, d3=8, rst=15
[esp32-hal-hosted.c] ESP-Hosted initialized!
[esp32-hal-hosted.c] Slave firmware version: 2.12.6
[c6test] step=sdio_init OK - starting scan
[c6test] step=scan OK found=N      ← N > 0 confirms WiFi works
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

## PlatformIO Environments

| Environment | Board | Status |
|---|---|---|
| `firmware_cyd_2_4` | CYD 2.4" (ESP32) | ✅ Stable |
| `firmware_cyd_2_8` | CYD 2.8" (ESP32) | ✅ Stable |
| `firmware_cyd_3_5` | CYD 3.5" (ESP32) | ✅ Stable |
| `firmware_t_display_s3` | LilyGO T-Display-S3 | ⚠️ Beta |
| `firmware_t_embed_cc1101` | LilyGO T-Embed CC1101 | 🚧 Untested |
| `firmware_m5tab5` | M5Stack Tab5 (ESP32-P4) | ✅ Display + touch working, WiFi working |

Flash command: `pio run -e <env> -t upload --upload-port <COMx>`

Tab5 download mode: hold RESET ~2 s until green LED blinks rapidly, then release.

---

## Known Hardware MACs

| MAC suffix | Board |
|---|---|
| `a9:1b:58` | CYD 3.5" — use `firmware_cyd_3_5` |
| `b4:95:8c` | CYD 2.4" — use `firmware_cyd_2_4` |
| `e2:e6:95` | M5Stack Tab5 — use `firmware_m5tab5` |

Always confirm the board before flashing.  Ask if not specified.
