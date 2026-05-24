#pragma once
// neon_rf — RF backend abstraction for NEONDRIVE
//
// On CYD targets:  local ESP32 radio, all caps true, calls pass through.
// On M5Tab5:       ESP32-P4 + ESP32-C6 co-processor over SDIO.
//                  No hosted backend implemented yet.
//                  All RF ops return ESP_ERR_NOT_SUPPORTED.
//
// Future C6 hosted / wifi_remote backend plugs in here under
// #if defined(NEONDRIVE_TARGET_M5TAB5).  UI call sites never need to change.

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>

struct NeonRfCaps {
    bool     supports_station_scan;
    bool     supports_channel_control;
    bool     supports_promiscuous_rx;
    bool     supports_raw_frame_tx;
    bool     supports_ble;
    bool     requires_remote_coprocessor;
    bool     requires_custom_c6_firmware;
    const char* backend_name;
};

// Call once from setup() after Serial.begin().
// Logs reset reason, build target, RF capability summary, and last_rf_action.
void               neon_rf_init();

// true on CYD / local-radio targets; false on Tab5.
bool               neon_rf_is_local_radio();

// Capability struct — valid after neon_rf_init().
const NeonRfCaps*  neon_rf_get_caps();

// Store the name of the RF operation about to execute.
// Call before any RF function that could crash so next boot can diagnose the cause.
// TODO: persist across reboot via LittleFS /rf_last_action.txt (in-memory for now).
void               neon_rf_set_last_action(const char* action);
const char*        neon_rf_get_last_action();

// Log "[neon_rf] <fn>: ESP_ERR_NOT_SUPPORTED (backend=<name>)" and return the error.
esp_err_t          neon_rf_log_unsupported(const char* fn_name);

// Tab5 C6 bringup test hook: explicit, on-demand SDIO init probe.
// This must never run implicitly at boot in test firmware.
esp_err_t          neon_rf_c6test_sdio_init();
