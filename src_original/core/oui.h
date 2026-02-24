// OUI (Organizationally Unique Identifier) Lookup
// Maps first 3 bytes of MAC address to vendor name
#pragma once

#include <Arduino.h>

namespace OUI {
    // Get vendor name for MAC address (returns "Unknown" if not found)
    const char* getVendor(const uint8_t* mac);
    
    // Self-test: verify table integrity at startup [P7]
    bool selfTest();
}
