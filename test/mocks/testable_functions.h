// Testable pure functions extracted from core modules
// These functions have no hardware dependencies and can be unit tested
#pragma once

#include <cstdint>
#include <cmath>

// ============================================================================
// XP System - Level Calculations
// From: src/core/xp.cpp
// ============================================================================

// XP thresholds for each level (1-50)
// Level N requires XP_THRESHOLDS[N-1] total XP
static const uint32_t XP_THRESHOLDS[50] = {
    0,       // Level 1: 0 XP
    100,     // Level 2: 100 XP
    300,     // Level 3: 300 XP
    600,     // Level 4
    1000,    // Level 5
    1500,    // Level 6
    2300,    // Level 7
    3400,    // Level 8
    4800,    // Level 9
    6500,    // Level 10
    8500,    // Level 11
    11000,   // Level 12
    14000,   // Level 13
    17500,   // Level 14
    21500,   // Level 15
    26000,   // Level 16
    31000,   // Level 17
    36500,   // Level 18
    42500,   // Level 19
    49000,   // Level 20
    56000,   // Level 21
    64000,   // Level 22
    73000,   // Level 23
    83000,   // Level 24
    94000,   // Level 25
    106000,  // Level 26
    120000,  // Level 27
    136000,  // Level 28
    154000,  // Level 29
    174000,  // Level 30
    197000,  // Level 31
    223000,  // Level 32
    252000,  // Level 33
    284000,  // Level 34
    319000,  // Level 35
    359000,  // Level 36
    404000,  // Level 37
    454000,  // Level 38
    514000,  // Level 39
    600000,  // Level 40
    680000,  // Level 41
    770000,  // Level 42
    870000,  // Level 43
    980000,  // Level 44
    1100000, // Level 45
    1230000, // Level 46
    1370000, // Level 47
    1520000, // Level 48
    1680000, // Level 49
    1850000  // Level 50: 1,850,000 XP
};

static const uint8_t MAX_LEVEL = 50;

// Calculate level from total XP
// Returns level 1-50
inline uint8_t calculateLevel(uint32_t xp) {
    for (uint8_t i = MAX_LEVEL - 1; i > 0; i--) {
        if (xp >= XP_THRESHOLDS[i]) return i + 1;
    }
    return 1;
}

// Get XP required for a specific level
// Clamps to MAX_LEVEL for out-of-range levels, returns 0 for level 0
inline uint32_t getXPForLevel(uint8_t level) {
    if (level <= 1) return 0;
    if (level > MAX_LEVEL) level = MAX_LEVEL;
    return XP_THRESHOLDS[level - 1];
}

// Calculate XP remaining to next level
// Returns 0 if already at max level
inline uint32_t getXPToNextLevel(uint32_t currentXP) {
    uint8_t level = calculateLevel(currentXP);
    if (level >= MAX_LEVEL) return 0;
    return XP_THRESHOLDS[level] - currentXP;
}

// Calculate progress percentage to next level (0-100)
inline uint8_t getLevelProgress(uint32_t currentXP) {
    uint8_t level = calculateLevel(currentXP);
    if (level >= MAX_LEVEL) return 100;
    
    uint32_t currentLevelXP = XP_THRESHOLDS[level - 1];
    uint32_t nextLevelXP = XP_THRESHOLDS[level];
    uint32_t levelRange = nextLevelXP - currentLevelXP;
    uint32_t progress = currentXP - currentLevelXP;
    
    if (levelRange == 0) return 0;
    return (uint8_t)((progress * 100) / levelRange);
}

// ============================================================================
// Distance Calculations
// From: src/modes/warhog.cpp
// ============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Haversine formula for GPS distance calculation
// Returns distance in meters between two lat/lon points
inline double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0;  // Earth radius in meters
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    lat1 = lat1 * M_PI / 180.0;
    lat2 = lat2 * M_PI / 180.0;
    
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1) * cos(lat2) * sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return R * c;
}

// ============================================================================
// 802.11 Frame Parsing Helpers
// ============================================================================

// Check if MAC address is randomized (locally administered bit set)
// The second bit of the first octet indicates locally administered
inline bool isRandomizedMAC(const uint8_t* mac) {
    return (mac[0] & 0x02) != 0;
}

// Check if MAC is multicast (group bit set)
// The first bit of the first octet indicates multicast
inline bool isMulticastMAC(const uint8_t* mac) {
    return (mac[0] & 0x01) != 0;
}

