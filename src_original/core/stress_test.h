// Stress Test Module - Inject fake data to test modes without RF
// Hold TAB when entering any mode to activate
#pragma once

#include <Arduino.h>
#include <esp_wifi_types.h>

// Stress test scenarios
enum class StressScenario : uint8_t {
    IDLE = 0,          // No stress test
    NETWORK_FLOOD,     // Inject many networks rapidly
    CLIENT_FLOOD,      // Inject many clients to monitored network
    CHURN,             // Add/remove networks rapidly (stress prune)
    HIDDEN_REVEAL,     // Hidden networks that get revealed
    RSSI_CHAOS,        // Rapidly changing signal strengths
    MIXED_AUTH,        // Mix of OPEN/WEP/WPA/WPA2/WPA3
};

class StressTest {
public:
    static void init();
    static void update();  // Call from main loop
    
    // Activation (check for TAB key)
    static void checkActivation();
    static bool isActive() { return active; }
    static StressScenario getScenario() { return scenario; }
    
    // Scenario control
    static void setScenario(StressScenario s);
    static void nextScenario();  // Cycle through scenarios
    
    // Statistics
    static uint32_t getInjectedCount() { return injectedCount; }
    static uint32_t getRate() { return injectRate; }  // Injections per second
    
private:
    static bool active;
    static StressScenario scenario;
    static uint32_t lastInjectTime;
    static uint32_t injectedCount;
    static uint32_t injectRate;
    static uint32_t lastRateCalc;
    static uint32_t injectsSinceLastCalc;
    static uint8_t networkCounter;  // For unique BSSIDs
    static uint8_t clientCounter;   // For unique client MACs
    
    // Injection helpers (call target mode's public APIs)
    static void injectNetwork();
    static void injectClient();
    static void updateChurn();
    static void injectHidden();
    static void updateRSSIChaos();
    
    // Random data generators
    static void randomBSSID(uint8_t* bssid);
    static void randomMAC(uint8_t* mac);
    static int8_t randomRSSI();
    static uint8_t randomChannel();
    static wifi_auth_mode_t randomAuthMode();
    static const char* randomSSID();
    
    // SSID pool for realistic names
    static const char* ssidPool[];
    static const uint8_t ssidPoolSize;
};
