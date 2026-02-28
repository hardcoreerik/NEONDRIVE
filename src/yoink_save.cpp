// YOINK Save — PCAP and Hashcat .22000 file writers
// Ported from NEONDRIVE OINK mode for CYD ESP32
#include "yoink_save.h"
#include <SD.h>

// Trigger LED victory flash when handshake/PMKID captured (defined in main.cpp)
extern "C" void triggerLEDVictoryFlash();

// ============= PCAP Structures =============

#pragma pack(push, 1)
struct PCAPGlobalHeader {
    uint32_t magic_number  = 0xA1B2C3D4;
    uint16_t version_major = 2;
    uint16_t version_minor = 4;
    int32_t  thiszone      = 0;
    uint32_t sigfigs        = 0;
    uint32_t snaplen        = 65535;
    uint32_t network        = 127;  // LINKTYPE_IEEE802_11_RADIOTAP
};

struct PCAPPacketHeader {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
};
#pragma pack(pop)

// Minimal 8-byte radiotap header (no optional fields)
static const uint8_t RADIOTAP_HDR[] = {
    0x00, 0x00,             // Header revision, pad
    0x08, 0x00,             // Header length = 8 (little-endian)
    0x00, 0x00, 0x00, 0x00  // Present flags = none
};

// ============= Directory / Filename =============