// Normalize a value using z-score normalization
// Returns 0 if std is too small to avoid division by zero
inline float normalizeValue(float value, float mean, float std) {
    if (std < 0.001f) return 0.0f;
    return (value - mean) / std;
}

// Parse beacon interval from raw 802.11 beacon frame
// Returns default 100 if frame is too short
inline uint16_t parseBeaconInterval(const uint8_t* frame, uint16_t len) {
    if (len < 34) return 100;  // Default beacon interval
    // Beacon interval at offset 32 (after 24 byte header + 8 byte timestamp)
    return frame[32] | (frame[33] << 8);
}

// Parse capability info from raw 802.11 beacon frame
inline uint16_t parseCapability(const uint8_t* frame, uint16_t len) {
    if (len < 36) return 0;
    // Capability at offset 34
    return frame[34] | (frame[35] << 8);
}

// ============================================================================
// Achievement Bitfield Operations
// From: src/core/xp.h
// ============================================================================

// Check if an achievement is unlocked
inline bool hasAchievement(uint64_t achievements, uint64_t achievementBit) {
    return (achievements & achievementBit) != 0;
}

// Unlock an achievement
inline uint64_t unlockAchievement(uint64_t achievements, uint64_t achievementBit) {
    return achievements | achievementBit;
}

// Count number of unlocked achievements
inline uint8_t countAchievements(uint64_t achievements) {
    uint8_t count = 0;
    while (achievements) {
        count += achievements & 1;
        achievements >>= 1;
    }
    return count;
}

// ============================================================================
// SSID/String Validation Helpers
// Pure functions for safe string handling
// ============================================================================

// Check if character is printable ASCII (32-126)
inline bool isPrintableASCII(char c) {
    return c >= 32 && c <= 126;
}

// Check if SSID contains only printable characters
// Returns true if all characters are printable, false otherwise
inline bool isValidSSID(const char* ssid, size_t len) {
    if (ssid == nullptr || len == 0) return false;
    if (len > 32) return false;  // Max SSID length
    for (size_t i = 0; i < len; i++) {
        if (!isPrintableASCII(ssid[i])) return false;
    }
    return true;
}

// Check if SSID is hidden (zero-length or all null bytes)
inline bool isHiddenSSID(const uint8_t* ssid, uint8_t len) {
    if (len == 0) return true;
    for (uint8_t i = 0; i < len; i++) {
        if (ssid[i] != 0) return false;
    }
    return true;
}

// Calculate simple checksum of buffer (for integrity checking)
inline uint8_t calculateChecksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum ^= data[i];
    }
    return sum;
}

// ============================================================================
// Channel Validation
// From: src/modes/spectrum.cpp
// ============================================================================

// Check if channel is valid for 2.4GHz band (1-14)
inline bool isValid24GHzChannel(uint8_t channel) {
    return channel >= 1 && channel <= 14;
}

// Check if channel is a non-overlapping channel in US/EU (1, 6, 11)
inline bool isNonOverlappingChannel(uint8_t channel) {
    return channel == 1 || channel == 6 || channel == 11;
}

// Calculate center frequency for 2.4GHz channel in MHz
// Channel 1 = 2412 MHz, each channel +5 MHz (except ch14 = 2484)
inline uint16_t channelToFrequency(uint8_t channel) {
    if (channel < 1 || channel > 14) return 0;
    if (channel == 14) return 2484;
    return 2407 + (channel * 5);
}

// Calculate channel from frequency
inline uint8_t frequencyToChannel(uint16_t freqMHz) {
    if (freqMHz == 2484) return 14;
    if (freqMHz < 2412 || freqMHz > 2472) return 0;
    return (freqMHz - 2407) / 5;
}

// ============================================================================
// RSSI/Signal Helpers
// ============================================================================

// Convert RSSI to signal quality percentage (0-100)
// Uses typical range of -90 dBm (weak) to -30 dBm (strong)
inline uint8_t rssiToQuality(int8_t rssi) {
    if (rssi >= -30) return 100;
    if (rssi <= -90) return 0;
    return (uint8_t)((rssi + 90) * 100 / 60);
}

// Check if RSSI indicates a usable signal (typically > -80 dBm)
inline bool isUsableSignal(int8_t rssi) {
    return rssi > -80;
}

// Check if RSSI indicates excellent signal (typically > -50 dBm)
inline bool isExcellentSignal(int8_t rssi) {
    return rssi > -50;
}

// ============================================================================
// Time/Duration Helpers
// ============================================================================

// Convert milliseconds to TU (Time Units, 1 TU = 1024 microseconds)
// Used for beacon intervals
inline uint16_t msToTU(uint16_t ms) {
    return (uint16_t)((uint32_t)ms * 1000 / 1024);
}

