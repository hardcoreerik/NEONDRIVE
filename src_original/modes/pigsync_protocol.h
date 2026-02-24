/**
 * PigSync Protocol - ESP-NOW Sync between POPS (Porkchop) and SON (Sirloin)
 * 
 * SON OF A PIG - Reliable, encrypted, low-latency sync
 * 
 * This header MUST be identical on both devices.
 */

#ifndef PIGSYNC_PROTOCOL_H
#define PIGSYNC_PROTOCOL_H

#include <Arduino.h>

// ==[ PROTOCOL VERSION ]==
#define PIGSYNC_VERSION         0x30    // PigSync

// ==[ MAGIC BYTES ]==
#define PIGSYNC_MAGIC           0x50    // 'P' for Porkchop family

// ==[ ENCRYPTION KEYS ]== (must match on both devices)
// PMK (Primary Master Key) - set once at ESP-NOW init via esp_now_set_pmk()
static const uint8_t PIGSYNC_PMK[16] = {
    'S', 'O', 'N', 'O', 'F', 'A', 'P', 'I',
    'G', 'K', 'E', 'Y', '2', '0', '2', '4'
};
// LMK (Local Master Key) - set per-peer for encrypted unicast
static const uint8_t PIGSYNC_LMK[16] = {
    'P', 'O', 'R', 'K', 'C', 'H', 'O', 'P',
    'S', 'I', 'R', 'L', 'O', 'I', 'N', '!'
};

// ==[ TIMING CONSTANTS ]==
#define PIGSYNC_DISCOVERY_INTERVAL  100     // ms between discovery broadcasts
#define PIGSYNC_DISCOVERY_TIMEOUT   5000    // ms total discovery time
#define PIGSYNC_HELLO_TIMEOUT       15000   // ms to wait for RSP_HELLO (increased from 8000)
#define PIGSYNC_CHUNK_ACK_TIMEOUT   500     // ms to wait for chunk ACK (increased from 100)
#define PIGSYNC_TRANSFER_TIMEOUT    60000   // ms max for complete transfer (increased from 5000)
#define PIGSYNC_RETRY_COUNT         5       // retries per chunk
#define PIGSYNC_HELLO_RETRIES       6       // retries for CMD_HELLO/CMD_READY

// ==[ DIALOGUE TIMING ]==
#define PIGSYNC_PHRASE_DURATION     2500    // ms per dialogue bubble
#define PIGSYNC_TOAST_DURATION      3500    // ms for Papa toast overlay
#define PIGSYNC_PHASE_WATCHDOG      20000   // ms max per dialogue phase

// ==[ DATA LIMITS ]==
#define PIGSYNC_MAX_PAYLOAD         238     // ESP-NOW 250 - 12 (RspChunk header)
#define PIGSYNC_TX_BUFFER_SIZE      2048    // max serialized capture
#define PIGSYNC_MAX_BOUNTIES        15      // max bounty BSSIDs

// ==[ RELIABILITY ]==
#define PIGSYNC_ACK_TIMEOUT         500     // ms to wait for ACK (increased from 200)
#define PIGSYNC_MAX_RETRIES         3       // retries before failure
#define PIGSYNC_SEQ_WINDOW          64      // duplicate detection window

// ==[ WIFI CHANNEL ]==
#define PIGSYNC_DISCOVERY_CHANNEL   1       // Discovery always on channel 1
#define PIGSYNC_CHANNEL_SWITCH_MS   50      // Wait after channel switch
#define PIGSYNC_READY_TIMEOUT       5000    // Timeout for CMD_READY handshake (increased from 2000)

// ==[ BEACON (PHASE 3) ]==
#define PIGSYNC_BEACON_INTERVAL     5000    // ms between beacon broadcasts
#define PIGSYNC_BEACON_CHANNEL      1       // Beacons on discovery channel

