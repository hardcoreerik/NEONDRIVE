#include "neon_rf.h"
#include <Arduino.h>
#include <esp_system.h>
#include <string.h>
#if defined(NEONDRIVE_TARGET_M5TAB5)
#include <WiFi.h>
#include <driver/gpio.h>
#endif
#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
#include "device_profile_select.h"
#endif

// Last RF action persisted in-memory across the current boot.
// TODO: persist to LittleFS /rf_last_action.txt so the value survives a
//       crash-reboot. Call neon_rf_set_last_action() from each RF path that
//       could trigger an SDIO / driver fault, then read it back in neon_rf_init()
//       on next boot to identify which action caused the crash.
static char s_last_rf_action[64] = "";

#if defined(NEONDRIVE_TARGET_M5TAB5)
// Official M5Tab5 C6 SDIO mapping (P4 host -> C6):
// CLK=G12, CMD=G13, D0=G11, D1=G10, D2=G9, D3=G8, RST=G15
// WiFi.setPins() is required on pioarduino 55.03.38-1 / Arduino-ESP32 ≥3.3.0.
// The compile-time tab5_sdkconfig_override.h macros alone do NOT reach the
// SDMMC slot config in the pre-compiled libespressif__esp_hosted.a; only
// WiFi.setPins() (sdio_pin_config_t path, PR #11513) does.
static constexpr int TAB5_SDIO_CLK = 12;
static constexpr int TAB5_SDIO_CMD = 13;
static constexpr int TAB5_SDIO_D0  = 11;
static constexpr int TAB5_SDIO_D1  = 10;
static constexpr int TAB5_SDIO_D2  = 9;
static constexpr int TAB5_SDIO_D3  = 8;
static constexpr int TAB5_SDIO_RST = 15;

// Must be called before any WiFi.mode() / WiFi.begin() call.
// Idempotent — safe to call from multiple code paths.
static void tab5_configure_wifi_sdio_pins_once() {
    static bool configured = false;
    if (configured) return;
    configured = true;
    WiFi.setPins(TAB5_SDIO_CLK, TAB5_SDIO_CMD, TAB5_SDIO_D0, TAB5_SDIO_D1,
                 TAB5_SDIO_D2, TAB5_SDIO_D3, TAB5_SDIO_RST);
    Serial.printf("[neon_rf] WiFi.setPins: clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d rst=%d\n",
                  TAB5_SDIO_CLK, TAB5_SDIO_CMD, TAB5_SDIO_D0, TAB5_SDIO_D1,
                  TAB5_SDIO_D2, TAB5_SDIO_D3, TAB5_SDIO_RST);
}
#endif

// ── Capability tables ──────────────────────────────────────────────────────

#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
// T-Embed CC1101 Plus — ESP32-S3 with on-chip WiFi.
// CC1101 sub-GHz radio and nRF24L01 expansion are on the shared SPI bus;
// their drivers are Phase 6 (RadioLib). This block logs their presence.
//
// Antenna switch pins (verified from pinmap):
//   SW1=GPIO47, SW0=GPIO48  — select frequency band before CC1101 TX/RX:
//     SW1=H, SW0=L → 315 MHz
//     SW1=L, SW0=H → 868/915 MHz
//     SW1=H, SW0=H → 434 MHz
static constexpr int TEMBED_ANT_SW1  = 47;
static constexpr int TEMBED_ANT_SW0  = 48;
static constexpr int TEMBED_CC1101_CS   = 12;
static constexpr int TEMBED_CC1101_GDO0 =  3;
static constexpr int TEMBED_CC1101_GDO2 = 38;

static const NeonRfCaps s_caps = {
    /* supports_station_scan    */ true,
    /* supports_channel_control */ true,
    /* supports_promiscuous_rx  */ true,
    /* supports_raw_frame_tx    */ true,
    /* supports_ble             */ false,
    /* requires_remote_coprocessor  */ false,
    /* requires_custom_c6_firmware  */ false,
    /* backend_name             */ "tembed-s3",
};

bool neon_rf_is_local_radio() { return true; }

#elif defined(NEONDRIVE_TARGET_M5TAB5)

