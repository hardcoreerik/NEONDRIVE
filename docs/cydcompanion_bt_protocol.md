# CYDCompanion Bluetooth Protocol (CYD NEONDRIVE Port)

This firmware exposes a Bluetooth Classic SPP endpoint:

- Device name: `CYDCompanion-CYD`
- Transport: line-based text (`\n` terminated)
- Separator: `|`
- Response prefixes:
  - `OK|...`
  - `ERR|...`
  - `STATUS|...`
  - `DATA|...`
  - `ENTRY|...`
  - `AP|...`
  - `TARGET|...`
  - `CONFIG|...`

## Session

- On connect, device sends: `READY|CYD_NEONDRIVE|BTCTRL|1`
- Basic probe:
  - `HELLO`
  - `PING`
  - `HELP`

## Control Commands

- `STATUS`
  - Returns:
  - `STATUS|screen|wifiScan|apCount|apSelected|hasTarget|ssid|bssid|ch|rssi|sniff|autoMode|lock|pps|pkts|drop|werr|sdReady|configDirty|btConnected`
- `AP_LIST`
  - Returns multiple lines:
  - `AP|index|ssid|bssid|rssi|channel|auth`
  - Then: `OK|AP_LIST|count`
- `WIFI_SCAN`
- `SELECT_AP|index`
- `SELECT_BSSID|aa:bb:cc:dd:ee:ff`
- `TARGET_GET`
  - `TARGET|0` if none
  - `TARGET|1|ssid|bssid|rssi|channel|auth` if selected
- `TARGET_CLEAR`
- `SCREEN|HOME|WIFI_SCAN|CONFIRM_TARGET|TARGET_DETAILS|AUTOMATE_MENU|CONFIG|ABOUT`
- `SNIFF|START|STOP`
- `LOCK|0|1`
- `AUTO|NONE|Y0INK|RAW|SCOPE|JAMMIT`

## Config Commands

- `CONFIG_GET`
  - Returns:
  - `CONFIG|wifi_channelHopInterval|wifi_scanDuration|wifi_enableDeauth|display_showStats|display_timeout|display_brightness|configDirty`
- `CONFIG_SET|key|value`
  - Keys:
  - `wifi_channelHopInterval`
  - `wifi_scanDuration`
  - `wifi_enableDeauth`
  - `display_showStats`
  - `display_timeout`
  - `display_brightness`
- `CONFIG_SAVE`

## File Commands

Filesystem token:

- `SD`
- `LFS` (LittleFS)

Commands:

- `LIST|FS|/path`
  - Returns `ENTRY|FS|D|0|/dir` or `ENTRY|FS|F|size|/file`
- `STAT|FS|/path`
- `READ|FS|/file|offset|len`
  - Returns `DATA|offset|len|HEX...` then `OK|READ|len`
  - Max chunk per call: 256 bytes
- `WRITE_BEGIN|FS|/file|totalBytes|overwrite`
  - `overwrite`: `0/1` (or `true/false`)
- `WRITE_CHUNK|HEX...`
- `WRITE_END`
- `WRITE_ABORT`
- `DELETE|FS|/file`
- `MKDIR|FS|/dir`
- `RMDIR|FS|/dir`
- `RENAME|FS|/src|/dst`

## Notes

- All paths must be absolute (start with `/`).
- `..` path traversal is rejected.
- File payloads are hex encoded for predictable framing over SPP.
- If a write session is interrupted/disconnected, partial uploads are aborted and cleaned up.

---

## AI Injection REST API  (Phase 1 — HTTP, not SPP)

These endpoints are served by the CYD WebServer over Wi-Fi (same network as the Android
companion).  They are the bridge between the Android AI engine and the ESP32 radio.

### `POST /api/ai/inject_raw`

Transmit an arbitrary 802.11 frame the AI has assembled.

**Request body (JSON):**
```json
{ "channel": 6, "hex": "C000000000..." }
```
| Field | Type | Notes |
|---|---|---|
| `channel` | int | 1–13; device hops to this channel before TX. Omit to use current channel. |
| `hex` | string | Full 802.11 frame as an uppercase hex string (min 10 bytes, max 512 bytes). |

**Response (JSON):**
```json
{ "ok": true, "len": 26, "channel": 6, "esp_err": 0 }
```
- `esp_err` is the raw `esp_err_t` from `esp_wifi_80211_tx()`. `0` = `ESP_OK`.
- Returns HTTP 500 with `"ok": false` if the driver rejects the frame.

---

### `POST /api/ai/inject_deauth`

Ask the CYD to send a deauthentication burst using AI-chosen parameters.

**Request body (JSON):**
```json
{ "bssid": "AA:BB:CC:DD:EE:FF", "client": "FF:FF:FF:FF:FF:FF",
  "reason": 7, "count": 5, "channel": 6 }
```
| Field | Type | Notes |
|---|---|---|
| `bssid` | string | Target AP MAC (required). |
| `client` | string | Client MAC to kick. Defaults to `FF:FF:FF:FF:FF:FF` (broadcast). |
| `reason` | int | 802.11 reason code. AI picks based on vendor/chipset (e.g. 7 vs 3). Default 7. |
| `count` | int | Frames per burst. Capped at 30. Default 5. |
| `channel` | int | Optional channel hop before TX. |

**Response (JSON):**
```json
{ "ok": true, "bssid": "AA:BB:CC:DD:EE:FF", "client": "FF:FF:FF:FF:FF:FF",
  "reason": 7, "count": 5 }
```

---

### `GET /api/ai/frames`

Fetch all EAPOL frames the YOINK engine has captured, hex-encoded.
The Android AI engine uses this to build a `TargetProfile` — parsing cipher suites,
MIC fields, nonces, and OUI prefixes — without needing file system access.

**Response (JSON — example structure):**
```json
{
  "ok": true,
  "running": true,
  "state": "ATTACKING",
  "target": {
    "ssid": "MyNetwork",
    "bssid": "AA:BB:CC:DD:EE:FF",
    "oui": "AA:BB:CC",
    "rssi": -62,
    "auth": "WPA2_PSK"
  },
  "handshakes": [
    {
      "ssid": "MyNetwork",
      "bssid": "AA:BB:CC:DD:EE:FF",
      "station": "11:22:33:44:55:66",
      "ap_oui": "AA:BB:CC",
      "sta_oui": "11:22:33",
      "mask": 3,
      "valid": true,
      "saved": false,
      "frames": [
        { "msg": 1, "rssi": -62, "ts": 12345678, "eapol_hex": "0103...", "frame_hex": "8800..." },
        { "msg": 2, "rssi": -58, "ts": 12345710, "eapol_hex": "0103...", "frame_hex": "8800..." }
      ]
    }
  ]
}
```

**Android usage:**
1. Poll `GET /api/ai/frames` when YOINK is running.
2. Resolve `ap_oui` / `sta_oui` against the bundled IEEE MA-L CSV → vendor names.
3. Parse `eapol_hex` bytes: offset 1 = key info, offset 13 = nonce, offset 77 = MIC (WPA2/CCMP).
4. Build `TargetProfile` and send to AI engine.
5. Receive AI JSON → call `POST /api/ai/inject_raw` or `POST /api/ai/inject_deauth`.
