#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <stdint.h>

// ==========================================
// Bruce WiFi Attack Module (Enhanced)
// ==========================================

// Attack types
enum class BruceAttackType : uint8_t {
  NONE = 0,
  DEAUTH_FLOOD,
  BEACON_SPAM,
  PROBE_FLOOD,
  DEAUTH_BROADCAST  // Deauth to broadcast instead of target
};

// Target info for attack
struct BruceTarget {
  uint8_t bssid[6];
  char ssid[24];  // Reduced from 33 to save DRAM
  uint8_t channel;
};

// Attack statistics
struct BruceStats {
  uint32_t framesSent;
  uint32_t packetsSent;
  uint32_t targetCount;
  uint32_t startTimeMs;
  uint32_t elapsedMs;
  float framesPerSecond;
};

// Attack configuration
struct BruceConfig {
  uint16_t deauthReasonCode = 0x07;  // CLASS3_FRAME_FROM_NONASSOC_STA
  uint16_t floodIntervalMs = 20;     // Send frame every 20ms (50 fps)
  uint8_t beaconTxPower = 20;        // TX power in dBm
  bool channelHoppingEnabled = false;
  bool verboseLogging = false;
};

// Initialize Bruce subsystem
void bruceInit();

// Attack control
void bruceStartDeauthFlood(const BruceTarget& target, uint16_t durationMs);
void bruceStartBeaconSpam(const char* ssid, uint8_t channel, uint16_t durationMs, const BruceTarget* broadcastBssid = nullptr);
void bruceStartProbeFlood(const char* ssid, uint8_t channel, uint16_t durationMs);
void bruceStartDeauthBroadcast(uint8_t channel, uint16_t durationMs);
void bruceStopAttack();

// Status queries
bool bruceIsAttacking();
const char* bruceGetAttackName();
BruceStats bruceGetStats();
BruceAttackType bruceGetCurrentAttackType();
const char* bruceGetLastError();

// Configuration
void bruceSetConfig(const BruceConfig& cfg);
BruceConfig bruceGetConfig();

// Menu/UI hooks
void drawBruceMenu();
void bruceMenuTick();
void bruceAttackTick();  // Called from main loop for background processing

// Helper: Format MAC address
String bruceFormatMAC(const uint8_t* mac);

// Per-frame TX callback fired after every successful esp_wifi_80211_tx.
// Used by main.cpp to feed a deferred PCAP ring buffer without blocking
// the attack loop with SD writes.
typedef void (*BruceFrameTxCallback)(const uint8_t* frame, size_t len, uint8_t channel);
void bruceSetFrameCallback(BruceFrameTxCallback cb);

#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
// These targets do not support raw 802.11 frame injection via esp_wifi_80211_tx.
// Provide no-op stubs so call sites in main.cpp compile without change.
#include <Arduino.h>
inline void bruceInit() {}
inline void bruceStartDeauthFlood(const BruceTarget&, uint16_t) {}
inline void bruceStartBeaconSpam(const char*, uint8_t, uint16_t, const BruceTarget*) {}
inline void bruceStartProbeFlood(const char*, uint8_t, uint16_t) {}
inline void bruceStartDeauthBroadcast(uint8_t, uint16_t) {}
inline void bruceStopAttack() {}
inline bool bruceIsAttacking() { return false; }
inline const char* bruceGetAttackName() { return "NONE"; }
inline BruceStats bruceGetStats() { return BruceStats{}; }
inline BruceAttackType bruceGetCurrentAttackType() { return BruceAttackType::NONE; }
inline const char* bruceGetLastError() { return ""; }
inline void bruceSetConfig(const BruceConfig&) {}
inline BruceConfig bruceGetConfig() { return BruceConfig{}; }
inline void bruceAttackTick() {}
inline void bruceMenuTick() {}
inline String bruceFormatMAC(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}
inline void bruceSetFrameCallback(BruceFrameTxCallback) {}
#endif // NEONDRIVE_TARGET_M5TAB5 || NEONDRIVE_TARGET_M5CARDPUTER

