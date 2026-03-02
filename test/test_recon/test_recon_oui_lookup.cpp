#include <unity.h>
#include "../../src/recon_oui_db.h"

void setUp(void) {}
void tearDown(void) {}

static void test_known_oui_vendor_lookup(void) {
  const uint8_t mac1[6] = {0xB8, 0x27, 0xEB, 0xAA, 0xBB, 0xCC};  // Raspberry Pi
  const uint8_t mac2[6] = {0x18, 0xFE, 0x34, 0x01, 0x02, 0x03};  // Espressif
  TEST_ASSERT_EQUAL_STRING("Raspberry Pi", reconLookupOuiVendor(mac1));
  TEST_ASSERT_EQUAL_STRING("Espressif", reconLookupOuiVendor(mac2));
}

static void test_unknown_oui_vendor_lookup(void) {
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0x00, 0x00, 0x01};
  TEST_ASSERT_EQUAL_STRING("Unknown", reconLookupOuiVendor(mac));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_known_oui_vendor_lookup);
  RUN_TEST(test_unknown_oui_vendor_lookup);
  return UNITY_END();
}

