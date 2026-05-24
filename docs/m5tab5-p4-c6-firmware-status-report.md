# M5Tab5 P4/C6 Firmware Status Report

Date: 2026-05-23  
Repo: `X:\NEONDRIVE-TAB5\NEONDRIVE`  
Mode: Inspection only

## Context
NEONDRIVE’s RF logic was built for CYD-class ESP32 boards where the main ESP32 owns Wi-Fi directly.  
M5Tab5 uses ESP32-P4 (app/UI) + ESP32-C6 (Wi-Fi/BLE coprocessor over SDIO / ESP-Hosted model).

Known Tab5 P4 -> C6 pin map:
- ESP32-P4 G11 -> ESP32-C6 SDIO2_D0
- ESP32-P4 G10 -> ESP32-C6 SDIO2_D1
- ESP32-P4 G9  -> ESP32-C6 SDIO2_D2
- ESP32-P4 G8  -> ESP32-C6 SDIO2_D3
- ESP32-P4 G13 -> ESP32-C6 SDIO2_CMD
- ESP32-P4 G12 -> ESP32-C6 SDIO2_CK
- ESP32-P4 G15 -> ESP32-C6 RESET
- ESP32-P4 G14 -> ESP32-C6 IO2

---

## 1) Direct Wi-Fi/RF Calls Found

### `src/main.cpp`
| Function | Line(s) | API/Behavior | CYD | Tab5 risk | Belongs on | RF abstraction | First safe change |
|---|---:|---|---|---|---|---|---|
| `promiscuousPacketCallback` | 1672+ | Local promiscuous packet parse path | Works | High (callback source may not exist locally on P4) | C6/backend stream | Yes | Keep parser, decouple callback source |
| `startSniff` | 1747+ | `esp_wifi_set_promiscuous*`, AP mode, hidden AP, test deauth | Works | High | C6/backend | Yes | Backend gate on Tab5 |
| `stopSniff` | 1971+ | Promisc shutdown/retry loop | Works | High | C6/backend | Yes | Replace with backend stop |
| USB RPC `ai_inject_raw` | 2653-2654 | `esp_wifi_set_channel`, `esp_wifi_80211_tx` | Works | Very high | C6 RF service | Yes | Return unsupported on Tab5 local path |
| USB RPC `ai_inject_deauth` | 2677+ | Channel set + deauth send loop | Works | Very high | C6 RF service | Yes | Backend gate |
| Web `/api/ai/inject_raw` | 8626-8670 | raw hex -> `esp_wifi_80211_tx` (+ fallback) | Works | Very high | C6 RF service | Yes | Backend gate endpoint |
| Probe/WPS TX path | 6087-6088 | `esp_wifi_80211_tx` AP/STA fallback | Works | High | C6 RF service | Yes | Route through backend |
| `startReconDeauthHunter` | 10247+ | Promisc filter + channel | Works | High | C6/backend | Yes | Backend guard |
| `stopReconDeauthHunter` | 10277+ | Promisc teardown | Works | High | C6/backend | Yes | Backend guard |
| `startJammitWifi` | 11590+ | Promisc + PS off + TX power + WSL | Works | Very high | C6/custom RF service | Yes | Tab5 should not enter local path |
| `tryAlternateTx` | 1350+ | Promisc toggles + STA/AP interface retries | Works | High | C6 RF service | Yes | Exclude from Tab5 execution |

### `src/yoink_engine.cpp`
| Function | Line(s) | API/Behavior | CYD | Tab5 risk | Belongs on | RF abstraction | First safe change |
|---|---:|---|---|---|---|---|---|
| `startWifi` | 161+ | Promisc + PS + channel + callback | Works | High | C6/backend | Yes | Backend gate |
| `stopWifi` | 200+ | Promisc teardown | Works | High | C6/backend | Yes | Backend gate |
| `hopChannel`, attack ticks | 223, 870, 904, 1057 | channel set + raw tx | Works | Very high | C6 RF service | Yes | Unsupported on Tab5 until backend exists |

### `src/wsl_bypasser.cpp/.h`
| Function | Line(s) | API/Behavior | CYD | Tab5 risk | Belongs on | RF abstraction | First safe change |
|---|---:|---|---|---|---|---|---|
| `sendDeauthFrame/sendDisassoc/sendBurst/randomizeMAC` | 41, 59, 84, 102 | raw tx + MAC set + net80211 sanity-check override | Works | Very high | CYD-only local radio path | Yes | Keep Tab5 no-op stubs (already present) |

### `src/wifiscan.cpp`
| Function | Line(s) | API/Behavior | CYD | Tab5 risk | Belongs on | RF abstraction | First safe change |
|---|---:|---|---|---|---|---|---|
| `prepareScanEnvironment` | 56-57 | local promisc disable before scan | Works | Medium-High | backend-managed scan path | Yes | Backend guard on Tab5 |

### `src/deauth_hunter.cpp`
| Function | Line(s) | API/Behavior | CYD | Tab5 risk | Belongs on | RF abstraction | First safe change |
|---|---:|---|---|---|---|---|---|
| `setChannelFilter`, `hopChannel` | 177, 342 | direct `esp_wifi_set_channel` | Works | High | C6/backend | Yes | Route channel ops through backend |