// ==[ PACKET FLAGS ]==
#define PIGSYNC_FLAG_ACK_REQUIRED   0x01
#define PIGSYNC_FLAG_ENCRYPTED      0x02
#define PIGSYNC_FLAG_FRAGMENTED     0x04
#define PIGSYNC_FLAG_LAST_FRAGMENT  0x08

// ==[ COMMANDS (POPS → SON) ]==
#define CMD_DISCOVER        0x01    // Broadcast discovery ping
#define CMD_HELLO           0x02    // Initiate sync session
#define CMD_READY           0x03    // Confirm data channel switch
#define CMD_DISCONNECT      0x04    // Graceful disconnect
#define CMD_GET_COUNT       0x10    // Request capture counts
#define CMD_START_SYNC      0x11    // Request capture at index
#define CMD_ACK_CHUNK       0x12    // Acknowledge data chunk
#define CMD_MARK_SYNCED     0x13    // Mark capture as synced
#define CMD_PURGE           0x14    // Purge synced + goodbye
#define CMD_BOUNTIES        0x15    // Send bounty list
#define CMD_ABORT           0x16    // Abort current transfer
#define CMD_TIME_SYNC       0x18    // Request time sync (Phase 3)

// ==[ LAYER 0 BEACONS (SON → broadcast) ]==
#define BEACON_GRUNT        0xB0    // Passive status broadcast

// ==[ RESPONSES (SON → POPS) ]==
#define RSP_RING            0x80    // Ringing (accept pending)
#define RSP_BEACON          0x81    // Discovery response
#define RSP_HELLO           0x82    // Session accepted + data channel
#define RSP_READY           0x83    // Data channel confirmed
#define RSP_OK              0x84    // Generic success
#define RSP_ERROR           0x85    // Error with code
#define RSP_DISCONNECT      0x86    // Graceful disconnect (SON→POPS)
#define RSP_COUNT           0x90    // Capture counts
#define RSP_CHUNK           0x91    // Data chunk (fragmented)
#define RSP_COMPLETE        0x92    // Transfer complete with CRC
#define RSP_PURGED          0x93    // Purge complete + bounty matches
#define RSP_BOUNTIES_ACK    0x94    // Bounty list received
#define RSP_TIME_SYNC       0x96    // Time sync response (Phase 3)

// ==[ ERROR CODES ]==
// Prefixed with PIGSYNC_ to avoid collision with lwip ERR_* macros
#define PIGSYNC_ERR_INVALID_CMD     0x01    // Unknown command type
#define PIGSYNC_ERR_INVALID_INDEX   0x02    // Capture index out of range
#define PIGSYNC_ERR_BUSY            0x03    // Transfer already in progress
#define PIGSYNC_ERR_NO_CAPTURES     0x04    // No captures to sync
#define PIGSYNC_ERR_TIMEOUT         0x05    // Operation timed out
#define PIGSYNC_ERR_CRC_FAIL        0x06    // CRC verification failed
#define PIGSYNC_ERR_NOT_READY       0x07    // Son hasn't accepted call
#define PIGSYNC_ERR_SERIALIZE_FAIL  0x08    // Failed to serialize capture
#define PIGSYNC_ERR_BUFFER_OVERFLOW 0x09    // Buffer too small

// ==[ DEVICE FLAGS ]==
#define FLAG_HUNTING        0x01    // Device is actively hunting
#define FLAG_BUFFER_FULL    0x02    // Capture buffer full
#define FLAG_LOW_BATTERY    0x04    // Battery low warning
#define FLAG_CALL_ACTIVE    0x08    // Sync call in progress

// ==[ CAPTURE TYPES ]==
#define CAPTURE_TYPE_PMKID      0x01
#define CAPTURE_TYPE_HANDSHAKE  0x02

// ==[ DIALOGUE TRACK COUNT ]==
#define DIALOGUE_TRACK_COUNT    3   // Number of dialogue variations

#pragma pack(push, 1)

