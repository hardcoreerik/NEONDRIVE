/**
 * hal_t_embed_cc1101.cpp — LilyGO T-Embed CC1101 Plus HAL
 * =========================================================
 * ESP32-S3, 320×170 ST7789 (landscape), no touch, rotary encoder + side button.
 * All SPI devices share GPIO 9/10/11; CS management is caller's responsibility.
 *
 * Pins verified from: examples/factory_test/utilities.h
 * Official repo: https://github.com/Xinyuan-LilyGO/T-Embed-CC1101
 */
#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)

#include "neon_hal.h"
#include <Arduino.h>
#include <WiFi.h>

// ── Display ───────────────────────────────────────────────────────────────────

void neon_hal_display_size(int *width, int *height)
{
    if (width)  *width  = 320;
    if (height) *height = 170;
}

// ── UI metrics ────────────────────────────────────────────────────────────────
// Compact values for 320×170 canvas. All font pointers nullptr (GLCD fallback).

static const neon_hal_ui_t s_tembed_ui = {
    /* safe_margin  */ 4,
    /* top_gap      */ 4,
    /* header_h     */ 20,
    /* bottom_bar_h */ 20,
    /* btn_h        */ 20,
    /* btn_gap      */ 4,
    /* row_h        */ 12,
    /* pad          */ 3,
    /* border_r     */ 4,
    /* font_sm      */ nullptr, /* font_md */ nullptr,
    /* font_lg      */ nullptr, /* font_mono */ nullptr,
    /* text_size_sm */ 1, /* text_size_md */ 1, /* text_size_lg */ 2,
    /* reserve_x    */ 0, /* reserve_y */ 0, /* reserve_w */ 0, /* reserve_h */ 0,
};

const neon_hal_ui_t *neon_hal_ui_metrics(void)
{
    return &s_tembed_ui;
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
// On-chip ESP32-S3 WiFi — same path as CYD.

void neon_hal_wifi_init(void)
{
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
// T-Embed has no touch controller.

neon_touch_t neon_hal_touch_get(void)
{
    neon_touch_t t = { false, -1, -1 };
    return t;
}

// ── Keyboard / encoder ────────────────────────────────────────────────────────
// Encoder A/B and button are polled in main.cpp via PIN_ENCODER_A/B/PIN_NAV_SELECT.
// neon_hal_key_get() is a stub here — encoder events flow through the existing
// NEONDRIVE_TARGET_BUTTON_NAV path in main.cpp until Phase 4 migrates them here.

neon_key_t neon_hal_key_get(void)
{
    return { NeonKey::NONE, 0 };
}

#endif // NEONDRIVE_TARGET_T_EMBED_CC1101
