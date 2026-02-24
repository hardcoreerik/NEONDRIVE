// Utility Function Tests
// Tests string validation, channel helpers, RSSI conversion, and time utilities

#include <unity.h>
#include <cstring>
#include "../mocks/testable_functions.h"

void setUp(void) {
    // No setup needed
}

void tearDown(void) {
    // No teardown needed
}

// ============================================================================
// isPrintableASCII() tests
// ============================================================================

void test_isPrintableASCII_space(void) {
    TEST_ASSERT_TRUE(isPrintableASCII(' '));  // 32
}

void test_isPrintableASCII_tilde(void) {
    TEST_ASSERT_TRUE(isPrintableASCII('~'));  // 126
}

void test_isPrintableASCII_letters(void) {
    TEST_ASSERT_TRUE(isPrintableASCII('A'));
    TEST_ASSERT_TRUE(isPrintableASCII('Z'));
    TEST_ASSERT_TRUE(isPrintableASCII('a'));
    TEST_ASSERT_TRUE(isPrintableASCII('z'));
}

void test_isPrintableASCII_digits(void) {
    TEST_ASSERT_TRUE(isPrintableASCII('0'));
    TEST_ASSERT_TRUE(isPrintableASCII('9'));
}

void test_isPrintableASCII_special(void) {
    TEST_ASSERT_TRUE(isPrintableASCII('!'));
    TEST_ASSERT_TRUE(isPrintableASCII('@'));
    TEST_ASSERT_TRUE(isPrintableASCII('#'));
    TEST_ASSERT_TRUE(isPrintableASCII('_'));
}

void test_isPrintableASCII_control_chars(void) {
    TEST_ASSERT_FALSE(isPrintableASCII('\0'));   // Null
    TEST_ASSERT_FALSE(isPrintableASCII('\n'));   // Newline (10)
    TEST_ASSERT_FALSE(isPrintableASCII('\r'));   // CR (13)
    TEST_ASSERT_FALSE(isPrintableASCII('\t'));   // Tab (9)
    TEST_ASSERT_FALSE(isPrintableASCII(31));     // Unit separator
}

void test_isPrintableASCII_del_and_above(void) {
    TEST_ASSERT_FALSE(isPrintableASCII(127));    // DEL
    TEST_ASSERT_FALSE(isPrintableASCII((char)128));
    TEST_ASSERT_FALSE(isPrintableASCII((char)255));
}

// ============================================================================
// isValidSSID() tests
// ============================================================================

void test_isValidSSID_normal(void) {
    const char* ssid = "MyNetwork";
    TEST_ASSERT_TRUE(isValidSSID(ssid, strlen(ssid)));
}

void test_isValidSSID_with_spaces(void) {
    const char* ssid = "My Home WiFi";
    TEST_ASSERT_TRUE(isValidSSID(ssid, strlen(ssid)));
}

void test_isValidSSID_with_special(void) {
    const char* ssid = "Net@Work_2.4GHz!";
    TEST_ASSERT_TRUE(isValidSSID(ssid, strlen(ssid)));
}

void test_isValidSSID_null_pointer(void) {
    TEST_ASSERT_FALSE(isValidSSID(nullptr, 5));
}

void test_isValidSSID_zero_length(void) {
    TEST_ASSERT_FALSE(isValidSSID("Test", 0));
}

void test_isValidSSID_too_long(void) {
    const char* longSsid = "This SSID is way too long to be valid";
    TEST_ASSERT_FALSE(isValidSSID(longSsid, 40));  // > 32
}

void test_isValidSSID_max_length(void) {
    const char* maxSsid = "12345678901234567890123456789012";  // Exactly 32 chars
    TEST_ASSERT_TRUE(isValidSSID(maxSsid, 32));
}

void test_isValidSSID_with_newline(void) {
    const char ssid[] = "Test\nNetwork";
    TEST_ASSERT_FALSE(isValidSSID(ssid, strlen(ssid)));
}

void test_isValidSSID_with_null_byte(void) {
    const char ssid[] = {'T', 'e', 's', 't', '\0', 'N', 'e', 't'};
    TEST_ASSERT_FALSE(isValidSSID(ssid, 8));  // Contains null in middle
}

// ============================================================================
// isHiddenSSID() tests
// ============================================================================