// ==[ BASE HEADER (8 bytes) ]==
// All packets start with this header for reliable delivery
struct PigSyncHeader {
    uint8_t magic;      // PIGSYNC_MAGIC (0x50)
    uint8_t version;    // PIGSYNC_VERSION (0x30)
    uint8_t type;       // Packet type (CMD_* or RSP_*)
    uint8_t flags;      // Packet flags
    uint8_t seq;        // Sequence number (0-255, for reliability)
    uint8_t ack;        // Piggyback ACK (last received seq)
    uint16_t sessionId; // Connection identifier (0 for discovery/hello)
};

// ==[ CMD_DISCOVER (14 bytes) ]==
// Broadcast to find nearby SIRLOINs
struct CmdDiscover {
    PigSyncHeader hdr;      // sessionId=0 for discovery
    uint8_t pops_mac[6];    // POPS MAC for unicast response
};

// ==[ RSP_RING (8 bytes) ]==
struct RspRing {
    PigSyncHeader hdr;
};

// ==[ RSP_BEACON (20 bytes) ]==
// Discovery response from SON
struct RspBeacon {
    PigSyncHeader hdr;      // sessionId=0 for discovery
    uint8_t son_mac[6];     // SON MAC address
    uint16_t pending;       // Unsynced capture count
    uint8_t flags;          // FLAG_HUNTING, FLAG_BUFFER_FULL, etc.
    uint8_t rssi;           // Signal strength (for display)
};

// ==[ CMD_HELLO (8 bytes) ]==
// Initiate sync session (sessionId=0, Sirloin assigns new sessionId)
struct CmdHello {
    PigSyncHeader hdr;
};

// ==[ RSP_HELLO (16+ bytes) ]==
// Session accepted, includes Papa's HELLO line
// NOTE: sessionId in header is the NEW session (assigned by Sirloin)
struct RspHello {
    PigSyncHeader hdr;          // sessionId = new session token
    uint16_t pmkid_count;       // Unsynced PMKID count
    uint16_t hs_count;          // Unsynced handshake count
    uint8_t dialogue_id;        // Random 0-2 for dialogue track
    uint8_t mood;               // Son's base happiness (0-255, 128=neutral)
    uint8_t data_channel;       // Channel for data transfer (3,4,8,9,13)
    uint8_t papa_hello_len;     // Length of Papa's HELLO text (0 = use dialogue_id)
    // Followed by: char papa_hello_text[papa_hello_len] if len > 0
};

// ==[ CMD_READY (8 bytes) ]==
// Confirm data channel switch (sent after RSP_HELLO, on new channel)
struct CmdReady {
    PigSyncHeader hdr;          // sessionId must match RSP_HELLO
};

// ==[ RSP_READY (16 bytes) ]==
// Connection confirmed on data channel
struct RspReady {
    PigSyncHeader hdr;
    uint16_t pmkid_count;       // Final count after channel switch
    uint16_t hs_count;          // Final count after channel switch
    uint16_t total_bytes;       // Total bytes to transfer (estimate)
    uint16_t reserved;
};

// ==[ CMD_GET_COUNT (8 bytes) ]==
struct CmdGetCount {
    PigSyncHeader hdr;
};

// ==[ RSP_COUNT (12 bytes) ]==
struct RspCount {
    PigSyncHeader hdr;
    uint16_t pmkid_count;
    uint16_t hs_count;
};

// ==[ CMD_START_SYNC (12 bytes) ]==
// Request specific capture
struct CmdStartSync {
    PigSyncHeader hdr;
    uint8_t capture_type;   // CAPTURE_TYPE_PMKID or CAPTURE_TYPE_HANDSHAKE
    uint8_t reserved;
    uint16_t index;         // Capture index (0-based)
};

// ==[ RSP_CHUNK (12 + data bytes) ]==
// Data fragment
struct RspChunk {
    PigSyncHeader hdr;
    uint16_t chunk_seq;     // Fragment sequence (0-based) - renamed to avoid confusion with hdr.seq
    uint16_t chunk_total;   // Total fragments
    // Followed by: uint8_t data[len] (max PIGSYNC_MAX_PAYLOAD)
};