// ESP32-P4 + ESP32-C6 co-processor over SDIO.
// WiFi scan is enabled via compile-time SDIO pin override (tab5_sdkconfig_override.h).
static const NeonRfCaps s_caps = {
    /* supports_station_scan    */ true,
    /* supports_channel_control */ false,  // TODO: validate esp_wifi_set_channel via C6
    /* supports_promiscuous_rx  */ false,  // TODO: needs custom C6 slave firmware
    /* supports_raw_frame_tx    */ false,  // TODO: esp_wifi_80211_tx via C6
    /* supports_ble             */ false,
    /* requires_remote_coprocessor  */ true,
    /* requires_custom_c6_firmware  */ false,
    /* backend_name             */ "tab5-c6",
};

bool neon_rf_is_local_radio() { return false; }

#else  // CYD / T-Display-S3 / Cardputer — classic ESP32/S3 local-radio targets

static const NeonRfCaps s_caps = {
    /* supports_station_scan    */ true,
    /* supports_channel_control */ true,
    /* supports_promiscuous_rx  */ true,
    /* supports_raw_frame_tx    */ true,
    /* supports_ble             */ false,
    /* requires_remote_coprocessor  */ false,
    /* requires_custom_c6_firmware  */ false,
    /* backend_name             */ "cyd-local",
};

bool neon_rf_is_local_radio() { return true; }

#endif  // NEONDRIVE_TARGET_M5TAB5

// ── Public API ─────────────────────────────────────────────────────────────

