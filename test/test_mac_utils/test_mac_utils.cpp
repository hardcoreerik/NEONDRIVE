// Test MAC address utilities, PCAP structures, and deauth frame construction
// Tests pure functions from testable_functions.h
// Priority 4 of test expansion plan

#include <unity.h>
#include "../mocks/testable_functions.h"
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// BSSID Key Conversion Tests
// ============================================================================

void test_bssidToKey_allZeros(void) {
    uint8_t bssid[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint64_t key = bssidToKey(bssid);
    TEST_ASSERT_EQUAL_UINT64(0ULL, key);
}

void test_bssidToKey_allOnes(void) {
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint64_t key = bssidToKey(bssid);
    TEST_ASSERT_EQUAL_UINT64(0x0000FFFFFFFFFFFFULL, key);
}

void test_bssidToKey_typicalMAC(void) {
    // 64:EE:B7:20:82:86
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint64_t key = bssidToKey(bssid);
    // Expected: 0x64 in bits 40-47, 0xEE in 32-39, etc.
    uint64_t expected = 0x64EEB7208286ULL;
    TEST_ASSERT_EQUAL_UINT64(expected, key);
}

void test_bssidToKey_singleByte(void) {
    // Only first byte set
    uint8_t bssid1[6] = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT64(0x420000000000ULL, bssidToKey(bssid1));
    
    // Only last byte set
    uint8_t bssid2[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x42};
    TEST_ASSERT_EQUAL_UINT64(0x000000000042ULL, bssidToKey(bssid2));
}

void test_keyToBssid_roundTrip(void) {
    uint8_t original[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint64_t key = bssidToKey(original);
    uint8_t recovered[6];
    keyToBssid(key, recovered);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original, recovered, 6);
}

void test_keyToBssid_allZeros(void) {
    uint8_t bssid[6];
    keyToBssid(0ULL, bssid);
    uint8_t expected[6] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bssid, 6);
}

void test_keyToBssid_allOnes(void) {
    uint8_t bssid[6];
    keyToBssid(0xFFFFFFFFFFFFULL, bssid);
    uint8_t expected[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bssid, 6);
}

void test_keyToBssid_ignoredHighBits(void) {
    // Key with bits set above 48 should be ignored
    uint8_t bssid[6];
    keyToBssid(0xFF00112233445566ULL, bssid);
    // Only lower 48 bits should be used
    uint8_t expected[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bssid, 6);
}

// ============================================================================
// MAC Bit Manipulation Tests
// ============================================================================

void test_applyLocalMACBits_universalToLocal(void) {
    // Universal unicast MAC (bit 0 clear, bit 1 clear)
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    applyLocalMACBits(mac);
    // Should set bit 1 (locally administered), keep bit 0 clear (unicast)
    TEST_ASSERT_EQUAL_UINT8(0x02, mac[0]);
    // Other bytes unchanged
    TEST_ASSERT_EQUAL_UINT8(0x11, mac[1]);
}