// ==[ CMD_ACK_CHUNK (12 bytes) ]==
struct CmdAckChunk {
    PigSyncHeader hdr;
    uint16_t chunk_seq;     // Acknowledged chunk sequence number
    uint16_t reserved;
};

// ==[ RSP_COMPLETE (16 bytes) ]==
// Transfer done with CRC
struct RspComplete {
    PigSyncHeader hdr;
    uint16_t total_bytes;   // Total data bytes sent
    uint16_t reserved;
    uint32_t crc32;         // CRC32 of complete payload
};

// ==[ CMD_MARK_SYNCED (12 bytes) ]==
struct CmdMarkSynced {
    PigSyncHeader hdr;
    uint8_t capture_type;
    uint8_t reserved;
    uint16_t index;
};

// ==[ CMD_BOUNTIES (10 + count*6 bytes) ]==
struct CmdBounties {
    PigSyncHeader hdr;
    uint8_t count;          // Number of bounty BSSIDs (max 15)
    uint8_t reserved;
    // Followed by: uint8_t bssids[count][6]
};

// ==[ RSP_BOUNTIES_ACK (10 bytes) ]==
struct RspBountiesAck {
    PigSyncHeader hdr;
    uint8_t count;          // Acknowledged bounty count
    uint8_t reserved;
};

// ==[ CMD_PURGE (9+ bytes) ]==
// Sync complete, includes Papa's GOODBYE line
struct CmdPurge {
    PigSyncHeader hdr;
    uint8_t papa_goodbye_len;   // Length of Papa's GOODBYE text
    // Followed by: char papa_goodbye_text[papa_goodbye_len]
};

// ==[ RSP_PURGED (14+ bytes) ]==
struct RspPurged {
    PigSyncHeader hdr;
    uint16_t purged_count;      // Captures purged
    uint8_t bounty_matches;     // Bounties matched this session
    uint8_t matched_count;      // Matched BSSID count in payload
    // Followed by: uint8_t matched_bssids[matched_count][6]
};

// ==[ CMD_ABORT (8 bytes) ]==
struct CmdAbort {
    PigSyncHeader hdr;
};

// ==[ RSP_ERROR (10 bytes) ]==
struct RspError {
    PigSyncHeader hdr;
    uint8_t error_code;
    uint8_t reserved;
};

// ==[ RSP_OK (8 bytes) ]==
struct RspOk {
    PigSyncHeader hdr;
};

// ============================================================================
// PHASE 3: BEACON & TIME SYNC
// ============================================================================

// ==[ BEACON_GRUNT (24 bytes) ]==
// Connectionless broadcast for passive awareness
// Sirloin sends this periodically when idle (not hunting/syncing)
struct BeaconGrunt {
    uint8_t  magic;           // 0x50
    uint8_t  version;         // 0x30
    uint8_t  type;            // 0xB0 (BEACON_GRUNT)
    uint8_t  flags;           // mood[7:5] | alerts[4:0]
    uint8_t  sirloinMac[6];   // Sender MAC
    uint8_t  captureCount;    // Total unsynced captures (0-255, capped)
    uint8_t  batteryPercent;  // 0-100
    uint8_t  storagePercent;  // PSRAM usage 0-100
    uint8_t  wakeWindowSec;   // Next wake in N seconds (0 = always on)
    uint32_t unixTime;        // RTC time if valid (0 = invalid)
    uint16_t uptimeMin;       // Uptime in minutes (caps at 65535)
    char     name[4];         // Short name e.g. "SRL1"
};

// Beacon flags breakdown
#define BEACON_FLAG_MOOD_MASK     0xE0  // Bits 7:5 = mood tier (0-7)
#define BEACON_FLAG_MOOD_SHIFT    5
#define BEACON_FLAG_ALERT_MASK    0x1F  // Bits 4:0 = alerts
#define BEACON_ALERT_LOW_BATTERY  0x01  // Battery < 20%
#define BEACON_ALERT_STORAGE_FULL 0x02  // Storage > 90%
#define BEACON_ALERT_HUNTING      0x04  // Currently hunting
#define BEACON_ALERT_CALL_ACTIVE  0x08  // Sync call in progress
#define BEACON_ALERT_BOUNTY_MATCH 0x10  // Unclaimed bounty match

