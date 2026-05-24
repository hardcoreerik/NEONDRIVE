#pragma once
/**
 * neon_hal.h — NEONDRIVE Hardware Abstraction Layer
 * ==================================================
 * Platform-independent interface for all hardware capabilities that differ
 * across NEONDRIVE targets (display size, WiFi stack, touch controller, etc.)
 *
 * Feature code in main.cpp calls these functions.
 * Each target provides its own implementation in hal_<target>.cpp.
 * No #ifdef blocks belong in feature code — they live in the HAL files.
 *
 * Current implementations:
 *   hal_m5tab5.cpp  — M5Stack Tab5 (ESP32-P4 + ESP32-C6 SDIO WiFi 6)
 *   hal_cyd.cpp     — CYD 2.4/2.8/3.5", T-Display-S3, T-Embed-CC1101
 *
 * Status: scaffold.  Stubs are in place; implementations expand as features
 *         are ported per-target.  Call sites in main.cpp will be migrated
 *         from #ifdef blocks to HAL calls as features are added.
 */

#include <stdint.h>
#include <stdbool.h>

// ── Display ───────────────────────────────────────────────────────────────────

/**
 * Physical pixel dimensions of this board's display.
 * Either pointer may be NULL if the caller only needs one dimension.
 */
void neon_hal_display_size(int *width, int *height);

// ── WiFi ──────────────────────────────────────────────────────────────────────

/**
 * Initialise the WiFi subsystem for this platform.
 * Must be called once before any other neon_hal_wifi_* call.
 *
 * Tab5:      configures SDIO bus pins → ESP32-C6 co-processor (ESP-Hosted).
 *            Includes a mandatory 1500 ms boot delay for the C6.
 * CYD/other: configures the on-chip ESP32 classic WiFi stack. No delay needed.
 */
void neon_hal_wifi_init(void);

/**
 * Start a WiFi scan (blocking).
 * Returns number of networks found, or negative on error.
 */
int neon_hal_wifi_scan(void);

/**
 * Connect to a WPA2 access point (blocking, 15 s timeout).
 * Returns 0 on success, negative on error or timeout.
 */
int neon_hal_wifi_connect(const char *ssid, const char *password);

/** Disconnect and shut down WiFi. */
void neon_hal_wifi_deinit(void);

// ── Touch ─────────────────────────────────────────────────────────────────────

struct neon_touch_t {
    bool pressed;   ///< true if finger is currently down or just lifted
    int  x;         ///< last known x coordinate (pixels)
    int  y;         ///< last known y coordinate (pixels)
};

/**
 * Read current first-finger touch state.
 *
 * Prerequisites:
 *   Tab5:  M5.update() must have been called this frame.
 *   CYD:   touchscreen.read() / equivalent must have been called this frame.
 *
 * Does NOT call update() internally — caller owns the frame pump.
 */
neon_touch_t neon_hal_touch_get(void);