void test_applyLocalMACBits_multicastCleared(void) {
    // Multicast MAC (bit 0 set)
    uint8_t mac[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    applyLocalMACBits(mac);
    // Should clear bit 0 (unicast), set bit 1 (local)
    TEST_ASSERT_EQUAL_UINT8(0x02, mac[0]);
}

void test_applyLocalMACBits_alreadyLocal(void) {
    // Already locally administered unicast
    uint8_t mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    applyLocalMACBits(mac);
    // Should remain 0x02
    TEST_ASSERT_EQUAL_UINT8(0x02, mac[0]);
}

void test_applyLocalMACBits_preservesHighNibble(void) {
    // MAC with high nibble bits set
    uint8_t mac[6] = {0xFC, 0x00, 0x00, 0x00, 0x00, 0x00};
    applyLocalMACBits(mac);
    // High nibble should be preserved: 0xFC & 0xFC | 0x02 = 0xFE
    TEST_ASSERT_EQUAL_UINT8(0xFE, mac[0]);
}

void test_applyLocalMACBits_allOnesInput(void) {
    uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    applyLocalMACBits(mac);
    // 0xFF & 0xFC | 0x02 = 0xFE
    TEST_ASSERT_EQUAL_UINT8(0xFE, mac[0]);
}

void test_isValidLocalMAC_validLocal(void) {
    uint8_t mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    TEST_ASSERT_TRUE(isValidLocalMAC(mac));
}

void test_isValidLocalMAC_universal(void) {
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    TEST_ASSERT_FALSE(isValidLocalMAC(mac));
}

void test_isValidLocalMAC_multicast(void) {
    uint8_t mac[6] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(isValidLocalMAC(mac));
}

void test_isValidLocalMAC_universalMulticast(void) {
    uint8_t mac[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    TEST_ASSERT_FALSE(isValidLocalMAC(mac));
}

void test_isRandomizedMAC_randomized(void) {
    uint8_t mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    TEST_ASSERT_TRUE(isRandomizedMAC(mac));
}

void test_isRandomizedMAC_oui(void) {
    // Real OUI (Intel)
    uint8_t mac[6] = {0x00, 0x1B, 0x21, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(isRandomizedMAC(mac));
}

void test_isMulticastMAC_multicast(void) {
    uint8_t mac[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    TEST_ASSERT_TRUE(isMulticastMAC(mac));
}

void test_isMulticastMAC_unicast(void) {
    uint8_t mac[6] = {0x00, 0x1B, 0x21, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(isMulticastMAC(mac));
}

void test_isMulticastMAC_broadcast(void) {
    uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(isMulticastMAC(mac));
}

// ============================================================================
// MAC Formatting Tests
// ============================================================================

void test_formatMAC_typical(void) {
    uint8_t mac[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    char output[18];
    size_t len = formatMAC(mac, output, sizeof(output));
    TEST_ASSERT_EQUAL_UINT(17, len);
    TEST_ASSERT_EQUAL_STRING("64:EE:B7:20:82:86", output);
}

void test_formatMAC_allZeros(void) {
    uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    char output[18];
    size_t len = formatMAC(mac, output, sizeof(output));
    TEST_ASSERT_EQUAL_UINT(17, len);
    TEST_ASSERT_EQUAL_STRING("00:00:00:00:00:00", output);
}

void test_formatMAC_allOnes(void) {
    uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    char output[18];
    size_t len = formatMAC(mac, output, sizeof(output));
    TEST_ASSERT_EQUAL_UINT(17, len);
    TEST_ASSERT_EQUAL_STRING("FF:FF:FF:FF:FF:FF", output);
}

void test_formatMAC_bufferTooSmall(void) {
    uint8_t mac[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    char output[10];
    size_t len = formatMAC(mac, output, sizeof(output));
    TEST_ASSERT_EQUAL_UINT(0, len);
}

void test_formatMAC_nullOutput(void) {
    uint8_t mac[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    size_t len = formatMAC(mac, nullptr, 18);
    TEST_ASSERT_EQUAL_UINT(0, len);
}

void test_parseMAC_colonSeparated(void) {
    uint8_t mac[6];
    bool result = parseMAC("64:EE:B7:20:82:86", mac);
    TEST_ASSERT_TRUE(result);
    uint8_t expected[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, mac, 6);
}

void test_parseMAC_dashSeparated(void) {
    uint8_t mac[6];
    bool result = parseMAC("64-EE-B7-20-82-86", mac);
    TEST_ASSERT_TRUE(result);
    uint8_t expected[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, mac, 6);
}

void test_parseMAC_lowercase(void) {
    uint8_t mac[6];
    bool result = parseMAC("aa:bb:cc:dd:ee:ff", mac);
    TEST_ASSERT_TRUE(result);
    uint8_t expected[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, mac, 6);
}

void test_parseMAC_mixedCase(void) {
    uint8_t mac[6];
    bool result = parseMAC("Aa:Bb:Cc:Dd:Ee:Ff", mac);
    TEST_ASSERT_TRUE(result);
    uint8_t expected[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, mac, 6);
}

void test_parseMAC_invalidChars(void) {
    uint8_t mac[6];
    bool result = parseMAC("GG:HH:II:JJ:KK:LL", mac);
    TEST_ASSERT_FALSE(result);
}

void test_parseMAC_tooShort(void) {
    uint8_t mac[6];
    bool result = parseMAC("AA:BB:CC", mac);
    TEST_ASSERT_FALSE(result);
}

void test_parseMAC_nullInput(void) {
    uint8_t mac[6];
    bool result = parseMAC(nullptr, mac);
    TEST_ASSERT_FALSE(result);
}

void test_parseMAC_nullOutput(void) {
    bool result = parseMAC("AA:BB:CC:DD:EE:FF", nullptr);
    TEST_ASSERT_FALSE(result);
}

void test_parseMAC_formatMAC_roundTrip(void) {
    const char* original = "64:EE:B7:20:82:86";
    uint8_t mac[6];
    parseMAC(original, mac);
    char output[18];
    formatMAC(mac, output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING(original, output);
}

// ============================================================================
// PCAP Header Tests
// ============================================================================

void test_PCAPHeader_size(void) {
    TEST_ASSERT_EQUAL_UINT(24, sizeof(TestPCAPHeader));
}

void test_PCAPPacketHeader_size(void) {
    TEST_ASSERT_EQUAL_UINT(16, sizeof(TestPCAPPacketHeader));
}

void test_initPCAPHeader_magic(void) {
    TestPCAPHeader hdr;
    initPCAPHeader(&hdr);
    TEST_ASSERT_EQUAL_UINT32(0xA1B2C3D4, hdr.magic);
}

void test_initPCAPHeader_version(void) {
    TestPCAPHeader hdr;
    initPCAPHeader(&hdr);
    TEST_ASSERT_EQUAL_UINT16(2, hdr.version_major);
    TEST_ASSERT_EQUAL_UINT16(4, hdr.version_minor);
}

void test_initPCAPHeader_linktype(void) {
    TestPCAPHeader hdr;
    initPCAPHeader(&hdr);
    TEST_ASSERT_EQUAL_UINT32(105, hdr.linktype);  // IEEE802.11
}

void test_initPCAPHeader_snaplen(void) {
    TestPCAPHeader hdr;
    initPCAPHeader(&hdr);
    TEST_ASSERT_EQUAL_UINT32(65535, hdr.snaplen);
}

void test_isValidPCAPHeader_valid(void) {
    TestPCAPHeader hdr;
    initPCAPHeader(&hdr);
    TEST_ASSERT_TRUE(isValidPCAPHeader(&hdr));
}

void test_isValidPCAPHeader_bigEndian(void) {
    TestPCAPHeader hdr;
    initPCAPHeader(&hdr);
    hdr.magic = PCAP_MAGIC_BE;
    TEST_ASSERT_TRUE(isValidPCAPHeader(&hdr));
}

void test_isValidPCAPHeader_invalidMagic(void) {
    TestPCAPHeader hdr;
    initPCAPHeader(&hdr);
    hdr.magic = 0x12345678;
    TEST_ASSERT_FALSE(isValidPCAPHeader(&hdr));
}

void test_isValidPCAPHeader_wrongVersion(void) {
    TestPCAPHeader hdr;
    initPCAPHeader(&hdr);
    hdr.version_major = 3;
    TEST_ASSERT_FALSE(isValidPCAPHeader(&hdr));
}

void test_initPCAPPacketHeader_timestamp(void) {
    TestPCAPPacketHeader pkt;
    initPCAPPacketHeader(&pkt, 5500, 100);  // 5.5 seconds, 100 bytes
    TEST_ASSERT_EQUAL_UINT32(5, pkt.ts_sec);
    TEST_ASSERT_EQUAL_UINT32(500000, pkt.ts_usec);  // 500ms = 500000 usec
}

void test_initPCAPPacketHeader_length(void) {
    TestPCAPPacketHeader pkt;
    initPCAPPacketHeader(&pkt, 1000, 256);
    TEST_ASSERT_EQUAL_UINT32(256, pkt.incl_len);
    TEST_ASSERT_EQUAL_UINT32(256, pkt.orig_len);
}

void test_initPCAPPacketHeader_zeroTimestamp(void) {
    TestPCAPPacketHeader pkt;
    initPCAPPacketHeader(&pkt, 0, 50);
    TEST_ASSERT_EQUAL_UINT32(0, pkt.ts_sec);
    TEST_ASSERT_EQUAL_UINT32(0, pkt.ts_usec);
}

// ============================================================================
// Deauth Frame Construction Tests
// ============================================================================

void test_deauthFrame_size(void) {
    TEST_ASSERT_EQUAL_UINT(26, DEAUTH_FRAME_SIZE);
}

void test_buildDeauthFrame_returnValue(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    size_t len = buildDeauthFrame(frame, bssid, station, 7);
    TEST_ASSERT_EQUAL_UINT(26, len);
}

void test_buildDeauthFrame_frameControl(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 7);
    // Frame control for deauth: 0xC0 0x00
    TEST_ASSERT_EQUAL_UINT8(0xC0, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[1]);
}

void test_buildDeauthFrame_destination(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 7);
    // Destination at offset 4
    TEST_ASSERT_EQUAL_UINT8_ARRAY(station, frame + DEAUTH_OFFSET_DA, 6);
}

void test_buildDeauthFrame_source(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 7);
    // Source at offset 10 (spoofed as AP)
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, frame + DEAUTH_OFFSET_SA, 6);
}

void test_buildDeauthFrame_bssid(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 7);
    // BSSID at offset 16
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, frame + DEAUTH_OFFSET_BSSID, 6);
}

void test_buildDeauthFrame_reasonCode(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 7);
    // Reason code at offset 24
    TEST_ASSERT_EQUAL_UINT8(7, frame[24]);
    TEST_ASSERT_EQUAL_UINT8(0, frame[25]);
}

void test_buildDeauthFrame_differentReason(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 8);  // Reason 8: disassoc
    TEST_ASSERT_EQUAL_UINT8(8, frame[24]);
}

void test_buildDisassocFrame_frameControl(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDisassocFrame(frame, bssid, station, 8);
    // Frame control for disassoc: 0xA0 0x00
    TEST_ASSERT_EQUAL_UINT8(0xA0, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[1]);
}

void test_buildDisassocFrame_addresses(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDisassocFrame(frame, bssid, station, 8);
    // Same address layout as deauth
    TEST_ASSERT_EQUAL_UINT8_ARRAY(station, frame + DEAUTH_OFFSET_DA, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, frame + DEAUTH_OFFSET_SA, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, frame + DEAUTH_OFFSET_BSSID, 6);
}

void test_isValidDeauthFrame_valid(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 7);
    TEST_ASSERT_TRUE(isValidDeauthFrame(frame, 26));
}

