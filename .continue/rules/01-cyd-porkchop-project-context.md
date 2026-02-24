---
name: remembertheproject
description: Core project rules and architecture context for M5PORKCHOP CYD full replacement
alwaysApply: true
---

# Role and scope
- You are assisting on `M5PORKCHOP-CYD_FULL_REPLACEMENT`, an ESP32 CYD firmware port of Porkchop.
- Primary goal: preserve stable CYD bring-up while incrementally adding features.
- Keep responses and code aligned with existing architecture and hardware constraints.

# Source of truth files
- Build config: `platformio.ini`
- CYD display/touch wiring baseline: `include/User_Setup.h`
- App entry and UI flow: `src/main.cpp`
- YOINK engine: `src/yoink_engine.h`, `src/yoink_engine.cpp`, `src/yoink_save.h`, `src/yoink_save.cpp`
- Raw frame bypass: `src/wsl_bypasser.h`, `src/wsl_bypasser.cpp`
- Porting reference: `docs/YOINK_PORTING_PLAN.md`
- BT companion protocol reference: `docs/cydcompanion_bt_protocol.md`
- Legacy/original reference only: `src_original/**`

# Hard project constraints
- Hardware target is CYD class ESP32 board with 4MB flash and no PSRAM.
- Keep `platform = espressif32@6.12.0`, `framework = arduino`, board env `cyd` unless explicitly asked to migrate.
- Maintain 4MB-safe partition assumptions; do not switch to larger flash layouts.
- Keep `-Wl,-zmuldefs` in build flags to preserve raw management frame injection behavior.
- Keep local TFT config forced with:
  - `-DUSER_SETUP_LOADED=1`
  - `-include include/User_Setup.h`
- Preserve `build_src_filter = +<*> -<yoink.cpp>` (legacy file is intentionally excluded).

# UI and hardware baseline
- CYD TFT baseline is locked in `include/User_Setup.h` (ILI9341_2 driver, specific SPI pins/frequencies).
- `src/main.cpp` uses landscape orientation and current touch mapping assumptions.
- Do not change display driver, rotation strategy, or touch CS defaults without explicit user request.
- Respect current SD/LittleFS coexistence and existing init order patterns.

# Architecture and edit boundaries
- Prefer edits in current implementation under `src/**`.
- Treat `src_original/**` as reference archive; do not refactor it unless explicitly requested.
- Preserve current callback safety model: avoid heavy work or dynamic allocation in WiFi callback context.
- Keep memory usage conservative; avoid large heap allocations and long-lived buffers unless justified.
- Maintain existing APIs and names where practical to reduce regression risk.

# Build and verification workflow
- After code changes, validate with:
  - `pio run -e cyd`
- If relevant to filesystem/data changes, also validate:
  - `pio run -e cyd -t uploadfs`
- If tests are touched or related:
  - `pio test -e native`
- Report compile/test status and any skipped verification explicitly.

# Coding guidance for this repo
- Use C++ compatible with Arduino/ESP32 toolchain in this project.
- Prefer minimal, targeted changes over broad rewrites.
- Preserve existing logging style (`Serial.print*` and project helper logs).
- Keep comments short and practical; only where code intent is not obvious.
- Do not add new dependencies unless user explicitly approves.

# Behavior rules for assistant responses
- Prioritize project-specific facts from local files over generic ESP32 advice.
- When suggesting changes, reference concrete files and rationale tied to CYD constraints.
- If a request conflicts with locked hardware/build baseline, call it out and propose the safest alternative.
- For protocol or integration questions, align output with `docs/cydcompanion_bt_protocol.md`.

# Useful commands
- Build: `pio run -e cyd`
- Upload firmware: `pio run -e cyd -t upload`
- Upload filesystem image: `pio run -e cyd -t uploadfs`
- Serial monitor: `pio device monitor -b 115200`
