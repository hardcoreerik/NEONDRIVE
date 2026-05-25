/**
 * hal_t_display_s3.cpp — LilyGO T-Display-S3 HAL implementation
 * ==============================================================
 * Target:
 *   - LilyGO T-Display-S3 (ST7789 320x170, optional capacitive touch)
 *
 * WiFi:    On-chip ESP32-S3 WiFi stack.
 * Display: Managed by TFT_eSPI (8-bit parallel setup via User_Setup_tdisplay_s3.h).
 * Touch:   Stub for now; main.cpp still owns the current touch path.
 */
#if defined(NEONDRIVE_TARGET_TDISPLAY_S3)

#include "neon_hal.h"
#include <Arduino.h>
#include <WiFi.h>

void neon_hal_display_size(int *width, int *height)
{
    if (width)  *width  = 320;
    if (height) *height = 170;
}

static const neon_hal_ui_t s_tdisplay_ui = {
    // T-Display-S3 has a short 170px-tall panel; use compact spacing.
    /* safe_margin  */ 4,
    /* top_gap      */ 4,
    /* header_h     */ 20,
    /* bottom_bar_h */ 22,
    /* btn_h        */ 18,
    /* btn_gap      */ 4,
    /* row_h        */ 12,
    /* pad          */ 3,
    /* border_r     */ 6,
    /* font_sm      */ nullptr, /* font_md */ nullptr,
    /* font_lg      */ nullptr, /* font_mono */ nullptr,
    /* text_size_sm */ 1, /* text_size_md */ 1, /* text_size_lg */ 2,
    /* reserve_x    */ 0, /* reserve_y    */ 0, /* reserve_w    */ 0, /* reserve_h */ 0,
};

const neon_hal_ui_t *neon_hal_ui_metrics(void)
{
    return &s_tdisplay_ui;
}

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

neon_touch_t neon_hal_touch_get(void)
{
    neon_touch_t t = { false, -1, -1 };
    return t;
}

neon_key_t neon_hal_key_get(void)
{
    static constexpr uint8_t PIN_BTN_LEFT  = BUTTON_1; // GPIO0
    static constexpr uint8_t PIN_BTN_RIGHT = BUTTON_2; // GPIO14
    static constexpr unsigned long LONG_PRESS_MS = 600;

    struct BtnState {
        bool wasDown;
        bool longFired;
        unsigned long downAtMs;
    };

    static bool s_init = false;
    static BtnState s_left  = { false, false, 0 };
    static BtnState s_right = { false, false, 0 };

    if (!s_init) {
        pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
        pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
        s_init = true;
    }

    const unsigned long now = millis();
    const bool leftDown = (digitalRead(PIN_BTN_LEFT) == LOW);
    const bool rightDown = (digitalRead(PIN_BTN_RIGHT) == LOW);

    auto processButton = [&](bool isDown, BtnState &st, NeonKey shortKey) -> NeonKey {
        if (isDown && !st.wasDown) {
            st.wasDown = true;
            st.longFired = false;
            st.downAtMs = now;
            return NeonKey::NONE;
        }
        if (isDown && st.wasDown) {
            if (!st.longFired && (now - st.downAtMs) >= LONG_PRESS_MS) {
                st.longFired = true;
                return NeonKey::ENTER; // long press = select
            }
            return NeonKey::NONE;
        }
        if (!isDown && st.wasDown) {
            st.wasDown = false;
            if (!st.longFired) {
                return shortKey; // short press = direction
            }
        }
        return NeonKey::NONE;
    };

    // Left button has priority if both events occur in the same poll cycle.
    NeonKey key = processButton(leftDown, s_left, NeonKey::LEFT);
    if (key == NeonKey::NONE) {
        key = processButton(rightDown, s_right, NeonKey::RIGHT);
    }
    if (key != NeonKey::NONE) {
        return { key, 0 };
    }
    return { NeonKey::NONE, 0 };
}

#endif // NEONDRIVE_TARGET_TDISPLAY_S3
