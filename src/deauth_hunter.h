// Deauth Hunter - WiFi Deauthentication & Disassociation Attack Detection
// Enhanced for CYD with scrolling console display and comprehensive stats
// Better than NEMO with filtering, MAC tracking, and real-time analysis

#ifndef DEAUTH_HUNTER_H
#define DEAUTH_HUNTER_H

#include "WiFi.h"
#include "esp_wifi.h"
#include <vector>

// Configuration
#define MAX_LOG_ENTRIES 12        // Ring buffer size for console log
#define CHANNEL_HOP_MS 250        // Channel dwell time  
#define MAX_TRACKED_ATTACKERS 5   // Track top attackers
#define MAX_UNIQUE_MACS 30        // Max unique MACs to track

// WiFi 802.11 management frame types
#define FRAME_TYPE_DEAUTH 0xC0
#define FRAME_TYPE_DISASSOC 0xA0

// Deauth event log entry (minimized for DRAM efficiency)
struct DeauthEvent {
  uint32_t timestamp;
  uint8_t src_mac[6];
  uint8_t dst_mac[6];
  int8_t rssi;
  uint8_t channel;
  uint8_t reason;
  bool is_disassoc;  // false = deauth, true = disassoc
};

// Attacker tracking entry
struct AttackerInfo {
  uint8_t mac[6];
  uint16_t frame_count;
  uint32_t last_seen;
  int8_t avg_rssi;
};

// MAC address wrapper for vector storage
struct MacAddress {
  uint8_t addr[6];
  MacAddress() { memset(addr, 0, 6); }
  MacAddress(const uint8_t* mac) { memcpy(addr, mac, 6); }
};

// Comprehensive statistics
struct DeauthStats {
  uint32_t total_deauths;
  uint32_t total_disassocs;
  uint16_t unique_sources;  // Changed from uint32_t to save space
  uint16_t unique_targets;  // Changed from uint32_t to save space
  uint8_t channel_counts[13];  // Per-channel counts (changed from uint32_t to uint8_t, will roll over at 255)
  int32_t rssi_sum;
  uint32_t rssi_count;
  int8_t avg_rssi;
  uint8_t most_active_channel;
  uint32_t session_start_time;
  uint32_t last_event_time;
};

// Deauth Hunter class - all state and logic encapsulated
class DeauthHunter {
public:
  static void init();
  static void start();
  static void stop();
  static void update();
  static void reset();
  static void setEnabled(bool enabled);
  static bool isEnabled();
  static void ingestPromiscuousPacket(const wifi_promiscuous_pkt_t* pkt,
                                      wifi_promiscuous_pkt_type_t type);
  static uint16_t getPps();
  static int8_t getLastRssi();
  
  // State queries
  static bool isActive() { return active; }
  static uint8_t getCurrentChannel() { return currentChannel; }
  static uint8_t getChannelFilter() { return channelFilter; }  // 0 = hopping (all), 1..13 = locked
  static const DeauthStats& getStats() { static DeauthStats empty = {}; return stats ? *stats : empty; }
  static uint32_t getTotalEvents() { return stats ? (stats->total_deauths + stats->total_disassocs) : 0; }
  
  // Log access for UI
  static uint16_t getLogCount() { return logCount; }
  static const DeauthEvent* getLogEntry(uint16_t index);  // Returns nullptr if invalid
  static const std::vector<AttackerInfo>& getTopAttackers() { static std::vector<AttackerInfo> empty; return topAttackers ? *topAttackers : empty; }
  
  // Filtering options (future enhancement)
  static void setChannelFilter(uint8_t channel);  // 0 = all channels
  static void clearFilters();

private:
  static bool active;
  static bool enabled;
  static uint8_t currentChannel;
  static uint8_t channelIndex;
  static uint32_t lastChannelHop;
  static uint8_t channelFilter;
  static uint32_t ppsWindowCount;
  static uint16_t currentPps;
  static uint32_t lastPpsCommitMs;
  static int8_t lastRssi;
  
  // Data storage
  static DeauthEvent* logBuffer;  // Heap-allocated ring buffer
  static uint16_t logHead;   // Next write position
  static uint16_t logCount;  // Total entries (capped at MAX_LOG_ENTRIES)
  static DeauthStats* stats;  // Heap-allocated to save DRAM
  static std::vector<AttackerInfo>* topAttackers;  // Heap-allocated to save DRAM
  
  // Frame processing
  static void processDeauthFrame(const uint8_t* frame, uint16_t len, int8_t rssi, uint8_t channel, bool is_disassoc);
  static void addLogEntry(const DeauthEvent& event);
  static void updateAttackerTracking(const uint8_t* mac, int8_t rssi);
  static void updateStats(const DeauthEvent& event);
  static void hopChannel();
};

// MAC address formatting helper
inline void formatMac(const uint8_t* mac, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Legacy compatibility functions (wrapper around class methods)
inline void deauth_hunter_setup() { DeauthHunter::init(); }
inline void deauth_hunter_loop() { DeauthHunter::update(); }
inline void start_deauth_monitoring() { DeauthHunter::start(); }
inline void stop_deauth_monitoring() { DeauthHunter::stop(); }

#endif
