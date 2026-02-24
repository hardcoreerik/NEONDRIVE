// YOINK Engine — WPA handshake capture engine ported from M5PORKCHOP OINK mode
// Targets CYD ESP32 (classic, not S3)
#pragma once

#include <Arduino.h>
#include <esp_wifi.h>

// ============= Tuning Constants =============
#define YOINK_MAX_NETWORKS      16
#define YOINK_MAX_CLIENTS       16
#define YOINK_MAX_HANDSHAKES    2
#define YOINK_MAX_PMKIDS        4
#define YOINK_MAX_BOAR_BROS     50

// State machine timing (ms)
#define YOINK_SCAN_DURATION     5000
#define YOINK_PMKID_HUNT_MAX    30000
#define YOINK_LOCK_MIN          2500
#define YOINK_LOCK_MAX          8000
#define YOINK_ATTACK_MAX        15000
#define YOINK_WAIT_DURATION     4500
#define YOINK_WAIT_EXTENDED     9000
#define YOINK_BORED_RETRY       30000
#define YOINK_DEAUTH_INTERVAL   180    // ms between deauth bursts
#define YOINK_DEAUTH_COUNT      5      // frames per burst (per client)
#define YOINK_CHANNEL_HOP_MS    120    // ms between channel hops during scan

// ============= Data Structures =============

struct YoinkClient {
    uint8_t mac[6];
    int8_t rssi;
    uint32_t lastSeen;
};

struct YoinkNetwork {
    uint8_t bssid[6];
    char ssid[33];
    int8_t rssi;
    int8_t rssiAvg;           // Exponential moving average
    uint8_t channel;
    wifi_auth_mode_t authmode;
    uint32_t lastSeen;
    uint32_t lastDataSeen;    // Last data frame timestamp (client activity)
    uint16_t beaconCount;
    bool hasPMF;              // Protected Management Frames
    bool hasHandshake;        // At least one handshake captured
    uint8_t captureCount;     // Total handshakes successfully saved from this AP
    uint8_t attackAttempts;
    bool isHidden;
    uint32_t cooldownUntil;   // Don't re-target until this time
    uint8_t estimatedClients; // From data frame heuristic
};

struct YoinkEapolFrame {
    uint8_t eapolData[512];    // EAPOL payload (for .22000)
    uint8_t fullFrame[200];    // Full 802.11 frame (for PCAP) — trimmed for DRAM
    uint16_t eapolLen;
    uint16_t fullFrameLen;
    uint32_t timestamp;
    int8_t rssi;
};

struct YoinkHandshake {
    uint8_t bssid[6];
    uint8_t station[6];
    char ssid[33];
    YoinkEapolFrame frames[4];   // M1-M4
    uint8_t capturedMask;        // Bits 0-3 for M1-M4
    uint32_t firstSeen;
    uint32_t lastSeen;
    bool saved;
    uint8_t captureSeq;          // Sequence number for this slot (drives filename uniqueness)
    uint8_t beaconData[256];     // Static buffer (trimmed for DRAM)
    uint16_t beaconLen;

    bool hasM1() const { return capturedMask & 0x01; }
    bool hasM2() const { return capturedMask & 0x02; }
    bool hasM3() const { return capturedMask & 0x04; }
    bool hasM4() const { return capturedMask & 0x08; }
    bool hasValidPair() const {
        return (hasM1() && hasM2()) || (hasM2() && hasM3());
    }
    uint8_t getMessagePair() const {
        if (hasM1() && hasM2()) return 0x00;  // M1+M2
        if (hasM2() && hasM3()) return 0x02;  // M2+M3
        return 0xFF;
    }
};

struct YoinkPmkid {
    uint8_t bssid[6];
    uint8_t station[6];
    char ssid[33];
    uint8_t pmkid[16];
    uint32_t timestamp;
    bool saved;
};

// ============= State Machine =============

enum class YoinkState : uint8_t {
    IDLE,
    SCANNING,
    PMKID_HUNTING,
    NEXT_TARGET,
    LOCKING,
    ATTACKING,
    WAITING,
    BORED,
};

// ============= Deferred Event (callback → main loop) =============
// These live in .bss (static) to avoid heap allocation in callback context

struct PendingEapolEvent {
    uint8_t bssid[6];
    uint8_t station[6];
    uint8_t eapolData[512];
    uint8_t fullFrame[200];
    uint16_t eapolLen;
    uint16_t fullFrameLen;
    uint8_t messageNum;   // 1-4
    int8_t rssi;
    uint32_t timestamp;
    bool hasPmkid;
    uint8_t pmkid[16];
    bool pending;         // Atomic-ish flag: callback sets true, main loop clears
};

struct PendingBeaconEvent {
    uint8_t bssid[6];
    char ssid[33];
    uint8_t channel;
    int8_t rssi;
    wifi_auth_mode_t authmode;
    bool hasPMF;
    bool isHidden;
    uint8_t frameData[256];  // Raw beacon frame for PCAP (trimmed for DRAM)
    uint16_t frameLen;
    bool pending;
};

struct PendingClientEvent {
    uint8_t bssid[6];
    uint8_t clientMac[6];
    int8_t rssi;
    bool pending;
};

// ============= Log Ring (for monitor/web/companion) =============

#define YOINK_LOG_RING_SIZE 40

struct YoinkLogEntry {
    uint32_t seq;
    uint32_t ms;
    char msg[96];
    uint8_t severity;  // 0=info, 1=success, 2=warning, 3=error
};

// ============= Public API =============

namespace YoinkEngine {

    // Lifecycle
    void init();
    void start();
    void stop();
    void update();          // Call every loop() iteration
    bool isRunning();

    // State
    YoinkState getState();
    const char* getStateStr();

    // Stats
    uint32_t getPacketCount();
    uint32_t getDeauthCount();
    uint8_t  getNetworkCount();
    uint8_t  getClientCount();
    uint8_t  getHandshakeCount();
    uint8_t  getPmkidCount();
    uint8_t  getCurrentChannel();
    uint8_t  getTotalHandshakeSlots();  // total slots used (including saved)

    // Target info (current attack target)
    const char* getTargetSSID();
    const uint8_t* getTargetBSSID();
    int8_t  getTargetRSSI();
    uint8_t getEapolMask();
    const char* getTargetAuthStr();

    // Network list (for UI display)
    uint8_t getNetworkListCount();
    const YoinkNetwork* getNetwork(uint8_t index);

    // Handshake/PMKID list (for UI display)
    const YoinkHandshake* getHandshake(uint8_t index);
    const YoinkPmkid* getPmkid(uint8_t index);

    // Log ring (for monitor screen, webserver, companion app)
    uint32_t getLogSeq();
    uint8_t  getLogCount();
    const YoinkLogEntry* getLogEntry(uint8_t ringIndex);  // 0 = oldest in ring

    // The promiscuous callback installed by YoinkEngine
    // (public so main.cpp can reference it if needed for the packet ring)
    void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);

    // Log routing — routes YOINK messages to Serial + caller-registered callback
    typedef void (*LogCallback)(const char* msg);
    void setLogCallback(LogCallback cb);
    void logMessage(const char* fmt, ...);
}
