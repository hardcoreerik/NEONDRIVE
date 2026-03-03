#include <unity.h>
#include "../../src/recon_port_scanner.h"

void setUp(void) {}
void tearDown(void) {}

static void test_subnet_math_24(void) {
  const uint32_t ip = reconIpv4ToU32(192, 168, 1, 42);
  const uint32_t mask = reconIpv4ToU32(255, 255, 255, 0);
  const uint32_t subnet = reconSubnetBaseFromU32(ip, mask);
  const uint32_t bcast = reconBroadcastFromU32(subnet, mask);

  TEST_ASSERT_EQUAL_HEX32(reconIpv4ToU32(192, 168, 1, 0), subnet);
  TEST_ASSERT_EQUAL_HEX32(reconIpv4ToU32(192, 168, 1, 255), bcast);
  TEST_ASSERT_EQUAL_UINT8(24, reconPrefixLengthFromMaskU32(mask));
}

static void test_subnet_math_23(void) {
  const uint32_t ip = reconIpv4ToU32(10, 10, 5, 12);
  const uint32_t mask = reconIpv4ToU32(255, 255, 254, 0);
  const uint32_t subnet = reconSubnetBaseFromU32(ip, mask);
  const uint32_t bcast = reconBroadcastFromU32(subnet, mask);

  TEST_ASSERT_EQUAL_HEX32(reconIpv4ToU32(10, 10, 4, 0), subnet);
  TEST_ASSERT_EQUAL_HEX32(reconIpv4ToU32(10, 10, 5, 255), bcast);
  TEST_ASSERT_EQUAL_UINT8(23, reconPrefixLengthFromMaskU32(mask));
}

static void test_service_labels(void) {
  TEST_ASSERT_EQUAL_STRING("SSH", reconServiceName(22));
  TEST_ASSERT_EQUAL_STRING("HTTP", reconServiceName(80));
  TEST_ASSERT_EQUAL_STRING("HTTPS", reconServiceName(443));
  TEST_ASSERT_EQUAL_STRING("SMB", reconServiceName(445));
  TEST_ASSERT_EQUAL_STRING("RTSP", reconServiceName(554));
  TEST_ASSERT_EQUAL_STRING("MODBUS", reconServiceName(502));
  TEST_ASSERT_EQUAL_STRING("UPnP", reconServiceName(5000));
  TEST_ASSERT_EQUAL_STRING("BACnet", reconServiceName(47808));
  TEST_ASSERT_EQUAL_STRING("Unknown", reconServiceName(65000));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_subnet_math_24);
  RUN_TEST(test_subnet_math_23);
  RUN_TEST(test_service_labels);
  return UNITY_END();
}
