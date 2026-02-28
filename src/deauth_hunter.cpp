// Deauth Hunter Implementation
#include "deauth_hunter.h"
#include "esp_wifi_types.h"
#include <cstring>

// Static member initialization
bool DeauthHunter::active = false;
uint8_t DeauthHunter::currentChannel = 1;
uint8_t DeauthHunter::channelIndex = 0;
uint32_t DeauthHunter::lastChannelHop = 0;
uint8_t DeauthHunter::channelFilter = 0;

DeauthEvent* DeauthHunter::logBuffer = nullptr;  // Heap-allocated
uint16_t DeauthHunter::logHead = 0;
uint16_t DeauthHunter::logCount = 0;
DeauthStats* DeauthHunter::stats = nullptr;  // Heap-allocated
std::vector<AttackerInfo>* DeauthHunter::topAttackers = nullptr;  // Heap-allocated

// Channel list (2.4GHz) - stored in flash to save DRAM
static const uint8_t PROGMEM CHANNELS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static const uint8_t CHANNEL_COUNT = sizeof(CHANNELS) / sizeof(CHANNELS[0]);

void DeauthHunter::init() {
  // Allocate log buffer on heap (saves DRAM)
  if (!logBuffer) {
    logBuffer = (DeauthEvent*)malloc(sizeof(DeauthEvent) * MAX_LOG_ENTRIES);
    if (!logBuffer) {
      Serial.println("[DeauthHunter] ERROR: Failed to allocate log buffer!");
      return;
    }
  }
  
  // Allocate topAttackers vector on heap
  if (!topAttackers) {
    topAttackers = new std::vector<AttackerInfo>();
    if (topAttackers) {
      topAttackers->reserve(MAX_TRACKED_ATTACKERS);
    }
  }
  
  // Allocate stats on heap
  if (!stats) {
    stats = new DeauthStats();
  }
  
  // Reset all state
  active = false;
  currentChannel = 1;
  channelIndex = 0;
  lastChannelHop = 0;
  channelFilter = 0;
  logHead = 0;
  logCount = 0;
  memset(stats, 0, sizeof(DeauthStats));
  memset(logBuffer, 0, sizeof(DeauthEvent) * MAX_LOG_ENTRIES);
  topAttackers->clear();
  
  Serial.println("[DeauthHunter] Initialized");
}

void DeauthHunter::start() {
  if (active) return;
  
  Serial.println("[DeauthHunter] Starting...");
  
  // Initialize WiFi in promiscuous mode
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);
  
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&DeauthHunter::promiscuousCallback);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  
  if (stats) stats->session_start_time = millis();
  active = true;

  Serial.printf("[DeauthHunter] Active on channel %u\n", (unsigned)currentChannel);
}

void DeauthHunter::stop() {
  if (!active) return;
  
  Serial.println("[DeauthHunter] Stopping...");
  
  esp_wifi_set_promiscuous(false);
  active = false;
  
  if (stats) {
    Serial.printf("[DeauthHunter] Session stats: %lu deauths, %lu disassocs\n",
                  stats->total_deauths, stats->total_disassocs);
  }
}

void DeauthHunter::update() {
  if (!active) return;
  
  // Handle channel hopping
  uint32_t now = millis();
  if (now - lastChannelHop >= CHANNEL_HOP_MS) {
    hopChannel();
    lastChannelHop = now;
  }
}

void DeauthHunter::reset() {
  Serial.println("[DeauthHunter] Resetting stats...");
  
  if (!logBuffer) return;
  
  logHead = 0;
  logCount = 0;
  if (stats) memset(stats, 0, sizeof(DeauthStats));
  memset(logBuffer, 0, sizeof(DeauthEvent) * MAX_LOG_ENTRIES);
  if (topAttackers) topAttackers->clear();
  
  if (active && stats) {
    stats->session_start_time = millis();
  }
}

const DeauthEvent* DeauthHunter::getLogEntry(uint16_t index) {
  if (!logBuffer) return nullptr;
  if (index >= logCount || index >= MAX_LOG_ENTRIES) return nullptr;
  
  // Calculate actual buffer position (ring buffer)
  uint16_t pos;
  if (logCount < MAX_LOG_ENTRIES) {
    // Buffer not full yet - simple index
    pos = index;
  } else {
    // Buffer full - calculate from head
    pos = (logHead + index) % MAX_LOG_ENTRIES;
  }
  
  return &logBuffer[pos];
}

void DeauthHunter::setChannelFilter(uint8_t channel) {
  channelFilter = (channel >= 1 && channel <= 13) ? channel : 0;
  if (channelFilter) {
    char chBuf[4];
    snprintf(chBuf, sizeof(chBuf), "%u", (unsigned)channelFilter);
    Serial.printf("[DeauthHunter] Channel filter: %s\n", chBuf);
  } else {
    Serial.println("[DeauthHunter] Channel filter: ALL");
  }
}

void DeauthHunter::clearFilters() {
  channelFilter = 0;
  Serial.println("[DeauthHunter] Filters cleared");
}

