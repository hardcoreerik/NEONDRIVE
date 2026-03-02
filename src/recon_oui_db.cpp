#include "recon_oui_db.h"

struct ReconOuiVendorEntry {
  uint32_t key24;
  const char* vendor;
};

// Compact starter OUI table for common LAN devices.
static const ReconOuiVendorEntry kReconOuiTable[] = {
  {0x00163E, "Apple"},
  {0x001A11, "Google"},
  {0x001B63, "Apple"},
  {0x001CB3, "Apple"},
  {0x001E52, "Apple"},
  {0x001F3F, "Apple"},
  {0x002248, "Cisco"},
  {0x0023AE, "Cisco"},
  {0x00259C, "Cisco"},
  {0x00265A, "Apple"},
  {0x0026B0, "Apple"},
  {0x0026BB, "Apple"},
  {0x0026F2, "Netgear"},
  {0x0026F3, "Netgear"},
  {0x00304F, "Cisco"},
  {0x004E35, "Dell"},
  {0x0050F2, "Microsoft"},
  {0x08BFB8, "Ubiquiti"},
  {0x0C9D92, "Ubiquiti"},
  {0x14CC20, "TP-Link"},
  {0x18FE34, "Espressif"},
  {0x1C1B0D, "Samsung"},
  {0x244BFE, "Huawei"},
  {0x286C07, "Xiaomi"},
  {0x2C54CF, "LG"},
  {0x3C5A37, "Google"},
  {0x3C84A0, "Huawei"},
  {0x3CE1A1, "Intel"},
  {0x40B076, "Cisco"},
  {0x44D9E7, "Ubiquiti"},
  {0x485D36, "Intel"},
  {0x4C3275, "Intel"},
  {0x50C7BF, "Apple"},
  {0x58EF68, "TP-Link"},
  {0x5C514F, "Google"},
  {0x603197, "Zyxel"},
  {0x647002, "TP-Link"},
  {0x68FF7B, "TP-Link"},
  {0x6C5AB5, "Google"},
  {0x7085C2, "Intel"},
  {0x7427EA, "Huawei"},
  {0x74DA38, "Ubiquiti"},
  {0x7C8BCA, "Cisco"},
  {0x809F1D, "Nest"},
  {0x80EA96, "Samsung"},
  {0x842B2B, "Samsung"},
  {0x8C8590, "Apple"},
  {0x90F652, "TP-Link"},
  {0x98DA60, "TP-Link"},
  {0x9CFC01, "Xiaomi"},
  {0xA4CF12, "Apple"},
  {0xA8B1D4, "Espressif"},
  {0xAC84C6, "HP"},
  {0xB0BE83, "Apple"},
  {0xB4E62D, "Samsung"},
  {0xB827EB, "Raspberry Pi"},
  {0xBC3AEA, "Apple"},
  {0xBCFF4D, "ASUS"},
  {0xC0E4D0, "Netgear"},
  {0xC4E984, "TP-Link"},
  {0xCC32E5, "Intel"},
  {0xD03745, "TP-Link"},
  {0xD4E0B0, "TP-Link"},
  {0xD850E6, "ASUS"},
  {0xDCA632, "Raspberry Pi"},
  {0xE4956E, "Apple"},
  {0xE84E06, "Hikvision"},
  {0xECFA5C, "Xiaomi"},
  {0xF4F5D8, "Ubiquiti"},
  {0xF8E903, "Dahua"},
  {0xFCFBFB, "Amazon"},
};

static uint32_t reconOuiKey24(const uint8_t mac[6]) {
  if (!mac) return 0;
  return ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | (uint32_t)mac[2];
}

const char* reconLookupOuiVendor(const uint8_t mac[6]) {
  const uint32_t key = reconOuiKey24(mac);
  for (const auto& entry : kReconOuiTable) {
    if (entry.key24 == key) return entry.vendor;
  }
  return "Unknown";
}

