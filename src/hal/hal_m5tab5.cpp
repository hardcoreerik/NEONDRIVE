/**
 * hal_m5tab5.cpp — M5Stack Tab5 HAL implementation
 * =================================================
 * Target: ESP32-P4 (main processor) + ESP32-C6 (WiFi 6 co-processor)
 *
 * WiFi:    Routed to ESP32-C6 via SDIO bus (ESP-Hosted firmware v2.12.6).
 *          See CLAUDE.md "WiFi on Tab5" for full bringup history.
 * Display: MIPI-DSI 5" 1280×720, managed by M5GFX / M5Unified.
 * Touch:   Goodix GT911 capacitive, managed by M5GFX (I2C SDA=31, SCL=32).
 *
 * IMPORTANT: Do not add WiFi calls outside neon_hal_wifi_init().
 *   On Tab5, WiFi.setPins() MUST precede WiFi.mode().
 *   The 1500 ms delay is mandatory — the C6 SDIO slave needs time to boot.
 *   See CLAUDE.md for the full list of Tab5-specific anti-patterns.
 */
#if defined(NEONDRIVE_TARGET_M5TAB5)

#include "neon_hal.h"
#include <WiFi.h>
#include <M5Unified.h>

// SDIO pins from ESP32-P4 → ESP32-C6 co-processor (verified 2026-05-24)
#define _TAB5_WIFI_CLK   12
#define _TAB5_WIFI_CMD   13
#define _TAB5_WIFI_D0    11
#define _TAB5_WIFI_D1    10
#define _TAB5_WIFI_D2     9
#define _TAB5_WIFI_D3     8
#define _TAB5_WIFI_RST   15

// ── Display ───────────────────────────────────────────────────────────────────

void neon_hal_display_size(int *width, int *height)
{
    if (width)  *width  = 1280;
    if (height) *height = 720;
}

// ── UI metrics ────────────────────────────────────────────────────────────────
//
// All layout values are proportional to 1280×720.
// Fonts are pre-compiled bitmaps from M5GFX — no filesystem, no pixel-doubling.
//
//   header_h     =  80 px  ≈ H×0.111  (Montserrat-36 ~42 px + 19 px padding)
//   row_h        =  48 px  ≈ H×0.067  (Montserrat-28 ~34 px + 14 px padding)
//   bottom_bar_h = 112 px  ≈ H×0.156
//   btn_h        =  72 px  ≈ H×0.100  (comfortable capacitive tap target)
//   safe_margin  =  24 px  ≈ W×0.019
//   pad          =  16 px
//   border_r     =  12 px

#include <lgfx/v1/lgfx_fonts.hpp>

static const neon_hal_ui_t s_tab5_ui = {
    /* safe_margin  */ 24,
    /* top_gap      */ 16,
    /* header_h     */ 80,
    /* bottom_bar_h */ 112,
    /* btn_h        */ 72,
    /* btn_gap      */ 20,
    /* row_h        */ 48,
    /* pad          */ 16,
    /* border_r     */ 12,

    /* font_sm      */ &lgfx::fonts::lv_font_montserrat_20,
    /* font_md      */ &lgfx::fonts::lv_font_montserrat_28,
    /* font_lg      */ &lgfx::fonts::lv_font_montserrat_36,
    /* font_mono    */ &lgfx::fonts::FreeMono18pt7b,

    /* text_size_sm */ 1,
    /* text_size_md */ 1,
    /* text_size_lg */ 1,
};

const neon_hal_ui_t *neon_hal_ui_metrics(void)
{
    return &s_tab5_ui;
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

void neon_hal_wifi_init(void)
{
    // C6 SDIO slave firmware (v2.12.6, factory) needs ~1 s to boot before
    // the P4 host stack can talk to it. This delay is mandatory.
    delay(1500);

    // Route SDIO bus to C6 pins BEFORE WiFi.mode() — order is critical.
    WiFi.setPins(_TAB5_WIFI_CLK, _TAB5_WIFI_CMD,
                 _TAB5_WIFI_D0,  _TAB5_WIFI_D1,
                 _TAB5_WIFI_D2,  _TAB5_WIFI_D3,
                 _TAB5_WIFI_RST);

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
    // M5.update() must have been called this frame before we get here.
    auto td = M5.Touch.getDetail(0);
    neon_touch_t t;
    t.pressed = (td.isPressed() || td.wasPressed());
    t.x       = td.x;
    t.y       = td.y;
    return t;
}

#endif // NEONDRIVE_TARGET_M5TAB5