// ==[ CMD_TIME_SYNC (12 bytes) ]==
// Porkchop requests time from Sirloin (Sirloin has RTC, Porkchop doesn't)
struct CmdTimeSync {
    PigSyncHeader hdr;
    uint32_t porkchopMillis;    // millis() for RTT calculation
};

// ==[ RSP_TIME_SYNC (18 bytes) ]==
// Sirloin sends its RTC time to Porkchop
struct RspTimeSync {
    PigSyncHeader hdr;
    uint32_t echoedMillis;      // Echo back for RTT calculation
    uint32_t sirloinUnixTime;   // Sirloin's RTC time (source of truth)
    uint8_t  rtcValid;          // 1 if RTC is set, 0 if never synced
    uint8_t  reserved;
};

#pragma pack(pop)

// ==[ CHANNEL POOL FOR DATA TRANSFER ]==
// Avoid congested channels 1, 6, 11
static const uint8_t PIGSYNC_DATA_CHANNELS[] = {3, 4, 8, 9, 13};
static const uint8_t PIGSYNC_DATA_CHANNEL_COUNT = 5;

// Select data channel from sessionId
inline uint8_t selectDataChannel(uint16_t sessionId) {
    return PIGSYNC_DATA_CHANNELS[sessionId % PIGSYNC_DATA_CHANNEL_COUNT];
}

// ==[ DIALOGUE PHRASES (MUST BE IDENTICAL ON BOTH DEVICES) ]==

// Papa's HELLO lines (indexed by dialogue_id)
static const char* const PAPA_HELLO[DIALOGUE_TRACK_COUNT] = {
    "ABOUT TIME YOU SHOWED UP",
    "WHERES MY PMKID MONEY",
    "BACK FROM /DEV/OUTSIDE I SEE"
};

// Son's HELLO lines (indexed by dialogue_id)
static const char* const SON_HELLO[DIALOGUE_TRACK_COUNT] = {
    "PAPA ITS YOUR FAVORITE MISTAKE",
    "SURPRISE IM NOT IN JAIL",
    "MISSED YOUR LAST 40 CALLS"
};

// Son's GOODBYE lines (indexed by dialogue_id)
static const char* const SON_GOODBYE[DIALOGUE_TRACK_COUNT] = {
    "SAME ESP TIME SAME ESP CHANNEL",
    "SIGTERM OLD MAN",
    "/DEV/NULL YOUR CALLS"
};

// Papa's ROAST lines for 0 captures (indexed by dialogue_id)
static const char* const PAPA_ROAST[DIALOGUE_TRACK_COUNT] = {
    "ZERO PMKIDS? NOT MY SON",
    "YOUR TCPDUMP IS EMPTY",
    "SHOULD HAVE COMPILED YOU OUT"
};

// Son's ROAST REACTION lines (indexed by dialogue_id)
static const char* const SON_ROAST_REACTION[DIALOGUE_TRACK_COUNT] = {
    "SEGFAULT IN MY FEELINGS",
    "CORE DUMPED MY SELF ESTEEM",
    "MANS GOT NO CHILL OR HEAP"
};

// ==[ PAPA GOODBYE TIERS (sent in packet, tier-based) ]==

// Tier 0: empty (0 captures)
static const char* const PAPA_GOODBYE_T0[] = {
    "EMPTY HANDED AGAIN",
    "UPTIME WASTED ON YOU",
    "INHERITANCE.TXT UNCHANGED"
};
static constexpr size_t PAPA_GOODBYE_T0_COUNT = 3;

// Tier 1: low (1-3 captures)
static const char* const PAPA_GOODBYE_T1[] = {
    "BETTER THAN NOTHING I GUESS",
    "BARELY WORTH THE BANDWIDTH",
    "AT LEAST YOU TRIED"
};
static constexpr size_t PAPA_GOODBYE_T1_COUNT = 3;