// Convert TU to milliseconds
inline uint16_t tuToMs(uint16_t tu) {
    return (uint16_t)((uint32_t)tu * 1024 / 1000);
}

// ============================================================================
// String Escaping Helpers
// From: src/modes/warhog.cpp (escapeXML, writeCSVField)
// ============================================================================

// Escape a single character for XML output
// Returns pointer to static string with escaped version, or nullptr if no escaping needed
inline const char* escapeXMLChar(char c) {
    switch (c) {
        case '&':  return "&amp;";
        case '<':  return "&lt;";
        case '>':  return "&gt;";
        case '"':  return "&quot;";
        case '\'': return "&apos;";
        default:   return nullptr;  // No escaping needed
    }
}

// Check if a character needs XML escaping
inline bool needsXMLEscape(char c) {
    return c == '&' || c == '<' || c == '>' || c == '"' || c == '\'';
}

// Escape a string for XML output
// Returns the number of characters written to output (not including null terminator)
// If output is nullptr, just returns the required buffer size
// maxInputLen limits how many input chars to process (0 = use strlen)
// maxOutputLen is the size of the output buffer
inline size_t escapeXML(const char* input, char* output, size_t maxInputLen, size_t maxOutputLen) {
    if (input == nullptr) return 0;
    
    size_t inputLen = 0;
    while (input[inputLen] && (maxInputLen == 0 || inputLen < maxInputLen)) {
        inputLen++;
    }
    
    size_t outPos = 0;
    for (size_t i = 0; i < inputLen; i++) {
        const char* escaped = escapeXMLChar(input[i]);
        if (escaped) {
            size_t escLen = 0;
            while (escaped[escLen]) escLen++;
            
            if (output) {
                if (outPos + escLen >= maxOutputLen) break;  // Would overflow
                for (size_t j = 0; j < escLen; j++) {
                    output[outPos++] = escaped[j];
                }
            } else {
                outPos += escLen;
            }
        } else {
            if (output) {
                if (outPos + 1 >= maxOutputLen) break;  // Would overflow
                output[outPos++] = input[i];
            } else {
                outPos++;
            }
        }
    }
    
    if (output && outPos < maxOutputLen) {
        output[outPos] = '\0';
    }
    
    return outPos;
}

// Check if a string needs CSV quoting (contains comma, quote, newline, or CR)
inline bool needsCSVQuoting(const char* str) {
    if (str == nullptr) return false;
    for (size_t i = 0; str[i]; i++) {
        char c = str[i];
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            return true;
        }
    }
    return false;
}

// Check if a character is a control character that should be stripped from CSV
inline bool isCSVControlChar(char c) {
    return c < 32 && c != '\0';  // Control chars except null
}

// Escape a string for CSV output (handles quoting and control char stripping)
// Always wraps in quotes for SSID fields (as per RFC 4180 for fields with special chars)
// Returns the number of characters written to output (not including null terminator)
// If output is nullptr, just returns the required buffer size
// maxInputLen limits how many input chars to process (0 = use strlen, max 32 for SSID)
inline size_t escapeCSV(const char* input, char* output, size_t maxInputLen, size_t maxOutputLen) {
    if (input == nullptr) {
        if (output && maxOutputLen >= 3) {
            output[0] = '"';
            output[1] = '"';
            output[2] = '\0';
        }
        return 2;  // Empty quoted field
    }
    
    // Calculate input length (capped at maxInputLen or 32 for SSID)
    size_t inputLen = 0;
    size_t cap = (maxInputLen > 0 && maxInputLen < 32) ? maxInputLen : 32;
    while (input[inputLen] && inputLen < cap) {
        inputLen++;
    }
    
    size_t outPos = 0;
    
    // Opening quote
    if (output) {
        if (outPos >= maxOutputLen) return 0;
        output[outPos] = '"';
    }
    outPos++;
    
    // Process each character
    for (size_t i = 0; i < inputLen; i++) {
        char c = input[i];
        
        // Skip control characters
        if (isCSVControlChar(c)) continue;
        
        // Double quotes need to be escaped as ""
        if (c == '"') {
            if (output) {
                if (outPos + 2 >= maxOutputLen) break;
                output[outPos++] = '"';
                output[outPos++] = '"';
            } else {
                outPos += 2;
            }
        } else {
            if (output) {
                if (outPos + 1 >= maxOutputLen) break;
                output[outPos++] = c;
            } else {
                outPos++;
            }
        }
    }
    
    // Closing quote
    if (output) {
        if (outPos < maxOutputLen) {
            output[outPos] = '"';
        }
    }
    outPos++;
    
    // Null terminator
    if (output && outPos < maxOutputLen) {
        output[outPos] = '\0';
    }
    
    return outPos;
}