void DeauthHunter::promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* frame = pkt->payload;
  const uint16_t len = pkt->rx_ctrl.sig_len;
  
  if (len < 24) return;  // Minimum management frame size
  
  // Extract frame control
  uint8_t frame_type = frame[0];
  uint8_t channel = pkt->rx_ctrl.channel;
  int8_t rssi = pkt->rx_ctrl.rssi;
  
  // Check for deauth (0xC0) or disassociation (0xA0)
  if (frame_type == FRAME_TYPE_DEAUTH || frame_type == FRAME_TYPE_DISASSOC) {
    bool is_disassoc = (frame_type == FRAME_TYPE_DISASSOC);
    processDeauthFrame(frame, len, rssi, channel, is_disassoc);
  }
}

void DeauthHunter::processDeauthFrame(const uint8_t* frame, uint16_t len, int8_t rssi, uint8_t channel, bool is_disassoc) {
  // Apply channel filter if set
  if (channelFilter != 0 && channel != channelFilter) return;
  
  // Parse 802.11 management frame header
  // Format: [FC:2] [Duration:2] [DA:6] [SA:6] [BSSID:6] [SeqCtrl:2] [Reason:2]
  if (len < 26) return;
  
  DeauthEvent event;
  event.timestamp = millis();
  event.rssi = rssi;
  event.channel = channel;
  event.is_disassoc = is_disassoc;
  event.reason = (len >= 26) ? frame[24] : 0;  // Reason code at offset 24
  
  // Extract MACs
  memcpy(event.dst_mac, frame + 4, 6);   // Destination (receiver)
  memcpy(event.src_mac, frame + 10, 6);  // Source (transmitter)
  // BSSID (frame + 16) omitted to save memory
  
  // Add to log
  addLogEntry(event);
  
  // Update tracking
  updateAttackerTracking(event.src_mac, rssi);
  updateStats(event);
  
  // Debug output (optional - can be disabled for performance)
  if (getTotalEvents() % 10 == 1) {  // Print every 10th frame to avoid spam
    char macStr[18];
    formatMac(event.src_mac, macStr);
    Serial.printf("[DeauthHunter] %s from %s on CH%d RSSI:%ddB\n",
                  is_disassoc ? "DISASSOC" : "DEAUTH", macStr, channel, rssi);
  }
}

void DeauthHunter::addLogEntry(const DeauthEvent& event) {
  logBuffer[logHead] = event;
  logHead = (logHead + 1) % MAX_LOG_ENTRIES;
  if (logCount < MAX_LOG_ENTRIES) logCount++;
}

void DeauthHunter::updateAttackerTracking(const uint8_t* mac, int8_t rssi) {
  if (!topAttackers) return;
  
  // Find existing attacker
  for (auto& attacker : *topAttackers) {
    if (memcmp(attacker.mac, mac, 6) == 0) {
      attacker.frame_count++;
      attacker.last_seen = millis();
      // Update rolling average RSSI
      attacker.avg_rssi = (attacker.avg_rssi * 3 + rssi) / 4;
      return;
    }
  }
  
  // New attacker - add if we have space
  if (topAttackers->size() < MAX_TRACKED_ATTACKERS) {
    AttackerInfo newAttacker;
    memcpy(newAttacker.mac, mac, 6);
    newAttacker.frame_count = 1;
    newAttacker.last_seen = millis();
    newAttacker.avg_rssi = rssi;
    topAttackers->push_back(newAttacker);
  } else {
    // Replace lowest count attacker if this is more active
    auto minIt = topAttackers->begin();
    for (auto it = topAttackers->begin(); it != topAttackers->end();  ++it) {
      if (it->frame_count < minIt->frame_count) minIt = it;
    }
    if (minIt->frame_count == 1) {  // Replace single-hit attackers
      memcpy(minIt->mac, mac, 6);
      minIt->frame_count = 1;
      minIt->last_seen = millis();
      minIt->avg_rssi = rssi;
    }
  }
  
  // Sort by frame count (descending)
  std::sort(topAttackers->begin(), topAttackers->end(),
            [](const AttackerInfo& a, const AttackerInfo& b) {
              return a.frame_count > b.frame_count;
            });
}

void DeauthHunter::updateStats(const DeauthEvent& event) {
  if (!stats) return;
  
  // Increment counters
  if (event.is_disassoc) {
    stats->total_disassocs++;
  } else {
    stats->total_deauths++;
  }
  
  // Update per-channel stats
  if (event.channel >= 1 && event.channel <= 13) {
    if (stats->channel_counts[event.channel - 1] < 255) {  // Prevent rollover
      stats->channel_counts[event.channel - 1]++;
    }
  }
  
  // Update RSSI stats
  stats->rssi_sum += event.rssi;
  stats->rssi_count++;
  stats->avg_rssi = stats->rssi_count > 0 ? (stats->rssi_sum / stats->rssi_count) : -90;
  
  // Estimate unique sources/targets from attacker tracking
  stats->unique_sources = topAttackers ? topAttackers->size() : 0;
  stats->unique_targets = stats->unique_sources; //  Approximate estimate
  
  // Find most active channel
  uint32_t maxCount = 0;
  for (uint8_t i = 0; i < 13; i++) {
    if (stats->channel_counts[i] > maxCount) {
      maxCount = stats->channel_counts[i];
      stats->most_active_channel = i + 1;
    }
  }
  
  stats->last_event_time = event.timestamp;
}

void DeauthHunter::hopChannel() {
  channelIndex = (channelIndex + 1) % CHANNEL_COUNT;
  currentChannel = pgm_read_byte(&CHANNELS[channelIndex]);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}
