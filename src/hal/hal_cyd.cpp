/**
 * hal_cyd.cpp — CYD / T-Display-S3 / T-Embed HAL implementation
 * ==============================================================
 * Covers all non-Tab5 targets:
 *   - CYD 2.4" / 2.8" / 3.5" (ILI9341/ST7789, XPT2046 resistive touch)
 *   - LilyGO T-Display-S3     (ST7789, capacitive touch)
 *   - LilyGO T-Embed-CC1101   (no touch)
 *
 * WiFi:    On-chip ESP32 classic WiFi stack.  No pin routing needed.
 * Display: Managed by TFT_eSPI.  Dimensions vary by board variant.
 * Touch:   XPT2046 resistive via SPI (CYD) or capacitive (T-Display-S3).
 *          TODO: route neon_hal_touch_get() through XPT2046 once the
 *          existing touch path in main.cpp is migrated to the HAL.
 */
#if !defined(NEONDRIVE_TARGET_M5TAB5)

#include "neon_hal.h"
#include <WiFi.h>

// ── Display ───────────────────────────────────────────────────────────────────

void neon_hal_display_size(int *width, int *height)
{
#if defined(NEONDRIVE_TARGET_CYD35)
    // CYD 3.5" — ILI9488 480×320
    if (width)  *width  = 480;
    if (height) *height = 320;
#elif defined(NEONDRIVE_TARGET_TDISPLAY_S3)
    // LilyGO T-Display-S3 — ST7789 320×170
    if (width)  *width  = 320;
    if (height) *height = 170;
#elif defined(NEONDRIVE_TARGET_TEMBED)
    // LilyGO T-Embed-CC1101 — ST7789 240×135
    if (width)  *width  = 240;
    if (height) *height = 135;
#else
    // CYD 2.4" / 2.8" — ILI9341 240×320 (portrait native, landscape in use)
    if (width)  *width  = 240;
    if (height) *height = 320;
#endif
}

// ── UI metrics ────────────────────────────────────────────────────────────────
//
// Values match the existing hardcoded constants exactly — CYD behaviour is
// unchanged.  All font pointers are nullptr; applyFont() falls through to
// tft.setTextSize(text_size_*) using the default GLCD bitmap font.
//
// CYD 3.5" gets slightly larger values to make use of the extra pixels.

static const neon_hal_ui_t s_cyd_ui = {
#if defined(NEONDRIVE_TARGET_CYD35)
    /* safe_margin  */ 10,
    /* top_gap      */ 6,
    /* header_h     */ 32,
    /* bottom_bar_h */ 40,
    /* btn_h        */ 32,
    /* btn_gap      */ 6,
    /* row_h        */ 18,
    /* pad          */ 5,
    /* border_r     */ 6,
    /* font_sm      */ nullptr, /* font_md */ nullptr,
    /* font_lg      */ nullptr, /* font_mono */ nullptr,
    /* text_size_sm */ 1, /* text_size_md */ 2, /* text_size_lg */ 3,
#else
    // CYD 2.4" / 2.8" / T-Display-S3 / T-Embed — match existing constants
    /* safe_margin  */ 8,
    /* top_gap      */ 6,
    /* header_h     */ 30,
    /* bottom_bar_h */ 36,
    /* btn_h        */ 30,
    /* btn_gap      */ 6,
    /* row_h        */ 16,
    /* pad          */ 4,
    /* border_r     */ 6,
    /* font_sm      */ nullptr, /* font_md */ nullptr,
    /* font_lg      */ nullptr, /* font_mono */ nullptr,
    /* text_size_sm */ 1, /* text_size_md */ 2, /* text_size_lg */ 3,
#endif
};

const neon_hal_ui_t *neon_hal_ui_metrics(void)
{
    return &s_cyd_ui;
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

void neon_hal_wifi_init(void)
{
    // Classic ESP32: WiFi stack lives on-chip. No pin routing or boot delay.
    WiFi.mode(WIFI_STA);
}

int neon_hal_wifi_scan(void)
{
    int n = WiFi.scanNetworks();
    return (n >= 0) ? n : -1;
}

int neon_hal_wifi_connect(const char *ssid, const char *password)
{
    WiFi.begin(ssid, password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000UL) return -1;
        delay(250);
    }
    return 0;
}

void neon_hal_wifi_deinit(void)
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// ── Touch ─────────────────────────────────────────────────────────────────────

neon_touch_t neon_hal_touch_get(void)
{
    // TODO: migrate existing XPT2046 / capacitive touch path from main.cpp.
    // For now returns no-touch so call sites compile on all targets.
    neon_touch_t t = { false, -1, -1 };
    return t;
}

#endif // !NEONDRIVE_TARGET_M5TAB5
