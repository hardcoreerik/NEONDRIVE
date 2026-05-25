/**
 * hal_cardputer_adv.cpp — M5Stack Cardputer Advanced HAL implementation
 * ======================================================================
 * Target: ESP32-S3FN8 (8 MB flash, no PSRAM)
 *
 * Display: 1.14" IPS-LCD 240×135, ST7789V2, managed by M5GFX via M5Cardputer.
 * Input:   56-key mechanical keyboard, TCA8418 I²C scan controller (SDA=GPIO8,
 *          SCL=GPIO9, INT=GPIO11). Read via M5Cardputer.keyboard API.
 * WiFi:    On-chip ESP32-S3. Standard WiFi.h — no co-processor, no pin routing.
 * SD:      SPI mode: SCK=40, MISO=39, MOSI=14, CS=12.
 * Touch:   None — keyboard navigation only (neon_hal_touch_get returns no-touch).
 *
 * IMPORTANT: M5Cardputer.begin() must be called before neon_rf_init().
 *   Unlike Tab5, there is no SDIO sequencing requirement — WiFi init can
 *   happen immediately after M5Cardputer.begin() completes.
 */
#if defined(NEONDRIVE_TARGET_M5CARDPUTER)

#include "neon_hal.h"
#include <M5Cardputer.h>
#include <WiFi.h>

// ── Display ───────────────────────────────────────────────────────────────────

void neon_hal_display_size(int *width, int *height)
{
    if (width)  *width  = 240;
    if (height) *height = 135;
}

// ── UI metrics ────────────────────────────────────────────────────────────────
//
// 240×135 is a very compact canvas. Every pixel counts.
// All font pointers are nullptr — falls back to tft.setTextSize() with the
// default GLCD bitmap font (6×8 px per char at size 1, 12×16 at size 2).
//
// At size 1:  ~40 chars per row, ~16 rows visible in full screen
// At size 2:  ~20 chars per row, ~8 rows visible in full screen
//
// Layout:
//   Header (18 px) + content + bottom bar (20 px) = 135 px
//   Available content height: 135 - 18 - 20 = 97 px ≈ 8 rows at row_h=12

static const neon_hal_ui_t s_cardputer_ui = {
    /* safe_margin  */ 4,
    /* top_gap      */ 2,
    /* header_h     */ 18,
    /* bottom_bar_h */ 20,
    /* btn_h        */ 20,
    /* btn_gap      */ 3,
    /* row_h        */ 12,
    /* pad          */ 3,
    /* border_r     */ 4,
    /* font_sm      */ nullptr, /* font_md */ nullptr,
    /* font_lg      */ nullptr, /* font_mono */ nullptr,
    /* text_size_sm */ 1, /* text_size_md */ 1, /* text_size_lg */ 2,
    /* reserve_x    */ 0, /* reserve_y */ 0,
    /* reserve_w    */ 0, /* reserve_h */ 0,
};

const neon_hal_ui_t *neon_hal_ui_metrics(void)
{
    return &s_cardputer_ui;
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
// On-chip ESP32-S3 WiFi. No pin routing or boot delay needed.

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
// No touch hardware. Navigation is keyboard-only.

neon_touch_t neon_hal_touch_get(void)
{
    neon_touch_t t = { false, -1, -1 };
    return t;
}

// ── Keyboard ──────────────────────────────────────────────────────────────────
//
// M5Cardputer.update() must be called each frame before neon_hal_key_get().
// The TCA8418 controller reports key-down events; we return one event per call.
//
// Key mapping:
//   Arrow cluster (fn+wasd or dedicated keys) → NeonKey::UP/DOWN/LEFT/RIGHT
//   Enter / Return                            → NeonKey::ENTER
//   Backspace / ESC                           → NeonKey::BACK
//   Printable ASCII                           → NeonKey::CHAR + ch

neon_key_t neon_hal_key_get(void)
{
    neon_key_t result = { NeonKey::NONE, 0 };

    if (!M5Cardputer.Keyboard.isChange()) {
        return result;
    }

    // getKey() returns a Keyboard_Class::KeysState struct with pressed/released arrays
    auto &kb = M5Cardputer.Keyboard;

    // Check the most recent key press
    if (kb.isPressed()) {
        auto state = kb.keysState();
        // Iterate through currently-held keys; take the first non-modifier
        for (char c : state.word) {
            if (c == 0) continue;

            // Navigation mappings — M5Cardputer uses special values for arrow keys
            // The M5Cardputer keyboard maps arrow keys via fn+wasd by default
            switch ((uint8_t)c) {
                case 0xB5: result.key = NeonKey::UP;    return result; // Arrow Up
                case 0xB6: result.key = NeonKey::DOWN;  return result; // Arrow Down
                case 0xB4: result.key = NeonKey::LEFT;  return result; // Arrow Left
                case 0xB7: result.key = NeonKey::RIGHT; return result; // Arrow Right
                case '\n':
                case '\r': result.key = NeonKey::ENTER; return result;
                case 0x08:
                case 0x7F: result.key = NeonKey::BACK;  return result; // Backspace / Del
                case 0x1B: result.key = NeonKey::BACK;  return result; // ESC
                default:
                    if (c >= 0x20 && c < 0x7F) {
                        result.key = NeonKey::CHAR;
                        result.ch  = c;
                        return result;
                    }
                    break;
            }
        }
    }

    return result;
}

#endif // NEONDRIVE_TARGET_M5CARDPUTER
