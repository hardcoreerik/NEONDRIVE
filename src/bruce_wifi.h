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

