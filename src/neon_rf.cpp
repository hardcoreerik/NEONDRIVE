#include "neon_rf.h"
#include <Arduino.h>
#include <esp_system.h>
#include <string.h>
#if defined(NEONDRIVE_TARGET_M5TAB5) && (defined(TAB5_TEST_C6_SDIO) || defined(TAB5_TEST_C6_SCAN))
#include <WiFi.h>
#endif

// Last RF action persisted in-memory across the current boot.
// TODO: persist to LittleFS /rf_last_action.txt so the value survives a
//       crash-reboot. Call neon_rf_set_last_action() from each RF path that
//       could trigger an SDIO / driver fault, then read it back in neon_rf_init()
//       on next boot to identify which action caused the crash.
static char s_last_rf_action[64] = "";

#if defined(NEONDRIVE_TARGET_M5TAB5) && (defined(TAB5_TEST_C6_SDIO) || defined(TAB5_TEST_C6_SCAN))
// Official M5Tab5 C6 SDIO mapping (P4 host -> C6):
// CLK=G12, CMD=G13, D0=G11, D1=G10, D2=G9, D3=G8, RST=G15
static constexpr int TAB5_SDIO_CLK = 12;
static constexpr int TAB5_SDIO_CMD = 13;
static constexpr int TAB5_SDIO_D0  = 11;
static constexpr int TAB5_SDIO_D1  = 10;
static constexpr int TAB5_SDIO_D2  = 9;
static constexpr int TAB5_SDIO_D3  = 8;
static constexpr int TAB5_SDIO_RST = 15;

static void tab5_configure_wifi_sdio_pins_once() {
    static bool configured = false;
    if (configured) return;
    configured = true;
    Serial.printf("[neon_rf] tab5 hosted SDIO pins expected: clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d rst=%d\n",
                  TAB5_SDIO_CLK, TAB5_SDIO_CMD, TAB5_SDIO_D0, TAB5_SDIO_D1,
                  TAB5_SDIO_D2, TAB5_SDIO_D3, TAB5_SDIO_RST);
}
#endif

// ── Capability tables ──────────────────────────────────────────────────────

#if defined(NEONDRIVE_TARGET_M5TAB5)

// ESP32-P4 + ESP32-C6 co-processor over SDIO.
static const NeonRfCaps s_caps = {
#if defined(TAB5_TEST_C6_SCAN)
    /* supports_station_scan    */ true,
#else
    /* supports_station_scan    */ false,
#endif
    /* supports_channel_control */ false,
    /* supports_promiscuous_rx  */ false,
    /* supports_raw_frame_tx    */ false,
    /* supports_ble             */ false,
    /* requires_remote_coprocessor  */ true,
    /* requires_custom_c6_firmware  */ false,
#if defined(TAB5_TEST_C6_SCAN)
    /* backend_name             */ "tab5-c6-scan",
#else
    /* backend_name             */ "tab5-none",
#endif
};

bool neon_rf_is_local_radio() { return false; }

#else  // CYD / classic ESP32 local-radio targets

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
#if defined(TAB5_TEST_C6_SDIO) || defined(TAB5_TEST_C6_SCAN)
    tab5_configure_wifi_sdio_pins_once();
#endif
#if defined(TAB5_TEST_C6_SDIO)
    Serial.println("[neon_rf] TAB5_TEST_C6_SDIO: runtime probe enabled (boot init disabled).");
#endif
#if defined(TAB5_TEST_C6_SCAN)
    Serial.println("[neon_rf] TAB5_TEST_C6_SCAN: scan path UNBLOCKED for bringup.");
#endif
#if !defined(TAB5_TEST_C6_SCAN)
    Serial.println("[neon_rf] Tab5: local RF ops BLOCKED. C6 backend not yet implemented.");
    Serial.println("[neon_rf] WiFi features need SDIO C6 init -- will be unblocked when resolved.");
#endif
#endif
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