void neon_rf_init() {
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reasonStr = "UNKNOWN";
    switch (reason) {
        case ESP_RST_POWERON:   reasonStr = "POWERON";   break;
        case ESP_RST_SW:        reasonStr = "SW";         break;
        case ESP_RST_PANIC:     reasonStr = "PANIC";      break;
        case ESP_RST_INT_WDT:   reasonStr = "INT_WDT";   break;
        case ESP_RST_TASK_WDT:  reasonStr = "TASK_WDT";  break;
        case ESP_RST_WDT:       reasonStr = "WDT";        break;
        case ESP_RST_DEEPSLEEP: reasonStr = "DEEPSLEEP";  break;
        case ESP_RST_BROWNOUT:  reasonStr = "BROWNOUT";   break;
        default: break;
    }

    Serial.printf("[neon_rf] backend=%s  reset=%s\n",
                  s_caps.backend_name, reasonStr);
    Serial.printf("[neon_rf] caps: scan=%d  ch_ctrl=%d  promisc=%d"
                  "  raw_tx=%d  coprocessor=%d\n",
                  s_caps.supports_station_scan,
                  s_caps.supports_channel_control,
                  s_caps.supports_promiscuous_rx,
                  s_caps.supports_raw_frame_tx,
                  s_caps.requires_remote_coprocessor);

    // Report last known RF action (helps identify crash cause after WDT/PANIC resets)
    if (s_last_rf_action[0] != '\0') {
        Serial.printf("[neon_rf] last_rf_action (in-memory): %s\n", s_last_rf_action);
    }

#if defined(NEONDRIVE_TARGET_M5TAB5)
    // Call WiFi.setPins() before any WiFi.mode() — required on pioarduino 55.03.38-1.
    // The one-shot guard inside ensures this is safe to call from multiple paths.
    tab5_configure_wifi_sdio_pins_once();

    // Do NOT pulse GPIO15 (C6 RST) manually.
    //
    // The C6 running ESP-Hosted 2.12.6 slave firmware initialises its SDIO
    // peripheral automatically at power-on.  Asserting GPIO15 LOW forces a
    // hard reset of the C6 mid-boot, then the 500 ms wait may not be long
    // enough for the slave to re-init before the host's CMD5 timeout fires.
    // The M5Stack OTA tool communicates successfully with the C6 without any
    // explicit RST pulse, confirming the C6 is ready at natural boot time.
    //
    // We instead simply wait here to ensure the C6 has had adequate time from
    // power-on to reach its SDIO slave ready state before the first WiFi call.
    Serial.println("[neon_rf] C6 boot wait: 1500 ms (no RST pulse)");
    delay(1500);
    Serial.println("[neon_rf] C6 boot wait: done");

#if defined(TAB5_TEST_C6_SCAN)
    // Auto-test: attempt WiFi STA mode immediately to validate SDIO init.
    // Only enabled in the c6_scan test build; production firmware waits for UI.
    // WiFi.setPins() was already called unconditionally above via
    // tab5_configure_wifi_sdio_pins_once() — do not call it again here.
    Serial.println("[c6test] step=sdio_init start (auto-test in neon_rf_init)");
    neon_rf_set_last_action("c6test_auto_sdio_init");
    if (!WiFi.mode(WIFI_MODE_STA)) {
        Serial.println("[c6test] step=sdio_init FAIL reason=wifi_mode_sta");
    } else {
        Serial.println("[c6test] step=sdio_init OK - starting scan");
        neon_rf_set_last_action("c6test_auto_scan");
        int n = WiFi.scanNetworks(false, true);  // blocking, show_hidden
        if (n < 0) {
            Serial.printf("[c6test] step=scan FAIL result=%d\n", n);
        } else {
            Serial.printf("[c6test] step=scan OK found=%d\n", n);
            for (int i = 0; i < n && i < 5; i++) {
                Serial.printf("[c6test]   [%d] SSID=%s RSSI=%d CH=%d\n",
                              i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
            }
        }
    }
    Serial.println("[c6test] auto-test complete");
#endif
#elif defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
    // Configure antenna switch to neutral / off before CC1101 driver takes over.
    // Default: both LOW — radio disabled until band is explicitly selected.
    pinMode(TEMBED_ANT_SW1, OUTPUT); digitalWrite(TEMBED_ANT_SW1, LOW);
    pinMode(TEMBED_ANT_SW0, OUTPUT); digitalWrite(TEMBED_ANT_SW0, LOW);

    Serial.println("[neon_rf] T-Embed CC1101 Plus radio map:");
    Serial.printf("[neon_rf]   CC1101: CS=%d GDO0=%d GDO2=%d  "
                  "ANT_SW1=%d ANT_SW0=%d\n",
                  TEMBED_CC1101_CS, TEMBED_CC1101_GDO0, TEMBED_CC1101_GDO2,
                  TEMBED_ANT_SW1, TEMBED_ANT_SW0);
#if ND_HW_NRF24
    Serial.println("[neon_rf]   nRF24L01: CE=43 CS=44 IRQ=-1 (expansion module)");
#endif
#if ND_HW_IR_TX
    Serial.println("[neon_rf]   IR: TX_EN=2 RX=1");
#endif
    Serial.println("[neon_rf]   CC1101 RadioLib driver: Phase 6 (not yet active)");
#endif  // NEONDRIVE_TARGET_M5TAB5 / T_EMBED_CC1101
}

const NeonRfCaps* neon_rf_get_caps() { return &s_caps; }

void neon_rf_set_last_action(const char* action) {
    if (!action) return;
    strncpy(s_last_rf_action, action, sizeof(s_last_rf_action) - 1);
    s_last_rf_action[sizeof(s_last_rf_action) - 1] = '\0';
    Serial.printf("[neon_rf] action: %s\n", s_last_rf_action);
    // TODO: LittleFS.open("/rf_last_action.txt", "w") flush here once FS is
    //       guaranteed mounted before first RF call.
}

const char* neon_rf_get_last_action() { return s_last_rf_action; }

esp_err_t neon_rf_log_unsupported(const char* fn_name) {
    Serial.printf("[neon_rf] %s: NOT SUPPORTED on backend=%s\n",
                  fn_name ? fn_name : "?", s_caps.backend_name);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t neon_rf_c6test_sdio_init() {
#if defined(NEONDRIVE_TARGET_M5TAB5) && defined(TAB5_TEST_C6_SDIO)
    Serial.println("[c6test] step=sdio_init start");
    neon_rf_set_last_action("c6test_sdio_init");
    tab5_configure_wifi_sdio_pins_once();
    Serial.println("[c6test] sdio pins configured");
    if (!WiFi.mode(WIFI_MODE_STA)) {
        Serial.println("[c6test] step=sdio_init fail reason=wifi_mode_sta");
        return ESP_FAIL;
    }
    Serial.println("[c6test] step=sdio_init ok");
    return ESP_OK;
#else
    Serial.println("[c6test] step=sdio_init fail reason=unsupported_build");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