void test_isHiddenSSID_zero_length(void) {
    uint8_t ssid[] = {};
    TEST_ASSERT_TRUE(isHiddenSSID(ssid, 0));
}

void test_isHiddenSSID_all_nulls(void) {
    uint8_t ssid[] = {0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(isHiddenSSID(ssid, 8));
}

void test_isHiddenSSID_single_null(void) {
    uint8_t ssid[] = {0};
    TEST_ASSERT_TRUE(isHiddenSSID(ssid, 1));
}

void test_isHiddenSSID_visible(void) {
    uint8_t ssid[] = {'T', 'e', 's', 't'};
    TEST_ASSERT_FALSE(isHiddenSSID(ssid, 4));
}

void test_isHiddenSSID_mixed_with_non_null(void) {
    uint8_t ssid[] = {0, 0, 'A', 0, 0};
    TEST_ASSERT_FALSE(isHiddenSSID(ssid, 5));
}

// ============================================================================
// calculateChecksum() tests
// ============================================================================

void test_calculateChecksum_empty(void) {
    uint8_t data[] = {};
    TEST_ASSERT_EQUAL_UINT8(0, calculateChecksum(data, 0));
}

void test_calculateChecksum_single_byte(void) {
    uint8_t data[] = {0x42};
    TEST_ASSERT_EQUAL_UINT8(0x42, calculateChecksum(data, 1));
}

void test_calculateChecksum_xor_cancels(void) {
    uint8_t data[] = {0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT8(0, calculateChecksum(data, 2));  // FF XOR FF = 0
}

void test_calculateChecksum_sequence(void) {
    uint8_t data[] = {0x01, 0x02, 0x04};  // 1 XOR 2 = 3, 3 XOR 4 = 7
    TEST_ASSERT_EQUAL_UINT8(0x07, calculateChecksum(data, 3));
}

void test_calculateChecksum_all_zeros(void) {
    uint8_t data[] = {0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(0, calculateChecksum(data, 5));
}

// ============================================================================
// Channel validation tests
// ============================================================================

void test_isValid24GHzChannel_valid(void) {
    for (uint8_t ch = 1; ch <= 14; ch++) {
        TEST_ASSERT_TRUE(isValid24GHzChannel(ch));
    }
}

void test_isValid24GHzChannel_zero(void) {
    TEST_ASSERT_FALSE(isValid24GHzChannel(0));
}

void test_isValid24GHzChannel_too_high(void) {
    TEST_ASSERT_FALSE(isValid24GHzChannel(15));
    TEST_ASSERT_FALSE(isValid24GHzChannel(36));  // 5GHz channel
    TEST_ASSERT_FALSE(isValid24GHzChannel(255));
}

void test_isNonOverlappingChannel_standard(void) {
    TEST_ASSERT_TRUE(isNonOverlappingChannel(1));
    TEST_ASSERT_TRUE(isNonOverlappingChannel(6));
    TEST_ASSERT_TRUE(isNonOverlappingChannel(11));
}

void test_isNonOverlappingChannel_overlapping(void) {
    TEST_ASSERT_FALSE(isNonOverlappingChannel(2));
    TEST_ASSERT_FALSE(isNonOverlappingChannel(3));
    TEST_ASSERT_FALSE(isNonOverlappingChannel(4));
    TEST_ASSERT_FALSE(isNonOverlappingChannel(5));
    TEST_ASSERT_FALSE(isNonOverlappingChannel(7));
    TEST_ASSERT_FALSE(isNonOverlappingChannel(14));
}

// ============================================================================
// channelToFrequency() tests
// ============================================================================

void test_channelToFrequency_ch1(void) {
    TEST_ASSERT_EQUAL_UINT16(2412, channelToFrequency(1));
}

void test_channelToFrequency_ch6(void) {
    TEST_ASSERT_EQUAL_UINT16(2437, channelToFrequency(6));
}

void test_channelToFrequency_ch11(void) {
    TEST_ASSERT_EQUAL_UINT16(2462, channelToFrequency(11));
}

void test_channelToFrequency_ch13(void) {
    TEST_ASSERT_EQUAL_UINT16(2472, channelToFrequency(13));
}

void test_channelToFrequency_ch14(void) {
    TEST_ASSERT_EQUAL_UINT16(2484, channelToFrequency(14));  // Special case
}

void test_channelToFrequency_invalid(void) {
    TEST_ASSERT_EQUAL_UINT16(0, channelToFrequency(0));
    TEST_ASSERT_EQUAL_UINT16(0, channelToFrequency(15));
}

// ============================================================================
// frequencyToChannel() tests
// ============================================================================

void test_frequencyToChannel_2412(void) {
    TEST_ASSERT_EQUAL_UINT8(1, frequencyToChannel(2412));
}

void test_frequencyToChannel_2437(void) {
    TEST_ASSERT_EQUAL_UINT8(6, frequencyToChannel(2437));
}

void test_frequencyToChannel_2462(void) {
    TEST_ASSERT_EQUAL_UINT8(11, frequencyToChannel(2462));
}

void test_frequencyToChannel_2484(void) {
    TEST_ASSERT_EQUAL_UINT8(14, frequencyToChannel(2484));
}

void test_frequencyToChannel_invalid_low(void) {
    TEST_ASSERT_EQUAL_UINT8(0, frequencyToChannel(2400));
}

void test_frequencyToChannel_invalid_high(void) {
    TEST_ASSERT_EQUAL_UINT8(0, frequencyToChannel(2500));
}

void test_frequencyToChannel_5ghz(void) {
    TEST_ASSERT_EQUAL_UINT8(0, frequencyToChannel(5180));  // 5GHz ch36
}

// ============================================================================
// rssiToQuality() tests
// ============================================================================

void test_rssiToQuality_excellent(void) {
    TEST_ASSERT_EQUAL_UINT8(100, rssiToQuality(-30));
    TEST_ASSERT_EQUAL_UINT8(100, rssiToQuality(-20));
    TEST_ASSERT_EQUAL_UINT8(100, rssiToQuality(0));
}

void test_rssiToQuality_good(void) {
    // -50 dBm: (-50 + 90) * 100 / 60 = 66%
    uint8_t q = rssiToQuality(-50);
    TEST_ASSERT_TRUE(q >= 60 && q <= 70);
}

void test_rssiToQuality_weak(void) {
    // -80 dBm: (-80 + 90) * 100 / 60 = 16%
    uint8_t q = rssiToQuality(-80);
    TEST_ASSERT_TRUE(q >= 10 && q <= 20);
}

void test_rssiToQuality_very_weak(void) {
    TEST_ASSERT_EQUAL_UINT8(0, rssiToQuality(-90));
    TEST_ASSERT_EQUAL_UINT8(0, rssiToQuality(-100));
}

void test_rssiToQuality_midpoint(void) {
    // -60 dBm: (-60 + 90) * 100 / 60 = 50%
    TEST_ASSERT_EQUAL_UINT8(50, rssiToQuality(-60));
}

// ============================================================================
// isUsableSignal() / isExcellentSignal() tests
// ============================================================================

void test_isUsableSignal_strong(void) {
    TEST_ASSERT_TRUE(isUsableSignal(-50));
    TEST_ASSERT_TRUE(isUsableSignal(-70));
    TEST_ASSERT_TRUE(isUsableSignal(-79));
}

void test_isUsableSignal_boundary(void) {
    TEST_ASSERT_FALSE(isUsableSignal(-80));
    TEST_ASSERT_FALSE(isUsableSignal(-90));
}

void test_isExcellentSignal_strong(void) {
    TEST_ASSERT_TRUE(isExcellentSignal(-30));
    TEST_ASSERT_TRUE(isExcellentSignal(-49));
}

void test_isExcellentSignal_boundary(void) {
    TEST_ASSERT_FALSE(isExcellentSignal(-50));
    TEST_ASSERT_FALSE(isExcellentSignal(-60));
}

// ============================================================================
// msToTU() / tuToMs() tests
// ============================================================================

void test_msToTU_standard_beacon(void) {
    // 100ms beacon interval = ~97 TU (100 * 1000 / 1024)
    uint16_t tu = msToTU(100);
    TEST_ASSERT_TRUE(tu >= 97 && tu <= 98);
}

void test_tuToMs_standard_beacon(void) {
    // 100 TU = ~102ms (100 * 1024 / 1000)
    uint16_t ms = tuToMs(100);
    TEST_ASSERT_TRUE(ms >= 102 && ms <= 103);
}

void test_msToTU_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0, msToTU(0));
}

void test_tuToMs_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0, tuToMs(0));
}