// Tier 2: medium (4-7 captures)
static const char* const PAPA_GOODBYE_T2[] = {
    "NOT BAD KID",
    "ACCEPTABLE PAYLOAD",
    "MAYBE YOU AINT WORTHLESS"
};
static constexpr size_t PAPA_GOODBYE_T2_COUNT = 3;

// Tier 3: high (8-10 captures)
static const char* const PAPA_GOODBYE_T3[] = {
    "NOW THATS WHAT IM TALKING ABOUT",
    "ADDED TO INHERITANCE.TXT",
    "SON OF A PIG INDEED"
};
static constexpr size_t PAPA_GOODBYE_T3_COUNT = 3;

// Tier 4: legendary (10+ captures)
static const char* const PAPA_GOODBYE_T4[] = {
    "LEGENDARY HAUL. ALMOST PROUD.",
    "BEST SON EVER. TODAY.",
    "HASHCAT GONNA EAT GOOD"
};
static constexpr size_t PAPA_GOODBYE_T4_COUNT = 3;

// ==[ SYNC HINTS (shown during transfer) ]==
static const char* const SYNC_HINTS[] = {
    "youve died before",
    "praise the bandwidth",
    "git gud at waiting",
    "try tongue but hole",
    "visions of latency"
};
static constexpr size_t SYNC_HINTS_COUNT = 5;

// ==[ PHONE ROAST TIERS (Sirloin's phone roasts owner) ]==

// Tier 0: empty (0 captures)
static const char* const PHONE_ROAST_T0[] = {
    "RETURN -ENODATA",
    "PERMISSION DENIED",
    "404 SKILL NOT FOUND",
    "/DEV/NULL DELIVERY"
};
static constexpr size_t PHONE_ROAST_T0_COUNT = 4;

// Tier 1: pathetic (1-2 captures)
static const char* const PHONE_ROAST_T1[] = {
    "THATS IT?",
    "SCRIPT KIDDIE TIER",
    "WEAK SIGNAL ENERGY",
    "RETRY LIMIT REACHED"
};
static constexpr size_t PHONE_ROAST_T1_COUNT = 4;

// Tier 2: meh (3-5 captures)
static const char* const PHONE_ROAST_T2[] = {
    "COULD BE WORSE",
    "COMPILE WARNING TIER",
    "NOT AN ERROR NOT A SUCCESS",
    "PARTIAL SUCCESS"
};
static constexpr size_t PHONE_ROAST_T2_COUNT = 4;

// Tier 3: decent (6-10 captures)
static const char* const PHONE_ROAST_T3[] = {
    "RETURN 0",
    "ACCEPTABLE",
    "BUFFER ADEQUATE",
    "ABOVE BASELINE"
};
static constexpr size_t PHONE_ROAST_T3_COUNT = 4;

// Tier 4: respectable (11-25 captures)
static const char* const PHONE_ROAST_T4[] = {
    "NICE HASHCAT FOOD",
    "PAPA MIGHT MALLOC YOU",
    "HEAP OVERFLOW OF PRIDE",
    "SOLID EXIT CODE"
};
static constexpr size_t PHONE_ROAST_T4_COUNT = 4;

// Tier 5: legend (26+ captures)
static const char* const PHONE_ROAST_T5[] = {
    "ABSOLUTE UNIT OF A PIG",
    "WARDRIVING TRANSCENDENCE",
    "PWNED THE AIRWAVES",
    "PAPA FEARS YOU NOW"
};
static constexpr size_t PHONE_ROAST_T5_COUNT = 4;