// ============================================================================
// MAC Address Utilities
// From: src/modes/warhog.h (bssidToKey)
// From: src/core/wsl_bypasser.cpp (randomizeMAC)
// ============================================================================

// Convert 6-byte MAC address to 64-bit key for map/set storage
// Uses big-endian packing: mac[0] in highest byte position
inline uint64_t bssidToKey(const uint8_t* bssid) {
    return ((uint64_t)bssid[0] << 40) | ((uint64_t)bssid[1] << 32) |
           ((uint64_t)bssid[2] << 24) | ((uint64_t)bssid[3] << 16) |
           ((uint64_t)bssid[4] << 8) | bssid[5];
}

// Convert 64-bit key back to 6-byte MAC address
inline void keyToBssid(uint64_t key, uint8_t* bssid) {
    bssid[0] = (uint8_t)(key >> 40);
    bssid[1] = (uint8_t)(key >> 32);
    bssid[2] = (uint8_t)(key >> 24);
    bssid[3] = (uint8_t)(key >> 16);
    bssid[4] = (uint8_t)(key >> 8);
    bssid[5] = (uint8_t)key;
}

// Apply locally-administered MAC bit manipulation
// Sets locally administered bit (bit 1 of first byte) and clears multicast bit (bit 0)
// This transforms any MAC into a valid unicast locally-administered address
inline void applyLocalMACBits(uint8_t* mac) {
    mac[0] = (mac[0] & 0xFC) | 0x02;
}

// Check if MAC is a valid locally-administered unicast address
// Locally administered bit (bit 1) set, multicast bit (bit 0) clear
inline bool isValidLocalMAC(const uint8_t* mac) {
    return (mac[0] & 0x03) == 0x02;
}

// Format MAC address as colon-separated hex string
// Output buffer must be at least 18 bytes (17 chars + null)
// Returns number of characters written (not including null)
inline size_t formatMAC(const uint8_t* mac, char* output, size_t maxLen) {
    if (output == nullptr || maxLen < 18) return 0;
    
    const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;
    
    for (int i = 0; i < 6; i++) {
        if (i > 0) {
            output[pos++] = ':';
        }
        output[pos++] = hex[(mac[i] >> 4) & 0x0F];
        output[pos++] = hex[mac[i] & 0x0F];
    }
    output[pos] = '\0';
    return pos;
}

// Parse MAC address from colon-separated hex string
// Returns true if parsing succeeded, false otherwise
inline bool parseMAC(const char* str, uint8_t* mac) {
    if (str == nullptr || mac == nullptr) return false;
    
    int values[6];
    int count = 0;
    const char* p = str;
    
    while (*p && count < 6) {
        // Parse hex byte
        int val = 0;
        for (int i = 0; i < 2; i++) {
            char c = *p++;
            if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
            else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
            else return false;
        }
        values[count++] = val;
        
        // Skip separator if present
        if (*p == ':' || *p == '-') p++;
    }
    
    if (count != 6) return false;
    
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)values[i];
    }
    return true;
}

// ============================================================================
// PCAP File Format Structures
// From: src/modes/oink.cpp (PCAPHeader, PCAPPacketHeader)
// ============================================================================

// PCAP global header (24 bytes)
// Used at start of pcap file
#pragma pack(push, 1)
struct TestPCAPHeader {
    uint32_t magic;           // 0xA1B2C3D4 for little-endian
    uint16_t version_major;   // 2
    uint16_t version_minor;   // 4
    int32_t thiszone;         // GMT offset (usually 0)
    uint32_t sigfigs;         // Timestamp accuracy (usually 0)
    uint32_t snaplen;         // Max capture length (65535)
    uint32_t linktype;        // 105 = LINKTYPE_IEEE802_11
};

// PCAP packet header (16 bytes)
// Precedes each captured packet
struct TestPCAPPacketHeader {
    uint32_t ts_sec;          // Timestamp seconds
    uint32_t ts_usec;         // Timestamp microseconds
    uint32_t incl_len;        // Captured length
    uint32_t orig_len;        // Original length
};
#pragma pack(pop)

// PCAP magic numbers
static const uint32_t PCAP_MAGIC_LE = 0xA1B2C3D4;  // Little-endian
static const uint32_t PCAP_MAGIC_BE = 0xD4C3B2A1;  // Big-endian
static const uint32_t LINKTYPE_IEEE802_11 = 105;

