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
// That calls updateKeyList() + updateKeysState() which populate the state struct.
//
// KeysState API (from Keyboard.h):
//   state.enter  — bool, true when Enter is held
//   state.del    — bool, true when Backspace is held
//   state.fn     — bool, true when FN is held
//   state.shift  — bool, true when Shift is held
//   state.opt    — bool, true when OPT is held
//   state.word   — std::vector<char>, printable chars (no Enter/BS codes)
//
// Physical arrow keys (bottom-right of keyboard) map via the TCA8418 remap
// to key_value_map positions [3][10..12], which produce these chars in state.word:
//   ','       → LEFT  arrow  (key_value_map [3][10] value_first)
//   '.'       → DOWN  arrow  (key_value_map [3][11] value_first)
//   '>' (Shift+.) → UP arrow  (key_value_map [3][11] value_second)
//   '/'       → RIGHT arrow  (key_value_map [3][12] value_first)
//
// FN+WASD is kept as an alternate navigation scheme for users who prefer it.
//
// Key mapping summary:
//   , / . / > / /    → NeonKey::LEFT/DOWN/UP/RIGHT  (dedicated arrow keys)
//   FN + A/S/W/D     → NeonKey::LEFT/DOWN/UP/RIGHT  (alternate)
//   Enter            → NeonKey::ENTER  (state.enter bool)
//   Backspace/OPT    → NeonKey::BACK   (state.del / state.opt bool)
//   Other printable  → NeonKey::CHAR + ch

neon_key_t neon_hal_key_get(void)
{
    neon_key_t result = { NeonKey::NONE, 0 };

    if (!M5Cardputer.Keyboard.isChange()) {
        return result;
    }

    auto &kb = M5Cardputer.Keyboard;
    if (!kb.isPressed()) {
        return result;  // key-up event — no action
    }

    const auto &state = kb.keysState();

    // Enter
    if (state.enter) {
        result.key = NeonKey::ENTER;
        return result;
    }

    // Tab → screenshot
    if (state.tab) {
        result.key = NeonKey::SCREENSHOT;
        return result;
    }

    // Backspace or OPT → Back
    if (state.del || state.opt) {
        result.key = NeonKey::BACK;
        return result;
    }

    // FN + WASD → directional navigation (alternate)
    if (state.fn) {
        for (char c : state.word) {
            switch (c) {
                case 'w': case 'W': result.key = NeonKey::UP;    return result;
                case 's': case 'S': result.key = NeonKey::DOWN;  return result;
                case 'a': case 'A': result.key = NeonKey::LEFT;  return result;
                case 'd': case 'D': result.key = NeonKey::RIGHT; return result;
                default: break;
            }
        }
        return result;  // FN + unrecognised — ignore
    }

    // Dedicated arrow keys (bottom-right of ADV keyboard)
    for (char c : state.word) {
        switch (c) {
            case ',': result.key = NeonKey::LEFT;  return result;
            case '.': result.key = NeonKey::DOWN;  return result;
            case '>': result.key = NeonKey::UP;    return result;  // Shift + .
            case '/': result.key = NeonKey::RIGHT; return result;
            default: break;
        }
    }

    // Other printable character
    for (char c : state.word) {
        if (c >= 0x20 && c < 0x7F) {
            result.key = NeonKey::CHAR;
            result.ch  = c;
            return result;
        }
    }

    return result;
}

#endif // NEONDRIVE_TARGET_M5CARDPUTER
