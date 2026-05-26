/**
 * hal_t_embed_cc1101.cpp — LilyGO T-Embed CC1101 Plus HAL
 * =========================================================
 * ESP32-S3, 320×170 ST7789 (landscape), no touch.
 * Input: rotary encoder + encoder button + side key.
 * All pins verified from official pinmap diagram and factory_test/utilities.h.
 *
 * neon_hal_key_get() uses mathertel/RotaryEncoder for clean quadrature decode
 * with debounced edge events for both buttons.  main.cpp no longer polls raw
 * GPIO for encoder A/B — it calls neon_hal_key_get() instead.
 */
#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)

#include "neon_hal.h"
#include <Arduino.h>
#include <RotaryEncoder.h>
#include <WiFi.h>

// ── Pin constants ─────────────────────────────────────────────────────────────
// Source: official pinmap + examples/factory_test/utilities.h
static constexpr int TEMBED_ENCODER_A   = 4;   // ENCODER_INA
static constexpr int TEMBED_ENCODER_B   = 5;   // ENCODER_INB
static constexpr int TEMBED_ENCODER_BTN = 0;   // ENCODER_KEY (shared with BOOT)
static constexpr int TEMBED_KEY         = 6;   // BOARD_USER_KEY (side key)
static constexpr uint32_t DEBOUNCE_MS   = 40;

// ── Encoder state ─────────────────────────────────────────────────────────────

static RotaryEncoder *s_enc = nullptr;

static void encoder_init() {
    if (s_enc) return;
    s_enc = new RotaryEncoder(TEMBED_ENCODER_A, TEMBED_ENCODER_B,
                              RotaryEncoder::LatchMode::TWO03);
    pinMode(TEMBED_ENCODER_BTN, INPUT_PULLUP);
    pinMode(TEMBED_KEY,         INPUT_PULLUP);
}

// ── Display ───────────────────────────────────────────────────────────────────

void neon_hal_display_size(int *width, int *height)
{
    if (width)  *width  = 320;
    if (height) *height = 170;
}

// ── UI metrics ────────────────────────────────────────────────────────────────
// Compact values for 320×170 canvas. No font pointers (GLCD fallback).

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
// On-chip ESP32-S3 WiFi — same stack as CYD.

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

neon_touch_t neon_hal_touch_get(void)
{
    neon_touch_t t = { false, -1, -1 };
    return t;
}

// ── Key input ─────────────────────────────────────────────────────────────────
// Returns one edge event per call.  Caller must poll every loop() tick.
//
// Priority: encoder rotation > encoder button > side key > NONE
// Rotation fires on each detent (TWO03 mode = one event per click).
// Button/key events fire on falling edge with DEBOUNCE_MS suppression.

neon_key_t neon_hal_key_get(void)
{
    encoder_init();
    s_enc->tick();

    // Encoder rotation — one event per detent, no debounce needed
    const int dir = static_cast<int>(s_enc->getDirection());
    if (dir > 0) return { NeonKey::DOWN, 0 };
    if (dir < 0) return { NeonKey::UP,   0 };

    // Encoder push button — GPIO 0, active low
    static bool s_btn_prev  = true;
    static uint32_t s_btn_t = 0;
    const bool btn = digitalRead(TEMBED_ENCODER_BTN);
    if (!btn && s_btn_prev && (millis() - s_btn_t > DEBOUNCE_MS)) {
        s_btn_prev = false;
        s_btn_t    = millis();
        return { NeonKey::ENTER, 0 };
    }
    if (btn) s_btn_prev = true;

    // Side key — GPIO 6, active low → BACK / cancel
    static bool s_key_prev  = true;
    static uint32_t s_key_t = 0;
    const bool key = digitalRead(TEMBED_KEY);
    if (!key && s_key_prev && (millis() - s_key_t > DEBOUNCE_MS)) {
        s_key_prev = false;
        s_key_t    = millis();
        return { NeonKey::BACK, 0 };
    }
    if (key) s_key_prev = true;

    return { NeonKey::NONE, 0 };
}

#endif // NEONDRIVE_TARGET_T_EMBED_CC1101