// Build the per-SSID captures directory: /captures/<SSID>_<BSSID-dashes>/
// Matches the convention used by startSniff() in main.cpp
static void buildCaptureDir(char* out, size_t outLen, const char* ssid, const uint8_t bssid[6]) {
    char sanitized[33] = {0};
    int pos = 0;
    if (ssid) {
        for (int i = 0; ssid[i] && pos < 32; i++) {
            char c = ssid[i];
            sanitized[pos++] = (isalnum(c) || c == '-' || c == '_') ? c : '_';
        }
    }
    if (pos == 0) strcpy(sanitized, "HIDDEN");
    snprintf(out, outLen, "/captures/%s_%02X-%02X-%02X-%02X-%02X-%02X",
             sanitized, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

namespace YoinkSave {

void ensureDirs(const char* ssid, const uint8_t bssid[6]) {
    if (!SD.exists("/captures")) SD.mkdir("/captures");
    char captureDir[160];
    buildCaptureDir(captureDir, sizeof(captureDir), ssid, bssid);
    if (!SD.exists(captureDir)) SD.mkdir(captureDir);
    char hsDir[168];
    snprintf(hsDir, sizeof(hsDir), "%s/Handshakes", captureDir);
    if (!SD.exists(hsDir)) SD.mkdir(hsDir);
}

void buildFilename(char* out, size_t outLen, const char* ssid,
                   const uint8_t bssid[6], const char* suffix, uint8_t seq) {
    // Build the capture dir path (dashes in BSSID for dir name)
    char captureDir[160];
    buildCaptureDir(captureDir, sizeof(captureDir), ssid, bssid);

    // Sanitize SSID for the filename (no separators in BSSID within filename)
    char sanitized[33] = {0};
    int pos = 0;
    if (ssid) {
        for (int i = 0; ssid[i] && pos < 32; i++) {
            char c = ssid[i];
            sanitized[pos++] = (isalnum(c) || c == '-' || c == '_') ? c : '_';
        }
    }
    if (pos == 0) strcpy(sanitized, "HIDDEN");

    if (seq == 0) {
        snprintf(out, outLen, "%s/Handshakes/%s_%02X%02X%02X%02X%02X%02X%s",
                 captureDir, sanitized,
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                 suffix);
    } else {
        snprintf(out, outLen, "%s/Handshakes/%s_%02X%02X%02X%02X%02X%02X_%u%s",
                 captureDir, sanitized,
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                 (unsigned)(seq + 1), suffix);
    }
}

// ============= PCAP Writing Helpers =============

static void writePcapGlobalHeader(File& f) {
    PCAPGlobalHeader hdr;
    f.write((uint8_t*)&hdr, sizeof(hdr));
}

static void writePcapPacket(File& f, const uint8_t* frame, uint16_t len, uint32_t tsMs) {
    uint32_t totalLen = sizeof(RADIOTAP_HDR) + len;
    PCAPPacketHeader pkt;
    pkt.ts_sec = tsMs / 1000;
    pkt.ts_usec = (tsMs % 1000) * 1000;
    pkt.incl_len = totalLen;
    pkt.orig_len = totalLen;

    f.write((uint8_t*)&pkt, sizeof(pkt));
    f.write(RADIOTAP_HDR, sizeof(RADIOTAP_HDR));
    f.write(frame, len);
}

// ============= Handshake PCAP =============

bool saveHandshakePcap(const YoinkHandshake& hs) {
    if (!hs.hasValidPair()) return false;

    ensureDirs(hs.ssid, hs.bssid);
    char path[192];
    buildFilename(path, sizeof(path), hs.ssid, hs.bssid, ".pcap", hs.captureSeq);

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        YoinkEngine::logMessage("[YOINK-SAVE] Failed to open %s", path);
        return false;
    }

    writePcapGlobalHeader(f);

    // 1. Write beacon if captured
    if (hs.beaconLen > 0) {
        writePcapPacket(f, hs.beaconData, hs.beaconLen, hs.firstSeen);
    }

    // 2. Write each captured EAPOL frame
    for (int i = 0; i < 4; i++) {
        if (!(hs.capturedMask & (1 << i))) continue;

        if (hs.frames[i].fullFrameLen > 0) {
            // Prefer the full captured 802.11 frame (best quality)
            writePcapPacket(f, hs.frames[i].fullFrame, hs.frames[i].fullFrameLen,
                            hs.frames[i].timestamp);
        } else if (hs.frames[i].eapolLen > 0) {
            // Fallback: reconstruct 802.11 data + LLC/SNAP + EAPOL
            uint8_t pkt[600];
            uint16_t pktLen = 0;

            // 802.11 Data header (24 bytes)
            pkt[0] = 0x08; // Data frame
            if (i == 0 || i == 2) {  // M1, M3: AP→STA (FromDS=1)
                pkt[1] = 0x02;
                memcpy(pkt + 4, hs.station, 6);   // DA
                memcpy(pkt + 10, hs.bssid, 6);    // SA/BSSID
                memcpy(pkt + 16, hs.bssid, 6);    // BSSID
            } else {  // M2, M4: STA→AP (ToDS=1)
                pkt[1] = 0x01;
                memcpy(pkt + 4, hs.bssid, 6);     // DA
                memcpy(pkt + 10, hs.station, 6);   // SA
                memcpy(pkt + 16, hs.bssid, 6);     // BSSID
            }
            pkt[2] = pkt[3] = 0;
            pkt[22] = pkt[23] = 0;
            pktLen = 24;

            // LLC/SNAP header (8 bytes: 802.1X Authentication = 0x888E)
            uint8_t llc[] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
            memcpy(pkt + pktLen, llc, 8);
            pktLen += 8;

            // EAPOL payload
            uint16_t copyLen = hs.frames[i].eapolLen;
            if (pktLen + copyLen > sizeof(pkt)) copyLen = sizeof(pkt) - pktLen;
            memcpy(pkt + pktLen, hs.frames[i].eapolData, copyLen);
            pktLen += copyLen;

            writePcapPacket(f, pkt, pktLen, hs.frames[i].timestamp);
        }
    }

    f.close();
    YoinkEngine::logMessage("[YOINK-SAVE] PCAP saved: %s (%d EAPOL frames)", path,
                  __builtin_popcount(hs.capturedMask));
    // Flash LED for victory!
    triggerLEDVictoryFlash();
    return true;
}

// ============= Hashcat .22000 — Handshake =============

bool saveHandshake22000(const YoinkHandshake& hs) {
    if (!hs.hasValidPair()) return false;

    // Determine message pair
    uint8_t msgPair;
    const YoinkEapolFrame* nonceFrame;  // M1 or M3 (has ANonce)
    const YoinkEapolFrame* eapolFrame;  // M2 (has MIC)

    if (hs.hasM1() && hs.hasM2()) {
        msgPair = 0x00;
        nonceFrame = &hs.frames[0];  // M1
        eapolFrame = &hs.frames[1];  // M2
    } else if (hs.hasM2() && hs.hasM3()) {
        msgPair = 0x02;
        nonceFrame = &hs.frames[2];  // M3
        eapolFrame = &hs.frames[1];  // M2
    } else {
        return false;
    }

    if (nonceFrame->eapolLen < 51 || eapolFrame->eapolLen < 97) {
        YoinkEngine::logMessage("[YOINK-SAVE] .22000: EAPOL frames too short");
        return false;
    }

    ensureDirs(hs.ssid, hs.bssid);
    char path[192];
    buildFilename(path, sizeof(path), hs.ssid, hs.bssid, "_hs.22000", hs.captureSeq);

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        YoinkEngine::logMessage("[YOINK-SAVE] Failed to open %s", path);
        return false;
    }

    // MIC from M2 (16 bytes at EAPOL offset 81)
    char micHex[33];
    for (int i = 0; i < 16; i++)
        sprintf(micHex + i * 2, "%02x", eapolFrame->eapolData[81 + i]);

    // MAC AP + MAC STA (12 hex chars each)
    char macAP[13], macSTA[13];
    sprintf(macAP, "%02x%02x%02x%02x%02x%02x",
            hs.bssid[0], hs.bssid[1], hs.bssid[2],
            hs.bssid[3], hs.bssid[4], hs.bssid[5]);
    sprintf(macSTA, "%02x%02x%02x%02x%02x%02x",
            hs.station[0], hs.station[1], hs.station[2],
            hs.station[3], hs.station[4], hs.station[5]);

    // ESSID (hex-encoded)
    char essidHex[65] = {0};
    int ssidLen = strlen(hs.ssid);
    if (ssidLen > 32) ssidLen = 32;
    for (int i = 0; i < ssidLen; i++)
        sprintf(essidHex + i * 2, "%02x", (uint8_t)hs.ssid[i]);

    // ANonce from M1/M3 (32 bytes at EAPOL offset 17)
    char nonceHex[65];
    for (int i = 0; i < 32; i++)
        sprintf(nonceHex + i * 2, "%02x", nonceFrame->eapolData[17 + i]);

    // Full EAPOL from M2 with MIC zeroed
    uint16_t eapolTotalLen = ((uint16_t)eapolFrame->eapolData[2] << 8) | eapolFrame->eapolData[3];
    eapolTotalLen += 4;  // Add EAPOL header (version, type, length)
    if (eapolTotalLen > eapolFrame->eapolLen) eapolTotalLen = eapolFrame->eapolLen;
    if (eapolTotalLen > 512) eapolTotalLen = 512;

    uint8_t eapolCopy[512];
    memcpy(eapolCopy, eapolFrame->eapolData, eapolTotalLen);
    memset(eapolCopy + 81, 0, 16);  // Zero MIC field

    // Static buffer for hex encoding (max 512 bytes * 2 + 1)
    static char eapolHex[1025];
    for (int i = 0; i < eapolTotalLen; i++)
        sprintf(eapolHex + i * 2, "%02x", eapolCopy[i]);
    eapolHex[eapolTotalLen * 2] = 0;

    f.printf("WPA*02*%s*%s*%s*%s*%s*%s*%02x\n",
             micHex, macAP, macSTA, essidHex, nonceHex, eapolHex, msgPair);

    f.close();
    YoinkEngine::logMessage("[YOINK-SAVE] .22000 handshake saved: %s", path);
    // Flash LED for victory!
    triggerLEDVictoryFlash();
    return true;
}

// ============= Hashcat .22000 — PMKID =============

bool savePmkid22000(const YoinkPmkid& p) {
    // Skip all-zero PMKIDs
    bool allZero = true;
    for (int i = 0; i < 16; i++) {
        if (p.pmkid[i] != 0) { allZero = false; break; }
    }
    if (allZero || p.ssid[0] == 0) return false;

    ensureDirs(p.ssid, p.bssid);
    char path[192];
    buildFilename(path, sizeof(path), p.ssid, p.bssid, ".22000");

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        YoinkEngine::logMessage("[YOINK-SAVE] Failed to open %s", path);
        return false;
    }

    char pmkidHex[33], macAP[13], macSTA[13];
    for (int i = 0; i < 16; i++)
        sprintf(pmkidHex + i * 2, "%02x", p.pmkid[i]);
    sprintf(macAP, "%02x%02x%02x%02x%02x%02x",
            p.bssid[0], p.bssid[1], p.bssid[2],
            p.bssid[3], p.bssid[4], p.bssid[5]);
    sprintf(macSTA, "%02x%02x%02x%02x%02x%02x",
            p.station[0], p.station[1], p.station[2],
            p.station[3], p.station[4], p.station[5]);

    char essidHex[65] = {0};
    int len = strlen(p.ssid);
    if (len > 32) len = 32;
    for (int i = 0; i < len; i++)
        sprintf(essidHex + i * 2, "%02x", (uint8_t)p.ssid[i]);

    f.printf("WPA*01*%s*%s*%s*%s***01\n", pmkidHex, macAP, macSTA, essidHex);
    f.close();
    YoinkEngine::logMessage("[YOINK-SAVE] PMKID .22000 saved: %s", path);
    // Flash LED for victory!
    triggerLEDVictoryFlash();
    return true;
}

}  // namespace YoinkSave
