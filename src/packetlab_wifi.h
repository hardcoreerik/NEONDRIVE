#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <stdint.h>

// ==========================================
// PACKETLAB WiFi Attack Module (Enhanced)
// ==========================================

// Attack types
enum class PACKETLABAttackType : uint8_t {
  NONE = 0,
  DEAUTH_FLOOD,
  BEACON_SPAM,
  PROBE_FLOOD,
  DEAUTH_BROADCAST  // Deauth to broadcast instead of target
};

// Target info for attack
struct PACKETLABTarget {
  uint8_t bssid[6];
  char ssid[24];  // Reduced from 33 to save DRAM
  uint8_t channel;
};

// Attack statistics
struct PACKETLABStats {
  uint32_t framesSent;
  uint32_t packetsSent;
  uint32_t targetCount;
  uint32_t startTimeMs;
  uint32_t elapsedMs;
  float framesPerSecond;
};

// Attack configuration
struct PACKETLABConfig {
  uint16_t deauthReasonCode = 0x07;  // CLASS3_FRAME_FROM_NONASSOC_STA
  uint16_t floodIntervalMs = 20;     // Send frame every 20ms (50 fps)
  uint8_t beaconTxPower = 20;        // TX power in dBm
  bool channelHoppingEnabled = false;
  bool verboseLogging = false;
};

// Initialize PACKETLAB subsystem
void PACKETLABInit();

// Attack control
void PACKETLABStartDeauthFlood(const PACKETLABTarget& target, uint16_t durationMs);
void PACKETLABStartBeaconSpam(const char* ssid, uint8_t channel, uint16_t durationMs, const PACKETLABTarget* broadcastBssid = nullptr);
void PACKETLABStartProbeFlood(const char* ssid, uint8_t channel, uint16_t durationMs);
void PACKETLABStartDeauthBroadcast(uint8_t channel, uint16_t durationMs);
void PACKETLABStopAttack();

// Status queries
bool PACKETLABIsAttacking();
const char* PACKETLABGetAttackName();
PACKETLABStats PACKETLABGetStats();
PACKETLABAttackType PACKETLABGetCurrentAttackType();
const char* PACKETLABGetLastError();

// Configuration
void PACKETLABSetConfig(const PACKETLABConfig& cfg);
PACKETLABConfig PACKETLABGetConfig();

// Menu/UI hooks
void drawPACKETLABMenu();
void PACKETLABMenuTick();
void PACKETLABAttackTick();  // Called from main loop for background processing

// Helper: Format MAC address
String PACKETLABFormatMAC(const uint8_t* mac);

// Per-frame TX callback fired after every successful esp_wifi_80211_tx.
// Used by main.cpp to feed a deferred PCAP ring buffer without blocking
// the attack loop with SD writes.
typedef void (*PACKETLABFrameTxCallback)(const uint8_t* frame, size_t len, uint8_t channel);
void PACKETLABSetFrameCallback(PACKETLABFrameTxCallback cb);

#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
// These targets do not support raw 802.11 frame injection via esp_wifi_80211_tx.
// Provide no-op stubs so call sites in main.cpp compile without change.
#include <Arduino.h>
inline void PACKETLABInit() {}
inline void PACKETLABStartDeauthFlood(const PACKETLABTarget&, uint16_t) {}
inline void PACKETLABStartBeaconSpam(const char*, uint8_t, uint16_t, const PACKETLABTarget*) {}
inline void PACKETLABStartProbeFlood(const char*, uint8_t, uint16_t) {}
inline void PACKETLABStartDeauthBroadcast(uint8_t, uint16_t) {}
inline void PACKETLABStopAttack() {}
inline bool PACKETLABIsAttacking() { return false; }
inline const char* PACKETLABGetAttackName() { return "NONE"; }
inline PACKETLABStats PACKETLABGetStats() { return PACKETLABStats{}; }
inline PACKETLABAttackType PACKETLABGetCurrentAttackType() { return PACKETLABAttackType::NONE; }
inline const char* PACKETLABGetLastError() { return ""; }
inline void PACKETLABSetConfig(const PACKETLABConfig&) {}
inline PACKETLABConfig PACKETLABGetConfig() { return PACKETLABConfig{}; }
inline void PACKETLABAttackTick() {}
inline void PACKETLABMenuTick() {}
inline String PACKETLABFormatMAC(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}
inline void PACKETLABSetFrameCallback(PACKETLABFrameTxCallback) {}
#endif // NEONDRIVE_TARGET_M5TAB5 || NEONDRIVE_TARGET_M5CARDPUTER

