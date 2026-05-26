// WSL Bypasser — Frame injection enabler for ESP32
// Overrides ieee80211_raw_frame_sanity_check() via -zmuldefs linker flag
// to allow transmission of management frames (deauth, disassoc, etc.)
#pragma once

#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER) \
 || defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
// These targets do not use the classic ESP32 libnet80211 frame injection path.
// Provide no-op stubs so call sites in main.cpp compile without change.
#include <esp_err.h>
namespace WSLBypasser {
  inline bool isActive() { return false; }
  inline esp_err_t sendDeauthFrame(const uint8_t*, const uint8_t*, uint8_t) { return ESP_ERR_NOT_SUPPORTED; }
  inline esp_err_t sendDisassocFrame(const uint8_t*, const uint8_t*, uint8_t) { return ESP_ERR_NOT_SUPPORTED; }
  inline void sendDeauthBurst(const uint8_t*, const uint8_t*, uint8_t) {}
  inline void randomizeMAC() {}
}
#else

#include <Arduino.h>
#include <esp_wifi.h>

// Forward-declare the overridden sanity check (for self-test)
extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t);

namespace WSLBypasser {

// Check if the WSL bypass override is active (Marauder-style self-test)
// Calls the overridden function with magic arg 31337; returns true if override is in place
inline bool isActive() {
    return ieee80211_raw_frame_sanity_check(31337, 0, 0) == 1;
}

// Send a deauth frame: AP → Station (spoofed as AP)
esp_err_t sendDeauthFrame(const uint8_t* bssid, const uint8_t* station, uint8_t reason);

// Send a disassociation frame
esp_err_t sendDisassocFrame(const uint8_t* bssid, const uint8_t* station, uint8_t reason);

// Send a deauth burst (targeted, bidirectional, with jitter)
void sendDeauthBurst(const uint8_t* bssid, const uint8_t* station, uint8_t count);

// Randomize the WiFi STA MAC address
void randomizeMAC();

}  // namespace WSLBypasser

#endif // M5TAB5 || M5CARDPUTER || T_EMBED_CC1101
