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

### SD card on Tab5 — status and next steps

SD is physically present on the Tab5 (SPI3 bus).  It is **disabled in firmware**
(`BOARD_HAS_SD = false`) because the correct pin mapping has not been verified.

Before re-enabling SD:
1. Obtain the Tab5 schematic and identify the SPI3 MOSI/MISO/SCK/CS pins.
2. Confirm none of those pins are shared with GT911 I²C (SDA=31, SCL=32).
3. Set `PIN_SD_SCLK`, `PIN_SD_MISO`, `PIN_SD_MOSI`, `PIN_SD_CS` in the
   `#if defined(NEONDRIVE_TARGET_M5TAB5)` pin block in `src/main.cpp`.
4. Change `BOARD_HAS_SD` back to `true`.
5. Boot and verify GPIO32 still shows `I2C_MASTER_SCL` (not `SPI_MASTER_MOSI`).

---

### WiFi on Tab5 — known crash

When any code path triggers `WiFi.begin()` or starts a scan, the ESP32-P4 tries
to initialise the ESP32-C6 co-processor over the SDIO bus:

```
E H_SDIO_DRV: sdio card init failed
FreeRTOS: Task "sdio_read" should not return, Aborting now!
```

This is a known unresolved issue. All WiFi-dependent features (wardrive, scan,
deauth, SP3CTER) will crash the device on Tab5 until the SDIO / C6 init problem
is fixed. **Do not attempt to work around it by catching the abort** — the task
stack is corrupted by then.

Investigation starting point: verify the pioarduino platform version includes
the correct C6 co-processor firmware blob, and that `ARDUINO_M5STACK_TAB5` is
triggering the right SDIO host configuration in ESP-IDF.

---

## PlatformIO Environments

| Environment | Board | Status |
|---|---|---|
| `firmware_cyd_2_4` | CYD 2.4" (ESP32) | ✅ Stable |
| `firmware_cyd_2_8` | CYD 2.8" (ESP32) | ✅ Stable |
| `firmware_cyd_3_5` | CYD 3.5" (ESP32) | ✅ Stable |
| `firmware_t_display_s3` | LilyGO T-Display-S3 | ⚠️ Beta |
| `firmware_t_embed_cc1101` | LilyGO T-Embed CC1101 | 🚧 Untested |
| `firmware_m5tab5` | M5Stack Tab5 (ESP32-P4) | 🔧 In progress — touch works, WiFi crashes |

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
