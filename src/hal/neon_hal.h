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
 *   hal_m5tab5.cpp        — M5Stack Tab5 (ESP32-P4 + ESP32-C6 SDIO WiFi 6)
 *   hal_cardputer_adv.cpp — M5Stack Cardputer Advanced (ESP32-S3, 240×135, keyboard)
 *   hal_cyd.cpp           — CYD 2.4/2.8/3.5", T-Display-S3, T-Embed-CC1101
 *
 * Status: scaffold.  Stubs are in place; implementations expand as features
 *         are ported per-target.  Call sites in main.cpp will be migrated
 *         from #ifdef blocks to HAL calls as features are added.
 */

#include <stdint.h>
#include <stdbool.h>

// ── M5GFX umbrella ────────────────────────────────────────────────────────────
// Targets that share the M5GFX/M5Unified display stack.
// Use NEONDRIVE_USES_M5GFX instead of NEONDRIVE_TARGET_M5TAB5 for any
// code that is M5GFX-generic (tft binding, named fonts, clip rect, M5.update).
// Tab5-specific code (SDIO WiFi, GT911 touch, IMU rotation) still uses
// NEONDRIVE_TARGET_M5TAB5 directly.
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  #define NEONDRIVE_USES_M5GFX 1
#endif

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

// ── UI metrics ────────────────────────────────────────────────────────────────

/**
 * Platform-specific UI layout dimensions and font pointers.
 * Font pointers are const void* for library independence; cast to
 * lgfx::IFont* inside applyFont() when NEONDRIVE_TARGET_M5TAB5 is defined.
 * All pointer fields are nullptr on non-Tab5 targets (fall back to
 * tft.setTextSize(text_size_*) using the default GLCD bitmap font).
 */
struct neon_hal_ui_t {
    int safe_margin;    ///< outer padding from screen edge
    int top_gap;        ///< gap below top edge before header
    int header_h;       ///< header band height
    int bottom_bar_h;   ///< bottom navigation bar height
    int btn_h;          ///< standard button height
    int btn_gap;        ///< gap between buttons
    int row_h;          ///< list/table row height
    int pad;            ///< inner padding inside panels
    int border_r;       ///< border radius for rounded rects

    const void *font_sm;   ///< small body font (nullptr → text_size_sm)
    const void *font_md;   ///< medium / button font (nullptr → text_size_md)
    const void *font_lg;   ///< large / header font  (nullptr → text_size_lg)
    const void *font_mono; ///< monospace / data font (nullptr → text_size_sm)

    int text_size_sm;   ///< fallback setTextSize for non-Tab5 small
    int text_size_md;   ///< fallback setTextSize for non-Tab5 medium
    int text_size_lg;   ///< fallback setTextSize for non-Tab5 large

    // Optional hard no-draw reserve rectangle (e.g., Hypercub overlay area).
    // Set width/height to 0 to disable.
    int reserve_x;
    int reserve_y;
    int reserve_w;
    int reserve_h;
};

/** Return pointer to this platform's UI metrics (never NULL). */
const neon_hal_ui_t *neon_hal_ui_metrics(void);

// ── Keyboard ──────────────────────────────────────────────────────────────────

/**
 * Logical key codes returned by neon_hal_key_get().
 * Navigation keys map directly to UI focus movement.
 * CHAR carries an ASCII character for text-entry fields.
 */
enum class NeonKey : uint8_t {
    NONE       = 0,
    UP,
    DOWN,
    LEFT,
    RIGHT,
    ENTER,
    BACK,       ///< Backspace / ESC / physical Back button
    CHAR,       ///< Printable character — inspect neon_key_t::ch
    SCREENSHOT  ///< Capture screen to SD card (Tab key on Cardputer ADV)
};

struct neon_key_t {
    NeonKey key;
    char    ch; ///< ASCII value when key == NeonKey::CHAR, else 0
};

/**
 * Poll for a single pending key event (non-blocking).
 * Returns {NeonKey::NONE, 0} when no key is available.
 *
 * Cardputer: reads TCA8418 via M5Cardputer.update() + keyboard API.
 * All other targets: stub returning NONE (no physical keyboard).
 */
neon_key_t neon_hal_key_get(void);
