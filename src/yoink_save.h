// YOINK Save — PCAP and Hashcat .22000 file writers
#pragma once

#include <Arduino.h>
#include <SD.h>
#include "yoink_engine.h"

namespace YoinkSave {

    // Ensure SD directory structure exists for the given SSID/BSSID
    void ensureDirs(const char* ssid, const uint8_t bssid[6]);

    // Build a capture filename: /captures/<SSID>_<BSSID>/Handshakes/<SSID>_<BSSID_hex>[_N]{suffix}
    // seq=0 → no suffix, seq=1 → _2 appended before extension suffix, etc.
    void buildFilename(char* out, size_t outLen, const char* ssid,
                       const uint8_t bssid[6], const char* suffix, uint8_t seq = 0);

    // Save a complete handshake as PCAP (beacon + EAPOL frames)
    bool saveHandshakePcap(const YoinkHandshake& hs);

    // Save handshake in hashcat .22000 format (WPA*02*...)
    bool saveHandshake22000(const YoinkHandshake& hs);

    // Save PMKID in hashcat .22000 format (WPA*01*...)
    bool savePmkid22000(const YoinkPmkid& p);
}
