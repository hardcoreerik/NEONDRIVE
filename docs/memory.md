# Memory and DRAM Rules

## Scope
This document defines memory standards, measurement steps, and regression checks for the CYD ESP32 firmware.

Goals:
- Reduce DRAM pressure and fragmentation risk without changing user-visible behavior.
- Keep release behavior/UI identical.
- Make memory usage measurable and repeatable per build.

## Non-Negotiables
- No UI layout/color/font/text/flow changes.
- No feature or output behavior changes (logs, files, button actions).
- All diagnostics are compile-time gated and disabled in release by default.

## Build Environments
- `env:cyd`: release-equivalent build (default).
- `env:cyd_debug_mem`: memory diagnostics build (`UI_DEBUG=1`, `UI_DEBUG_MEM=1`).

## Compile-Time Flags
Defined in `platformio.ini`:
- `UI_DEBUG=0` in release.
- `UI_DEBUG_MEM=0` in release.
- `MEM_BUDGET_MIN_FREE_HEAP=40960` (40 KB warning threshold in debug mem builds).
- `FEATURE_ANDROID_OFFLOAD=1` (documents intended protocol boundary).

Debug memory env:
- `env:cyd_debug_mem` unflags release debug defines and enables:
  - `UI_DEBUG=1`
  - `UI_DEBUG_MEM=1`

## Memory Instrumentation (UI_DEBUG_MEM)
Implemented in `src/main.cpp`:
- Logs:
  - `heap_caps_get_free_size(MALLOC_CAP_8BIT)`
  - `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
  - `ESP.getFreeHeap()`
  - `ESP.getHeapSize()`
  - `ESP.getSketchSize()`
  - `ESP.getFreeSketchSpace()`
  - PSRAM info when enabled
- Boot memory budget log:
  - prints free heap, largest free block, data+bss static estimate
  - warns if free heap is below `MEM_BUDGET_MIN_FREE_HEAP`

Log points:
- `boot_after_init`
- `after_ui_init`
- `screen:<name>` on each screen transition
- `after_start_sniff`
- `after_stop_sniff`
- `after_start_capture` (Bruce capture open)
- `after_stop_capture` (Bruce capture close)

## Memory Rules

### A) Strings
- Keep constant strings in flash (string literals, `F(...)` where compatible).
- Avoid dynamic `String` construction in hot loops and frequent redraw paths.
- Preserve exact displayed/logged text when refactoring storage/formatting.

### B) Buffers
- Do not place large buffers on stack.
- Prefer explicit static/global buffers with documented sizing.
- Keep subsystem buffers local to that subsystem.

### C) Dynamic Allocation
- Avoid repeated malloc/free in loops.
- Allocate once and reuse where possible.
- Document ownership for allocated resources.

### D) Assets
- Keep assets in flash/LittleFS/SD, not copied into persistent DRAM buffers.
- No asset content changes.

### E) UI
- No per-frame dynamic allocation on redraw paths.
- Reuse UI objects/layout constants.
- UI output must remain visually identical.

### F) Data Structures
- Use compact types where safe (`uint8_t`, `uint16_t`).
- Avoid duplicate copies of scan/capture data.
- Keep exported/logged data content unchanged.

## Implemented DRAM/Stability Changes
- Added compile-time-gated memory instrumentation and budget warning.
- Added memory probes on required lifecycle points.
- Reduced transient heap churn in Deauth Hunter:
  - replaced `String` temp formatting in hot logs with fixed buffers/`Serial.printf`.
  - pre-reserved attacker vector capacity (`MAX_TRACKED_ATTACKERS`) to avoid reallocations.
- Reduced transient heap churn in Recon UI stats redraw by using fixed numeric buffers instead of temporary `String` objects.
- Removed dead include from `main.cpp` (`pineap_hunter.h`) with no runtime use.
- Removed dead files not part of active build:
  - `src/yoink.cpp` (legacy file not used by current firmware)
  - `src/yoink.h` (legacy header for excluded file)
  - `psram_check.ino` (not in PlatformIO `src` build path)
  - `$null` (stray workspace file)
- Removed obsolete `build_src_filter` exclusion for `yoink.cpp` after deleting that file.

## Link-Time Metrics (Baseline vs Current)

### env:cyd (release)
- Baseline (before changes):
  - `text=1023724`, `data=254572`, `bss=63785`, `dec=1342081`
  - RAM summary: `89560 / 327680` (27.3%)
  - Flash summary: `1255881 / 1966080` (63.9%)
- Current (after changes):
  - `text=1023392`, `data=254412`, `bss=63785`, `dec=1341589`
  - RAM summary: `89560 / 327680` (27.3%)
  - Flash summary: `1255389 / 1966080` (63.9%)
- Delta:
  - `text -332`
  - `data -160`
  - `bss +0`
  - `dec -492`
  - Flash summary `-492` bytes

### env:cyd_debug_mem (diagnostic)
- Current:
  - `text=1024736`, `data=255392`, `bss=63793`, `dec=1343921`
  - RAM summary: `89584 / 327680` (27.3%)
  - Flash summary: `1257713 / 1966080` (64.0%)

## Runtime Metrics Status
Runtime serial capture depends on exclusive COM access.
- Instrumentation is compiled and uploaded in `cyd_debug_mem`.
- If serial port is free, expected logs include `[mem] ...` and `[mem-budget] ...` at all required points.
- On February 27, 2026, automated capture in this workspace was blocked by serial port contention (`COM16 access denied`), so runtime heap/largest-block numbers remain pending device capture.

## Android Offload Strategy (Behavior-Preserving)
Use Android app for heavy post-processing while ESP32 keeps identical UI/control behavior:
- Offload to Android:
  - sorting/filtering/aggregation of scan/capture events
  - analytics and reporting
  - historical/correlation views
  - large JSON transformation and enrichment
- Keep on ESP32:
  - capture, sniff, channel lock, monitor controls, file generation
  - existing logs and file formats (PCAP/CSV/log)

Protocol boundary:
- ESP32 sends raw/status events (`/api/status`, `/api/packets`, existing logs/files).
- Android computes derived views; no required firmware-side behavior change.
- Fallback safe: if Android is disconnected, ESP32 behavior remains unchanged.

## Regression Guard
- Boot memory budget warning is active only when `UI_DEBUG_MEM=1`.
- Release behavior is unchanged (`UI_DEBUG_MEM=0` in `env:cyd`).

## How To Test
- Build release:
  - `pio run -e cyd`
- Build debug memory:
  - `pio run -e cyd_debug_mem`
- Size reports:
  - `pio run -e cyd -t size`
  - `pio run -e cyd_debug_mem -t size`
- Upload debug memory build:
  - `pio run -e cyd_debug_mem -t upload --upload-port <PORT>`
- Monitor logs:
  - `pio device monitor -p <PORT> -b 115200`
- Trigger memory log points:
  - boot device, verify `boot_after_init` and `after_ui_init`
  - navigate screens, verify `screen:<name>`
  - start/stop sniff in Target Ops
  - start/stop Bruce attack capture in Bruce Monitor