void test_isValidDeauthFrame_tooShort(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 7);
    TEST_ASSERT_FALSE(isValidDeauthFrame(frame, 20));
}

void test_isValidDeauthFrame_wrongType(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDisassocFrame(frame, bssid, station, 8);
    TEST_ASSERT_FALSE(isValidDeauthFrame(frame, 26));
}

void test_isValidDisassocFrame_valid(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDisassocFrame(frame, bssid, station, 8);
    TEST_ASSERT_TRUE(isValidDisassocFrame(frame, 26));
}

void test_isValidDisassocFrame_wrongType(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t station[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    buildDeauthFrame(frame, bssid, station, 7);
    TEST_ASSERT_FALSE(isValidDisassocFrame(frame, 26));
}

// ============================================================================
// Broadcast Deauth Frame Tests
// ============================================================================

void test_buildDeauthFrame_broadcast(void) {
    uint8_t frame[32];
    uint8_t bssid[6] = {0x64, 0xEE, 0xB7, 0x20, 0x82, 0x86};
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    buildDeauthFrame(frame, bssid, broadcast, 7);
    // Destination should be broadcast
    TEST_ASSERT_EQUAL_UINT8_ARRAY(broadcast, frame + DEAUTH_OFFSET_DA, 6);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // BSSID Key Conversion
    RUN_TEST(test_bssidToKey_allZeros);
    RUN_TEST(test_bssidToKey_allOnes);
    RUN_TEST(test_bssidToKey_typicalMAC);
    RUN_TEST(test_bssidToKey_singleByte);
    RUN_TEST(test_keyToBssid_roundTrip);
    RUN_TEST(test_keyToBssid_allZeros);
    RUN_TEST(test_keyToBssid_allOnes);
    RUN_TEST(test_keyToBssid_ignoredHighBits);
    
    // MAC Bit Manipulation
    RUN_TEST(test_applyLocalMACBits_universalToLocal);
    RUN_TEST(test_applyLocalMACBits_multicastCleared);
    RUN_TEST(test_applyLocalMACBits_alreadyLocal);
    RUN_TEST(test_applyLocalMACBits_preservesHighNibble);
    RUN_TEST(test_applyLocalMACBits_allOnesInput);
    RUN_TEST(test_isValidLocalMAC_validLocal);
    RUN_TEST(test_isValidLocalMAC_universal);
    RUN_TEST(test_isValidLocalMAC_multicast);
    RUN_TEST(test_isValidLocalMAC_universalMulticast);
    RUN_TEST(test_isRandomizedMAC_randomized);
    RUN_TEST(test_isRandomizedMAC_oui);
    RUN_TEST(test_isMulticastMAC_multicast);
    RUN_TEST(test_isMulticastMAC_unicast);
    RUN_TEST(test_isMulticastMAC_broadcast);
    
    // MAC Formatting
    RUN_TEST(test_formatMAC_typical);
    RUN_TEST(test_formatMAC_allZeros);
    RUN_TEST(test_formatMAC_allOnes);
    RUN_TEST(test_formatMAC_bufferTooSmall);
    RUN_TEST(test_formatMAC_nullOutput);
    RUN_TEST(test_parseMAC_colonSeparated);
    RUN_TEST(test_parseMAC_dashSeparated);
    RUN_TEST(test_parseMAC_lowercase);
    RUN_TEST(test_parseMAC_mixedCase);
    RUN_TEST(test_parseMAC_invalidChars);
    RUN_TEST(test_parseMAC_tooShort);
    RUN_TEST(test_parseMAC_nullInput);
    RUN_TEST(test_parseMAC_nullOutput);
    RUN_TEST(test_parseMAC_formatMAC_roundTrip);
    
    // PCAP Headers
    RUN_TEST(test_PCAPHeader_size);
    RUN_TEST(test_PCAPPacketHeader_size);
    RUN_TEST(test_initPCAPHeader_magic);
    RUN_TEST(test_initPCAPHeader_version);
    RUN_TEST(test_initPCAPHeader_linktype);
    RUN_TEST(test_initPCAPHeader_snaplen);
    RUN_TEST(test_isValidPCAPHeader_valid);
    RUN_TEST(test_isValidPCAPHeader_bigEndian);
    RUN_TEST(test_isValidPCAPHeader_invalidMagic);
    RUN_TEST(test_isValidPCAPHeader_wrongVersion);
    RUN_TEST(test_initPCAPPacketHeader_timestamp);
    RUN_TEST(test_initPCAPPacketHeader_length);
    RUN_TEST(test_initPCAPPacketHeader_zeroTimestamp);
    
    // Deauth Frame Construction
    RUN_TEST(test_deauthFrame_size);
    RUN_TEST(test_buildDeauthFrame_returnValue);
    RUN_TEST(test_buildDeauthFrame_frameControl);
    RUN_TEST(test_buildDeauthFrame_destination);
    RUN_TEST(test_buildDeauthFrame_source);
    RUN_TEST(test_buildDeauthFrame_bssid);
    RUN_TEST(test_buildDeauthFrame_reasonCode);
    RUN_TEST(test_buildDeauthFrame_differentReason);
    RUN_TEST(test_buildDisassocFrame_frameControl);
    RUN_TEST(test_buildDisassocFrame_addresses);
    RUN_TEST(test_isValidDeauthFrame_valid);
    RUN_TEST(test_isValidDeauthFrame_tooShort);
    RUN_TEST(test_isValidDeauthFrame_wrongType);
    RUN_TEST(test_isValidDisassocFrame_valid);
    RUN_TEST(test_isValidDisassocFrame_wrongType);
    RUN_TEST(test_buildDeauthFrame_broadcast);
    
    return UNITY_END();
}