### `src/bruce_wifi.cpp/.h`
| Function | Line(s) | API/Behavior | CYD | Tab5 risk | Belongs on | RF abstraction | First safe change |
|---|---:|---|---|---|---|---|---|
| attack tx internals | 270+ | `esp_wifi_80211_tx` | Works | High if used | C6/custom RF service | Yes | Already compile-gated for Tab5 |
| Tab5 stubs | `bruce_wifi.h` 81+ | no-op API on Tab5 | N/A | Low | P4 app layer | Yes | Pattern should be expanded to other RF modules |

### Requested APIs not found
No hits found for:
- `esp_wifi_init`, `esp_wifi_deinit`, `esp_wifi_set_mode`, `esp_wifi_start`, `esp_wifi_stop`
- `esp_wifi_scan_start`, `esp_wifi_scan_get_ap_records`, `esp_wifi_get_channel`, `esp_wifi_restore`
- `esp_netif_init`, `esp_event_loop_create_default`

---

## 2) Board/Target Logic

### Build-level targeting
- `platformio.ini`:
  - CYD/T-Display/T-Embed use `espressif32@6.12.0`
  - Tab5 uses `pioarduino platform-espressif32` with `board = esp32-p4-evboard`
  - Tab5 flags include `NEONDRIVE_TARGET_M5TAB5` and `ARDUINO_M5STACK_TAB5`
  - Tab5 excludes `wsl_bypasser.cpp` and `bruce_wifi.cpp` via `build_src_filter`

### Compile-time gating
- `include/device_profile_select.h` selects profile by target define.
- `src/main.cpp` has extensive `#if defined(NEONDRIVE_TARGET_...)` branches.
- `src/wsl_bypasser.h` provides Tab5 no-op stubs.
- `src/bruce_wifi.h` provides Tab5 no-op stubs.

### P4/C6/ESP-Hosted status
- No concrete app-side hosted backend implementation found:
  - no operational `esp_hosted` / `wifi_remote` integration in `src/include/hardware/scripts`
- Docs acknowledge architecture and instability:
  - `docs/HARDWARE_TARGETS.md` calls out P4->C6 model and SDIO crash risk.
  - `CLAUDE.md` notes checking C6 firmware blob/version as future work.

---

## 3) Crash/Reboot Clues

### Known documented clue
- `docs/HARDWARE_TARGETS.md` explicitly notes:
  - `H_SDIO_DRV: sdio card init failed`
  - Wi-Fi features non-functional on current Tab5 path

### High-risk patterns
- RF operations are triggered directly from UI flows (scan/recon/jammit/inject).
- Many `esp_wifi_*` calls do not check return codes.
- Retries/toggles (`tryAlternateTx`, `stopSniff`) assume local radio ownership.
- Multiple blocking `delay(...)` in RF mode transitions.

### Hard reset evidence
- `ESP.restart()` exists in web update flow (`src/main.cpp:8125`) (not RF-specific but relevant for diagnosing reboot sources).

---

## 4) ESP-Hosted / C6 Firmware Management Status

### Found
- Architecture awareness only (docs/comments).
- No P4 app logic for:
  - C6 firmware version probe
  - compatibility check gates
  - C6 reset/boot sequencing via reserved interface
  - C6 OTA/upgrade orchestration

### Not found (but needed later)
- RF backend for Tab5 routed through hosted/C6 service
- User-facing compatibility preflight before enabling RF features
- Preferred USB-C/P4-side upgrade path with fallback messaging for downloader/USB-TTL recovery cases

---

## Summary Table

| Feature / Screen | Current implementation | CYD status | M5Tab5 risk | Recommended backend |
|---|---|---|---|---|
| Basic scan | `WiFi.scanNetworks` + local prep in `wifiscan.cpp` | Works | Medium-High | `neon_rf.scan()` |
| Channel set | direct `esp_wifi_set_channel` in multiple modules | Works | High | `neon_rf.setChannel()` |
| Promiscuous RX | direct `esp_wifi_set_promiscuous*` + callbacks | Works | High | `neon_rf.promiscStart/Stop()` |
| Packet callback parsing | local callback parse in `main.cpp` | Works | High | parser kept; packet source supplied by backend |
| Raw frame TX | direct `esp_wifi_80211_tx` (+ WSL on CYD) | Works | Very High / unsupported locally | C6-side RF service |
| BLE features if present | partial scaffolding/headers | Partial | High | backend-gated BLE service |
| SD logging | local SD/PCAP writes | Works | Medium (coupled to RF state) | keep local (backend-agnostic) |
| UI rendering | local display/touch on app CPU | Works | Low | keep on P4 |

---

## Smallest Safe Starting Plan
1. Add boot/reset diagnostics.
2. Add `last_rf_action` crash logging.
3. Add a `neon_rf` abstraction layer.
4. On M5Tab5, make unsupported RF calls return `ESP_ERR_NOT_SUPPORTED` instead of calling local `esp_wifi` APIs.
5. Preserve existing CYD behavior unchanged.

---

## Current status statement for AI handoff
NEONDRIVE currently contains substantial CYD-native Wi-Fi/RF code paths that directly call local `esp_wifi` APIs.  
Tab5 build gating disables only part of this surface (`bruce_wifi`, `wsl_bypasser`), but other RF paths in `main.cpp`, `yoink_engine.cpp`, `wifiscan.cpp`, and `deauth_hunter.cpp` still use local-radio assumptions.  
Given Tab5’s P4+C6 architecture and documented SDIO instability, these direct calls are likely the primary reason for RF-triggered reboot/failure behavior.  
Next safe step is backend abstraction + strict Tab5 unsupported-path handling before introducing custom C6 firmware work.