void test_msToTU_tuToMs_roundtrip(void) {
    // Converting 1000ms to TU and back should be close to original
    uint16_t tu = msToTU(1000);
    uint16_t ms = tuToMs(tu);
    // Should be within 1% (10ms for 1000ms)
    TEST_ASSERT_TRUE(ms >= 990 && ms <= 1010);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // isPrintableASCII tests
    RUN_TEST(test_isPrintableASCII_space);
    RUN_TEST(test_isPrintableASCII_tilde);
    RUN_TEST(test_isPrintableASCII_letters);
    RUN_TEST(test_isPrintableASCII_digits);
    RUN_TEST(test_isPrintableASCII_special);
    RUN_TEST(test_isPrintableASCII_control_chars);
    RUN_TEST(test_isPrintableASCII_del_and_above);
    
    // isValidSSID tests
    RUN_TEST(test_isValidSSID_normal);
    RUN_TEST(test_isValidSSID_with_spaces);
    RUN_TEST(test_isValidSSID_with_special);
    RUN_TEST(test_isValidSSID_null_pointer);
    RUN_TEST(test_isValidSSID_zero_length);
    RUN_TEST(test_isValidSSID_too_long);
    RUN_TEST(test_isValidSSID_max_length);
    RUN_TEST(test_isValidSSID_with_newline);
    RUN_TEST(test_isValidSSID_with_null_byte);
    
    // isHiddenSSID tests
    RUN_TEST(test_isHiddenSSID_zero_length);
    RUN_TEST(test_isHiddenSSID_all_nulls);
    RUN_TEST(test_isHiddenSSID_single_null);
    RUN_TEST(test_isHiddenSSID_visible);
    RUN_TEST(test_isHiddenSSID_mixed_with_non_null);
    
    // calculateChecksum tests
    RUN_TEST(test_calculateChecksum_empty);
    RUN_TEST(test_calculateChecksum_single_byte);
    RUN_TEST(test_calculateChecksum_xor_cancels);
    RUN_TEST(test_calculateChecksum_sequence);
    RUN_TEST(test_calculateChecksum_all_zeros);
    
    // Channel validation tests
    RUN_TEST(test_isValid24GHzChannel_valid);
    RUN_TEST(test_isValid24GHzChannel_zero);
    RUN_TEST(test_isValid24GHzChannel_too_high);
    RUN_TEST(test_isNonOverlappingChannel_standard);
    RUN_TEST(test_isNonOverlappingChannel_overlapping);
    
    // channelToFrequency tests
    RUN_TEST(test_channelToFrequency_ch1);
    RUN_TEST(test_channelToFrequency_ch6);
    RUN_TEST(test_channelToFrequency_ch11);
    RUN_TEST(test_channelToFrequency_ch13);
    RUN_TEST(test_channelToFrequency_ch14);
    RUN_TEST(test_channelToFrequency_invalid);
    
    // frequencyToChannel tests
    RUN_TEST(test_frequencyToChannel_2412);
    RUN_TEST(test_frequencyToChannel_2437);
    RUN_TEST(test_frequencyToChannel_2462);
    RUN_TEST(test_frequencyToChannel_2484);
    RUN_TEST(test_frequencyToChannel_invalid_low);
    RUN_TEST(test_frequencyToChannel_invalid_high);
    RUN_TEST(test_frequencyToChannel_5ghz);
    
    // rssiToQuality tests
    RUN_TEST(test_rssiToQuality_excellent);
    RUN_TEST(test_rssiToQuality_good);
    RUN_TEST(test_rssiToQuality_weak);
    RUN_TEST(test_rssiToQuality_very_weak);
    RUN_TEST(test_rssiToQuality_midpoint);
    
    // Signal quality tests
    RUN_TEST(test_isUsableSignal_strong);
    RUN_TEST(test_isUsableSignal_boundary);
    RUN_TEST(test_isExcellentSignal_strong);
    RUN_TEST(test_isExcellentSignal_boundary);
    
    // Time conversion tests
    RUN_TEST(test_msToTU_standard_beacon);
    RUN_TEST(test_tuToMs_standard_beacon);
    RUN_TEST(test_msToTU_zero);
    RUN_TEST(test_tuToMs_zero);
    RUN_TEST(test_msToTU_tuToMs_roundtrip);
    
    return UNITY_END();
}