// Initialize PCAP global header with standard values
inline void initPCAPHeader(TestPCAPHeader* hdr) {
    hdr->magic = PCAP_MAGIC_LE;
    hdr->version_major = 2;
    hdr->version_minor = 4;
    hdr->thiszone = 0;
    hdr->sigfigs = 0;
    hdr->snaplen = 65535;
    hdr->linktype = LINKTYPE_IEEE802_11;
}

// Initialize PCAP packet header from timestamp and length
inline void initPCAPPacketHeader(TestPCAPPacketHeader* hdr, uint32_t ts_ms, uint16_t len) {
    hdr->ts_sec = ts_ms / 1000;
    hdr->ts_usec = (ts_ms % 1000) * 1000;
    hdr->incl_len = len;
    hdr->orig_len = len;
}

// Validate PCAP header magic and version
inline bool isValidPCAPHeader(const TestPCAPHeader* hdr) {
    if (hdr->magic != PCAP_MAGIC_LE && hdr->magic != PCAP_MAGIC_BE) return false;
    if (hdr->version_major != 2) return false;
    if (hdr->version_minor != 4) return false;
    return true;
}

// ============================================================================
// Deauth Frame Construction
// From: src/core/wsl_bypasser.cpp (sendDeauthFrame)
// ============================================================================

// Deauth frame size
static const size_t DEAUTH_FRAME_SIZE = 26;

// Deauth frame offsets
static const size_t DEAUTH_OFFSET_FRAME_CTRL = 0;   // 2 bytes
static const size_t DEAUTH_OFFSET_DURATION = 2;     // 2 bytes
static const size_t DEAUTH_OFFSET_DA = 4;           // 6 bytes (destination address)
static const size_t DEAUTH_OFFSET_SA = 10;          // 6 bytes (source address)
static const size_t DEAUTH_OFFSET_BSSID = 16;       // 6 bytes
static const size_t DEAUTH_OFFSET_SEQ = 22;         // 2 bytes (sequence control)
static const size_t DEAUTH_OFFSET_REASON = 24;      // 2 bytes (reason code)

// Frame control values
static const uint16_t FRAME_CTRL_DEAUTH = 0x00C0;   // Type: Management, Subtype: Deauth
static const uint16_t FRAME_CTRL_DISASSOC = 0x00A0; // Type: Management, Subtype: Disassoc

// Build a deauth frame in provided buffer (must be >= 26 bytes)
// Returns frame size (always 26)
inline size_t buildDeauthFrame(uint8_t* frame, const uint8_t* bssid, 
                                const uint8_t* station, uint8_t reason) {
    // Frame control (deauth)
    frame[0] = 0xC0;
    frame[1] = 0x00;
    
    // Duration
    frame[2] = 0x00;
    frame[3] = 0x00;
    
    // Destination address (station being deauthed)
    for (int i = 0; i < 6; i++) {
        frame[DEAUTH_OFFSET_DA + i] = station[i];
    }
    
    // Source address (spoofed as AP)
    for (int i = 0; i < 6; i++) {
        frame[DEAUTH_OFFSET_SA + i] = bssid[i];
    }
    
    // BSSID
    for (int i = 0; i < 6; i++) {
        frame[DEAUTH_OFFSET_BSSID + i] = bssid[i];
    }
    
    // Sequence control
    frame[22] = 0x00;
    frame[23] = 0x00;
    
    // Reason code (2 bytes, little endian)
    frame[24] = reason;
    frame[25] = 0x00;
    
    return DEAUTH_FRAME_SIZE;
}

// Build a disassoc frame (same structure, different frame control)
inline size_t buildDisassocFrame(uint8_t* frame, const uint8_t* bssid,
                                  const uint8_t* station, uint8_t reason) {
    size_t len = buildDeauthFrame(frame, bssid, station, reason);
    // Change frame control to disassoc
    frame[0] = 0xA0;
    return len;
}

// Verify deauth frame structure
inline bool isValidDeauthFrame(const uint8_t* frame, size_t len) {
    if (len < DEAUTH_FRAME_SIZE) return false;
    if (frame[0] != 0xC0 || frame[1] != 0x00) return false;
    return true;
}

// Verify disassoc frame structure
inline bool isValidDisassocFrame(const uint8_t* frame, size_t len) {
    if (len < DEAUTH_FRAME_SIZE) return false;
    if (frame[0] != 0xA0 || frame[1] != 0x00) return false;
    return true;
}