// ==[ HELPER: Select Papa's goodbye by capture tier ]==
inline const char* selectPapaGoodbye(uint16_t captures) {
    if (captures == 0) {
        return PAPA_GOODBYE_T0[random(PAPA_GOODBYE_T0_COUNT)];
    } else if (captures <= 3) {
        return PAPA_GOODBYE_T1[random(PAPA_GOODBYE_T1_COUNT)];
    } else if (captures <= 7) {
        return PAPA_GOODBYE_T2[random(PAPA_GOODBYE_T2_COUNT)];
    } else if (captures <= 10) {
        return PAPA_GOODBYE_T3[random(PAPA_GOODBYE_T3_COUNT)];
    } else {
        return PAPA_GOODBYE_T4[random(PAPA_GOODBYE_T4_COUNT)];
    }
}

// ==[ HELPER: Select phone roast by capture tier ]==
inline const char* selectPhoneRoast(uint16_t captures) {
    if (captures == 0) {
        return PHONE_ROAST_T0[random(PHONE_ROAST_T0_COUNT)];
    } else if (captures <= 2) {
        return PHONE_ROAST_T1[random(PHONE_ROAST_T1_COUNT)];
    } else if (captures <= 5) {
        return PHONE_ROAST_T2[random(PHONE_ROAST_T2_COUNT)];
    } else if (captures <= 10) {
        return PHONE_ROAST_T3[random(PHONE_ROAST_T3_COUNT)];
    } else if (captures <= 25) {
        return PHONE_ROAST_T4[random(PHONE_ROAST_T4_COUNT)];
    } else {
        return PHONE_ROAST_T5[random(PHONE_ROAST_T5_COUNT)];
    }
}

// ==[ HELPER: Validate packet header ]==
inline bool isValidPacket(const uint8_t* data, size_t len) {
    if (len < sizeof(PigSyncHeader)) return false;
    const PigSyncHeader* hdr = (const PigSyncHeader*)data;
    return hdr->magic == PIGSYNC_MAGIC && hdr->version == PIGSYNC_VERSION;
}

// ==[ HELPER: CRC32 (same algorithm as existing) ]==
inline uint32_t calculateCRC32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

// ==[ HELPER: Initialize packet header ]==
inline void initHeader(PigSyncHeader* hdr, uint8_t type, uint8_t seq = 0, uint8_t ack = 0, uint16_t sessionId = 0) {
    hdr->magic = PIGSYNC_MAGIC;
    hdr->version = PIGSYNC_VERSION;
    hdr->type = type;
    hdr->flags = 0;
    hdr->seq = seq;
    hdr->ack = ack;
    hdr->sessionId = sessionId;
}

// ==[ HELPER: Generate random session ID (non-zero) ]==
inline uint16_t generateSessionId() {
    uint16_t id = 0;
    while (id == 0) {
        id = (uint16_t)random(1, 0xFFFF);
    }
    return id;
}

// ==[ HELPER: Check sequence number within window (for duplicate detection) ]==
// Returns true if seq_new is ahead of seq_last within the window
inline bool isSeqNewer(uint8_t seq_new, uint8_t seq_last, uint8_t window = PIGSYNC_SEQ_WINDOW) {
    // Handle wraparound: seq_new is newer if it's within [seq_last+1, seq_last+window]
    uint8_t diff = seq_new - seq_last;
    return diff > 0 && diff <= window;
}

// ==[ RELIABILITY STATE (per-connection tracking) ]==
struct PigSyncReliability {
    uint8_t  txSeq;             // Next outgoing sequence number
    uint8_t  lastRxSeq;         // Last received sequence (for ACK)
    uint8_t  lastAckSent;       // Last ACK we sent
    uint8_t  pendingRetries;    // Retries for current packet
    uint32_t lastTxTime;        // millis() of last TX (for timeout)
    bool     waitingForAck;     // True if expecting ACK for txSeq-1
    
    void reset() {
        txSeq = 0;
        lastRxSeq = 0;
        lastAckSent = 0;
        pendingRetries = 0;
        lastTxTime = 0;
        waitingForAck = false;
    }
    
    uint8_t nextSeq() {
        return txSeq++;  // Post-increment
    }
};

#endif // PIGSYNC_PROTOCOL_H
